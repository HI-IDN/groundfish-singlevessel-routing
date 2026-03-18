#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sqlite3.h>
#include <geos_c.h>

#include "../include/constants.h"
#include "../include/dat_parser.h"
#include "../include/geo_utils.h"
#include "../include/coastline_db.h"
#include "../include/country_bootstrap.h"

typedef struct {
    double lat;
    double lon;
} GeoPoint;

typedef struct {
    GeoPoint *points;
    int n;
    int cap;
} GeoPointVec;

typedef struct {
    int    id;
    char   name[128];
    double lat;
    double lon;
} PortInfo;

typedef struct {
    PortInfo *a;
    int n;
    int cap;
} PortInfoVec;

static void port_info_vec_init(PortInfoVec *v) {
    v->n = 0; v->cap = 16;
    v->a = (PortInfo*)calloc((size_t)v->cap, sizeof(PortInfo));
}
static void port_info_vec_free(PortInfoVec *v) {
    if (!v) return;
    free(v->a); v->a = NULL; v->n = v->cap = 0;
}
static int port_info_vec_push(PortInfoVec *v, PortInfo p) {
    if (v->n == v->cap) {
        int nc = v->cap * 2;
        PortInfo *tmp = (PortInfo*)realloc(v->a, (size_t)nc * sizeof(PortInfo));
        if (!tmp) return 0;
        v->a = tmp; v->cap = nc;
    }
    v->a[v->n++] = p;
    return 1;
}

static int insert_seed_into_ring(GEOSContextHandle_t ctx,
                                 const GEOSGeometry *original_polygon,
                                 const GEOSGeometry *original_boundary,
                                 GeoPoint seed,
                                 GeoPointVec *ring);

static void die_usage(const char *argv0) {
    fprintf(stderr,
            "Usage: %s --db <gsp_data.db> --coastline-file <island.tsv> [options]\n"
            "\n"
            "Options:\n"
            "  --waypoint-file <datafile.dat> Optional DAT file for manual WAYP seeds\n"
            "  --dat <datafile.dat>        Deprecated alias for --waypoint-file\n"
            "  --port-file <datafile.dat>  DAT file used to import PORT rows into DB\n"
            "  --boat-file <datafile.dat>  DAT file used to import BOAT rows into DB\n"
            "  --preserve-all-seeds       Force all loaded seed waypoints into final ring\n"
            "  --seed-hints-only          Use loaded seed waypoints only when insertion stays valid\n"
            "  --min-points <N>           Min waypoints for medium ring (default: 40)\n"
            "  --max-points <N>           Max waypoints for medium ring (default: 200)\n"
            "  --target-points <N>        Preferred medium waypoint count (default: 50)\n"
            "  --small-points <N>         Target waypoints for small/coarse ring (default: 12)\n"
            "  --fine-points <N>          Target waypoints for fine ring used in port augmentation (default: 200)\n",
            argv0);
}

static void point_vec_init(GeoPointVec *v) {
    v->n = 0;
    v->cap = 32;
    v->points = (GeoPoint*)calloc((size_t)v->cap, sizeof(GeoPoint));
}

static void point_vec_free(GeoPointVec *v) {
    if (!v) return;
    free(v->points);
    v->points = NULL;
    v->n = 0;
    v->cap = 0;
}

static int point_vec_push(GeoPointVec *v, double lat, double lon) {
    if (v->n == v->cap) {
        int new_cap = v->cap * 2;
        GeoPoint *tmp = (GeoPoint*)realloc(v->points, (size_t)new_cap * sizeof(GeoPoint));
        if (!tmp) return 0;
        v->points = tmp;
        v->cap = new_cap;
    }
    v->points[v->n].lat = lat;
    v->points[v->n].lon = lon;
    v->n++;
    return 1;
}

static double sqr(double x) {
    return x * x;
}

static double point_dist2(GeoPoint a, GeoPoint b) {
    return sqr(a.lat - b.lat) + sqr(a.lon - b.lon);
}

static double segment_length(GeoPoint a, GeoPoint b) {
    return sqrt(point_dist2(a, b));
}

static int downsample_points_stride(const GeoPointVec *src, int target_max, GeoPointVec *dst) {
    int stride;
    if (!src || !dst || src->n <= 0) return 0;
    if (target_max <= 0 || src->n <= target_max) {
        point_vec_init(dst);
        for (int i = 0; i < src->n; i++) {
            if (!point_vec_push(dst, src->points[i].lat, src->points[i].lon)) {
                point_vec_free(dst);
                return 0;
            }
        }
        return 1;
    }

    stride = (src->n + target_max - 1) / target_max;
    point_vec_init(dst);
    for (int i = 0; i < src->n; i += stride) {
        if (!point_vec_push(dst, src->points[i].lat, src->points[i].lon)) {
            point_vec_free(dst);
            return 0;
        }
    }
    if (dst->n == 0 || point_dist2(dst->points[dst->n - 1], src->points[src->n - 1]) > 1e-12) {
        if (!point_vec_push(dst, src->points[src->n - 1].lat, src->points[src->n - 1].lon)) {
            point_vec_free(dst);
            return 0;
        }
    }
    return 1;
}

static int append_unique_point(GeoPointVec *v, double lat, double lon, double tol2) {
    GeoPoint p;
    p.lat = lat;
    p.lon = lon;
    for (int i = 0; i < v->n; i++) {
        if (point_dist2(v->points[i], p) <= tol2) {
            return 1;
        }
    }
    return point_vec_push(v, lat, lon);
}

static int decimal_deg_to_degmin_int(double deg) {
    double abs_deg = fabs(deg);
    int whole_deg = (int)floor(abs_deg);
    double minutes = (abs_deg - (double)whole_deg) * 60.0;
    double degmin = (double)(whole_deg * 100) + minutes;
    return (int)llround(degmin * 100.0);
}

static int decimal_lon_to_degmin_storage(double lon_deg) {
    /* Iceland convention in dataset: western longitudes stored as positive degmin ints. */
    return decimal_deg_to_degmin_int(fabs(lon_deg));
}

static int insert_point_after(const GeoPointVec *src, int insert_after, GeoPoint seed, GeoPointVec *dst) {
    point_vec_init(dst);
    while (dst->cap < src->n + 1) {
        int new_cap = dst->cap * 2;
        GeoPoint *tmp = (GeoPoint*)realloc(dst->points, (size_t)new_cap * sizeof(GeoPoint));
        if (!tmp) {
            point_vec_free(dst);
            return 0;
        }
        dst->points = tmp;
        dst->cap = new_cap;
    }

    for (int i = 0; i < src->n; i++) {
        dst->points[dst->n++] = src->points[i];
        if (i == insert_after) {
            dst->points[dst->n++] = seed;
        }
    }
    return 1;
}

static char *strip_quotes_local(const char *name) {
    if (!name) {
        char *empty = (char*)malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }
    const char *start = name;
    const char *end = name + strlen(name);
    if (*start == '"') start++;
    if (end > start && *(end - 1) == '"') end--;
    size_t len = (size_t)(end - start);
    char *out = (char*)malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

static int insert_location_from_degmin(sqlite3_stmt *insert_stmt,
                                       sqlite3_stmt *select_stmt,
                                       int lat_degmin,
                                       int lon_degmin) {
    double lat_deg = degmin_to_deg(lat_degmin);
    double lon_deg = degmin_to_deg_lon(lon_degmin);

    sqlite3_bind_int(select_stmt, 1, lat_degmin);
    sqlite3_bind_int(select_stmt, 2, lon_degmin);
    if (sqlite3_step(select_stmt) == SQLITE_ROW) {
        int loc_id = sqlite3_column_int(select_stmt, 0);
        sqlite3_reset(select_stmt);
        sqlite3_clear_bindings(select_stmt);
        return loc_id;
    }
    sqlite3_reset(select_stmt);
    sqlite3_clear_bindings(select_stmt);

    sqlite3_bind_int(insert_stmt, 1, lat_degmin);
    sqlite3_bind_int(insert_stmt, 2, lon_degmin);
    sqlite3_bind_double(insert_stmt, 3, lat_deg);
    sqlite3_bind_double(insert_stmt, 4, lon_deg);
    if (sqlite3_step(insert_stmt) != SQLITE_DONE) {
        sqlite3_reset(insert_stmt);
        sqlite3_clear_bindings(insert_stmt);
        return -1;
    }
    sqlite3_reset(insert_stmt);
    sqlite3_clear_bindings(insert_stmt);
    return (int)sqlite3_last_insert_rowid(sqlite3_db_handle(insert_stmt));
}

static int store_ports_from_dat(sqlite3 *db,
                                const char *dat_path,
                                int *out_seen,
                                int *out_inserted) {
    DataSet dataset;
    sqlite3_stmt *loc_insert_stmt = NULL;
    sqlite3_stmt *loc_select_stmt = NULL;
    sqlite3_stmt *port_stmt = NULL;
    int rc = SQLITE_OK;
    int seen = 0;
    int inserted = 0;

    dataset_init(&dataset);
    read_dat_file_selected(dat_path, &dataset, GSP_DAT_SELECT_PORTS);

    rc = sqlite3_prepare_v2(db,
                            "INSERT INTO locations (easting, northing, lat, lon) VALUES (?, ?, ?, ?);",
                            -1, &loc_insert_stmt, NULL);
    if (rc != SQLITE_OK) goto cleanup;

    rc = sqlite3_prepare_v2(db,
                            "SELECT id FROM locations WHERE easting = ? AND northing = ?;",
                            -1, &loc_select_stmt, NULL);
    if (rc != SQLITE_OK) goto cleanup;

    rc = sqlite3_prepare_v2(db,
                            "INSERT OR IGNORE INTO ports (name, location_id) VALUES (?, ?);",
                            -1, &port_stmt, NULL);
    if (rc != SQLITE_OK) goto cleanup;

    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
    for (int i = 0; i < dataset.n_ports; i++) {
        const Port *port = &dataset.ports[i];
        const Location *loc = &dataset.locations[port->location_id];
        seen++;

        int lat_degmin = loc->easting;
        int lon_degmin = loc->northing;
        int loc_id = insert_location_from_degmin(loc_insert_stmt, loc_select_stmt, lat_degmin, lon_degmin);
        if (loc_id <= 0) {
            rc = SQLITE_ERROR;
            sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
            goto cleanup;
        }

        char *clean_name = strip_quotes_local(port->name);
        if (!clean_name) {
            rc = SQLITE_NOMEM;
            sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
            goto cleanup;
        }

        sqlite3_bind_text(port_stmt, 1, clean_name, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(port_stmt, 2, loc_id);
        if (sqlite3_step(port_stmt) != SQLITE_DONE) {
            free(clean_name);
            sqlite3_reset(port_stmt);
            sqlite3_clear_bindings(port_stmt);
            rc = SQLITE_ERROR;
            sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
            goto cleanup;
        }
        if (sqlite3_changes(db) > 0) inserted++;
        sqlite3_reset(port_stmt);
        sqlite3_clear_bindings(port_stmt);
        free(clean_name);
    }
    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);

cleanup:
    sqlite3_finalize(loc_insert_stmt);
    sqlite3_finalize(loc_select_stmt);
    sqlite3_finalize(port_stmt);
    dataset_free(&dataset);

    if (out_seen) *out_seen = seen;
    if (out_inserted) *out_inserted = inserted;
    return rc;
}

static int store_boats_from_dat(sqlite3 *db,
                                const char *dat_path,
                                int *out_seen,
                                int *out_inserted) {
    DataSet dataset;
    sqlite3_stmt *loc_insert_stmt = NULL;
    sqlite3_stmt *loc_select_stmt = NULL;
    sqlite3_stmt *boat_stmt = NULL;
    int rc = SQLITE_OK;
    int seen = 0;
    int inserted = 0;

    dataset_init(&dataset);
    read_dat_file_selected(dat_path, &dataset, GSP_DAT_SELECT_BOATS);

    rc = sqlite3_prepare_v2(db,
                            "INSERT INTO locations (easting, northing, lat, lon) VALUES (?, ?, ?, ?);",
                            -1, &loc_insert_stmt, NULL);
    if (rc != SQLITE_OK) goto cleanup;

    rc = sqlite3_prepare_v2(db,
                            "SELECT id FROM locations WHERE easting = ? AND northing = ?;",
                            -1, &loc_select_stmt, NULL);
    if (rc != SQLITE_OK) goto cleanup;

    rc = sqlite3_prepare_v2(db,
                            "INSERT INTO boats (start_location_id, end_location_id, capacity, name) "
                            "VALUES (?, ?, ?, ?);",
                            -1, &boat_stmt, NULL);
    if (rc != SQLITE_OK) goto cleanup;

    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
    for (int i = 0; i < dataset.n_boats; i++) {
        const Boat *boat = &dataset.boats[i];
        const Location *start_loc = &dataset.locations[boat->start_location_id];
        const Location *end_loc = &dataset.locations[boat->end_location_id];
        seen++;

        int start_loc_id = insert_location_from_degmin(loc_insert_stmt, loc_select_stmt,
                                                       start_loc->easting,
                                                       start_loc->northing);
        int end_loc_id = insert_location_from_degmin(loc_insert_stmt, loc_select_stmt,
                                                     end_loc->easting,
                                                     end_loc->northing);
        if (start_loc_id <= 0 || end_loc_id <= 0) {
            rc = SQLITE_ERROR;
            sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
            goto cleanup;
        }

        char *clean_name = strip_quotes_local(boat->name);
        if (!clean_name) {
            rc = SQLITE_NOMEM;
            sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
            goto cleanup;
        }

        sqlite3_bind_int(boat_stmt, 1, start_loc_id);
        sqlite3_bind_int(boat_stmt, 2, end_loc_id);
        sqlite3_bind_int(boat_stmt, 3, boat->capacity);
        sqlite3_bind_text(boat_stmt, 4, clean_name, -1, SQLITE_TRANSIENT);

        if (sqlite3_step(boat_stmt) != SQLITE_DONE) {
            free(clean_name);
            sqlite3_reset(boat_stmt);
            sqlite3_clear_bindings(boat_stmt);
            rc = SQLITE_ERROR;
            sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
            goto cleanup;
        }

        inserted++;
        sqlite3_reset(boat_stmt);
        sqlite3_clear_bindings(boat_stmt);
        free(clean_name);
    }
    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);

cleanup:
    sqlite3_finalize(loc_insert_stmt);
    sqlite3_finalize(loc_select_stmt);
    sqlite3_finalize(boat_stmt);
    dataset_free(&dataset);

    if (out_seen) *out_seen = seen;
    if (out_inserted) *out_inserted = inserted;
    return rc;
}

static const GEOSGeometry *pick_largest_polygon_component(GEOSContextHandle_t ctx, const GEOSGeometry *geom) {
    if (!geom) return NULL;
    int type_id = GEOSGeomTypeId_r(ctx, geom);
    if (type_id == GEOS_POLYGON) return geom;
    if (type_id != GEOS_MULTIPOLYGON) return NULL;

    int num = GEOSGetNumGeometries_r(ctx, geom);
    const GEOSGeometry *best = NULL;
    double best_area = -1.0;
    for (int i = 0; i < num; i++) {
        const GEOSGeometry *candidate = GEOSGetGeometryN_r(ctx, geom, i);
        double area = 0.0;
        if (!candidate || !GEOSArea_r(ctx, candidate, &area)) continue;
        if (area > best_area) {
            best_area = area;
            best = candidate;
        }
    }
    return best;
}

static GEOSGeometry *build_polygon_from_points(GEOSContextHandle_t ctx, const GeoPointVec *ring) {
    if (!ring || ring->n < 3) return NULL;

    GEOSCoordSequence *seq = GEOSCoordSeq_create_r(ctx, (unsigned int)(ring->n + 1), 2);
    if (!seq) return NULL;

    for (int i = 0; i < ring->n; i++) {
        GEOSCoordSeq_setX_r(ctx, seq, (unsigned int)i, ring->points[i].lon);
        GEOSCoordSeq_setY_r(ctx, seq, (unsigned int)i, ring->points[i].lat);
    }
    GEOSCoordSeq_setX_r(ctx, seq, (unsigned int)ring->n, ring->points[0].lon);
    GEOSCoordSeq_setY_r(ctx, seq, (unsigned int)ring->n, ring->points[0].lat);

    GEOSGeometry *shell = GEOSGeom_createLinearRing_r(ctx, seq);
    if (!shell) {
        GEOSCoordSeq_destroy_r(ctx, seq);
        return NULL;
    }
    return GEOSGeom_createPolygon_r(ctx, shell, NULL, 0);
}

static int extract_ring_points_from_polygon(GEOSContextHandle_t ctx, const GEOSGeometry *geom, GeoPointVec *out) {
    const GEOSGeometry *polygon = pick_largest_polygon_component(ctx, geom);
    if (!polygon) return 0;

    const GEOSGeometry *ring = GEOSGetExteriorRing_r(ctx, polygon);
    const GEOSCoordSequence *seq = ring ? GEOSGeom_getCoordSeq_r(ctx, ring) : NULL;
    unsigned int size = 0;
    if (!seq || !GEOSCoordSeq_getSize_r(ctx, seq, &size) || size < 4) return 0;

    point_vec_init(out);
    for (unsigned int i = 0; i + 1 < size; i++) {
        double lon = 0.0, lat = 0.0;
        if (!GEOSCoordSeq_getX_r(ctx, seq, i, &lon) || !GEOSCoordSeq_getY_r(ctx, seq, i, &lat)) {
            point_vec_free(out);
            return 0;
        }
        if (!point_vec_push(out, lat, lon)) {
            point_vec_free(out);
            return 0;
        }
    }
    return 1;
}

static GEOSGeometry *build_point_geometry(GEOSContextHandle_t ctx, GeoPoint p) {
    GEOSCoordSequence *seq = GEOSCoordSeq_create_r(ctx, 1, 2);
    if (!seq) return NULL;
    GEOSCoordSeq_setX_r(ctx, seq, 0, p.lon);
    GEOSCoordSeq_setY_r(ctx, seq, 0, p.lat);
    return GEOSGeom_createPoint_r(ctx, seq);
}

static int validate_ring_geometry(GEOSContextHandle_t ctx,
                                  const GEOSGeometry *original_polygon,
                                  const GEOSGeometry *original_boundary,
                                  const GeoPointVec *ring) {
    int valid = 0;
    GEOSGeometry *candidate = NULL;
    GEOSGeometry *candidate_boundary = NULL;

    if (!ring || ring->n < 3) return 0;

    candidate = build_polygon_from_points(ctx, ring);
    if (!candidate) goto cleanup;
    if (GEOSisEmpty_r(ctx, candidate) || !GEOSisValid_r(ctx, candidate)) goto cleanup;
    if (!GEOSCovers_r(ctx, candidate, original_polygon)) goto cleanup;

    candidate_boundary = GEOSBoundary_r(ctx, candidate);
    if (!candidate_boundary) goto cleanup;
    if (!GEOSDisjoint_r(ctx, candidate_boundary, original_boundary)) goto cleanup;

    valid = 1;

cleanup:
    if (candidate_boundary) GEOSGeom_destroy_r(ctx, candidate_boundary);
    if (candidate) GEOSGeom_destroy_r(ctx, candidate);
    return valid;
}

static int read_waypoint_seeds_from_dat(const char *dat_path, GeoPointVec *out) {
    DataSet dataset;
    dataset_init(&dataset);
    point_vec_init(out);

    read_dat_file_selected(dat_path, &dataset, GSP_DAT_SELECT_WAYPS);

    double tol2 = 1e-10;
    for (int i = 0; i < dataset.n_waypoints; i++) {
        const Waypoint *waypoint = &dataset.waypoints[i];
        const Location *loc = &dataset.locations[waypoint->location_id];
        double lat = degmin_to_deg(loc->easting);
        double lon = degmin_to_deg_lon(loc->northing);
        if (!append_unique_point(out, lat, lon, tol2)) {
            dataset_free(&dataset);
            point_vec_free(out);
            return 0;
        }
    }

    dataset_free(&dataset);
    return 1;
}

static int load_ports_info_from_db(sqlite3 *db, PortInfoVec *out) {
    sqlite3_stmt *stmt = NULL;
    const char *sql =
        "SELECT p.id, COALESCE(p.name,''), l.lat, l.lon "
        "FROM ports p "
        "JOIN locations l ON l.id = p.location_id "
        "UNION ALL "
        "SELECT 1000000 + b.id, printf('%s [start]', COALESCE(b.name,'')), ls.lat, ls.lon "
        "FROM boats b "
        "JOIN locations ls ON ls.id = b.start_location_id "
        "UNION ALL "
        "SELECT 2000000 + b.id, printf('%s [end]', COALESCE(b.name,'')), le.lat, le.lon "
        "FROM boats b "
        "JOIN locations le ON le.id = b.end_location_id "
        "ORDER BY 1;";

    port_info_vec_init(out);
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PortInfo pi;
        pi.id  = sqlite3_column_int(stmt, 0);
        const char *nm = (const char *)sqlite3_column_text(stmt, 1);
        strncpy(pi.name, nm ? nm : "", sizeof(pi.name) - 1);
        pi.name[sizeof(pi.name)-1] = '\0';
        pi.lat = sqlite3_column_double(stmt, 2);
        pi.lon = sqlite3_column_double(stmt, 3);
        if (!port_info_vec_push(out, pi)) {
            sqlite3_finalize(stmt);
            port_info_vec_free(out);
            return 0;
        }
    }

    sqlite3_finalize(stmt);
    return 1;
}

static int segment_crosses_land_for_port_access(GEOSContextHandle_t ctx,
                                                const GEOSGeometry *coastline_polygon,
                                                const GEOSGeometry *coastline_boundary,
                                                GeoPoint a,
                                                GeoPoint b) {
    GEOSCoordSequence *seq = NULL;
    GEOSGeometry *line = NULL;
    int crosses_land = 1;

    seq = GEOSCoordSeq_create_r(ctx, 2, 2);
    if (!seq) return 1;
    GEOSCoordSeq_setX_r(ctx, seq, 0, a.lon);
    GEOSCoordSeq_setY_r(ctx, seq, 0, a.lat);
    GEOSCoordSeq_setX_r(ctx, seq, 1, b.lon);
    GEOSCoordSeq_setY_r(ctx, seq, 1, b.lat);

    line = GEOSGeom_createLineString_r(ctx, seq);
    if (!line) return 1;

    /*
     * For port access we allow touching coastline at an endpoint,
     * but not a true crossing through land.
     */
    char crosses = GEOSCrosses_r(ctx, line, coastline_boundary);
    char inside = GEOSWithin_r(ctx, line, coastline_polygon);
    crosses_land = (crosses == 1 || inside == 1) ? 1 : 0;

    GEOSGeom_destroy_r(ctx, line);
    return crosses_land;
}

static int has_port_access_via_ring(GEOSContextHandle_t ctx,
                                    const GEOSGeometry *coastline_polygon,
                                    const GEOSGeometry *coastline_boundary,
                                    GeoPoint port,
                                    const GeoPointVec *ring) {
    for (int i = 0; i < ring->n; i++) {
        if (!segment_crosses_land_for_port_access(ctx, coastline_polygon, coastline_boundary, port, ring->points[i])) {
            return 1;
        }
    }
    return 0;
}

/*
 * Build fine port-access candidates by buffering the raw coastline polygon by
 * a tiny amount (~0.005 deg ≈ 500 m).  All ring points are kept – no subsampling –
 * so even narrow fjords get good coverage.
 */
static int build_fine_candidates_from_coastline(GEOSContextHandle_t ctx,
                                                const GEOSGeometry *coastline_polygon,
                                                GeoPointVec *out) {
    const int target_max_points = 1000;
    /* 32 segments gives a smooth curve that hugs the coast closely */
    GEOSGeometry *buffered = GEOSBuffer_r(ctx, coastline_polygon, BUFFERED_COASTLINE_OFFSET_DEG, 32);
    GeoPointVec raw = {0};
    if (!buffered) return 0;

    if (!extract_ring_points_from_polygon(ctx, buffered, &raw)) {
        GEOSGeom_destroy_r(ctx, buffered);
        return 0;
    }
    GEOSGeom_destroy_r(ctx, buffered);
    if (!downsample_points_stride(&raw, target_max_points, out)) {
        point_vec_free(&raw);
        return 0;
    }
    point_vec_free(&raw);
    return 1;
}

/*
 * Try to augment `ring` so that every port in the DB has at least one ring
 * waypoint reachable without crossing land.
 *
 * Candidates come from `fine_candidates` (the tight coastline buffer).
 * Per-port failures are non-fatal: a line is printed to stdout and we move on.
 * Always returns 1.
 */
static int augment_ring_for_port_access(sqlite3 *db,
                                        GEOSContextHandle_t ctx,
                                        const GEOSGeometry *coastline_polygon,
                                        const GEOSGeometry *coastline_boundary,
                                        const GeoPointVec *fine_candidates,
                                        GeoPointVec *ring,
                                        int *out_added) {
    PortInfoVec ports = {0};
    int added = 0;

    if (!load_ports_info_from_db(db, &ports)) {
        /* Can't load ports at all – treat as no-op rather than fatal */
        if (out_added) *out_added = 0;
        return 1;
    }

    for (int p = 0; p < ports.n; p++) {
        GeoPoint pt = {ports.a[p].lat, ports.a[p].lon};

        if (has_port_access_via_ring(ctx, coastline_polygon, coastline_boundary, pt, ring))
            continue;

        /* Search fine candidates for the closest point with clear line-of-sight */
        int best_idx = -1;
        double best_d2 = 0.0;
        for (int c = 0; c < fine_candidates->n; c++) {
            if (segment_crosses_land_for_port_access(ctx, coastline_polygon, coastline_boundary,
                                                     pt, fine_candidates->points[c]))
                continue;
            double d2 = point_dist2(pt, fine_candidates->points[c]);
            if (best_idx < 0 || d2 < best_d2) { best_d2 = d2; best_idx = c; }
        }

        if (best_idx < 0) {
            printf("port_no_access: %d,%s\n", ports.a[p].id, ports.a[p].name);
            continue;
        }

        if (!insert_seed_into_ring(ctx, coastline_polygon, coastline_boundary,
                                   fine_candidates->points[best_idx], ring)) {
            printf("port_no_access: %d,%s\n", ports.a[p].id, ports.a[p].name);
            continue;
        }
        added++;
    }

    if (out_added) *out_added = added;
    port_info_vec_free(&ports);
    return 1;
}

static int choose_auto_ring(GEOSContextHandle_t ctx,
                            const GEOSGeometry *original_polygon,
                            const GEOSGeometry *original_boundary,
                            const CoastlinePoints *coastline,
                            const WaypointGenerationOptions *opts,
                            GeoPointVec *out_ring,
                            double *out_buffer,
                            double *out_tolerance) {
    double min_lat = coastline->lat[0], max_lat = coastline->lat[0];
    double min_lon = coastline->lon[0], max_lon = coastline->lon[0];
    for (int i = 1; i < coastline->n; i++) {
        if (coastline->lat[i] < min_lat) min_lat = coastline->lat[i];
        if (coastline->lat[i] > max_lat) max_lat = coastline->lat[i];
        if (coastline->lon[i] < min_lon) min_lon = coastline->lon[i];
        if (coastline->lon[i] > max_lon) max_lon = coastline->lon[i];
    }

    double lat_span = max_lat - min_lat;
    double lon_span = max_lon - min_lon;
    double span = lat_span > lon_span ? lat_span : lon_span;
    if (span <= 0.0) span = 1.0;

    double buffer_candidates[6] = {
        fmax(0.05, span * 0.010),
        fmax(0.08, span * 0.015),
        fmax(0.12, span * 0.020),
        fmax(0.18, span * 0.030),
        fmax(0.25, span * 0.040),
        fmax(0.35, span * 0.055)
    };

    int found_any = 0;
    int found_in_range = 0;
    int best_score = 0;
    double best_buffer = 0.0, best_tol = 0.0;
    GeoPointVec best_ring = {0};

    for (int b = 0; b < 6; b++) {
        GEOSGeometry *outer = GEOSBuffer_r(ctx, original_polygon, buffer_candidates[b], 8);
        if (!outer) continue;

        double max_tol = span * 0.35 + buffer_candidates[b] * 2.0;
        for (int step = 0; step <= 48; step++) {
            double tol = (max_tol * step) / 48.0;
            GEOSGeometry *simplified = NULL;
            const GEOSGeometry *poly_view = NULL;
            GeoPointVec ring = {0};
            int score;
            int in_range;
            int is_valid = 0;

            if (tol <= 0.0) simplified = GEOSGeom_clone_r(ctx, outer);
            else simplified = GEOSTopologyPreserveSimplify_r(ctx, outer, tol);
            if (!simplified) continue;

            poly_view = pick_largest_polygon_component(ctx, simplified);
            if (poly_view && extract_ring_points_from_polygon(ctx, poly_view, &ring)) {
                is_valid = validate_ring_geometry(ctx, original_polygon, original_boundary, &ring);
            }

            if (is_valid) {
                score = abs(ring.n - opts->target_points);
                in_range = (ring.n >= opts->min_points && ring.n <= opts->max_points);
                if (in_range) score -= 1000;
                if (!found_any || score < best_score || (!found_in_range && in_range)) {
                    if (found_any) point_vec_free(&best_ring);
                    best_ring = ring;
                    best_score = score;
                    best_buffer = buffer_candidates[b];
                    best_tol = tol;
                    found_any = 1;
                    if (in_range) found_in_range = 1;
                    ring.points = NULL;
                    ring.n = ring.cap = 0;
                }
            }

            point_vec_free(&ring);
            GEOSGeom_destroy_r(ctx, simplified);
        }

        GEOSGeom_destroy_r(ctx, outer);
        if (found_in_range) break;
    }

    if (!found_any) return 0;

    *out_ring = best_ring;
    if (out_buffer) *out_buffer = best_buffer;
    if (out_tolerance) *out_tolerance = best_tol;
    return 1;
}

static int insert_seed_into_ring(GEOSContextHandle_t ctx,
                                 const GEOSGeometry *original_polygon,
                                 const GEOSGeometry *original_boundary,
                                 GeoPoint seed,
                                 GeoPointVec *ring) {
    GEOSGeometry *seed_geom = NULL;
    int inserted = 0;
    double best_added = 0.0;
    GeoPointVec best_candidate = {0};

    seed_geom = build_point_geometry(ctx, seed);
    if (!seed_geom) return 0;

    if (!GEOSDisjoint_r(ctx, seed_geom, original_polygon)) {
        GEOSGeom_destroy_r(ctx, seed_geom);
        return 0;
    }
    GEOSGeom_destroy_r(ctx, seed_geom);

    for (int i = 0; i < ring->n; i++) {
        if (point_dist2(ring->points[i], seed) <= 1e-10) {
            return 1;
        }
    }

    for (int i = 0; i < ring->n; i++) {
        int next = (i + 1) % ring->n;
        GeoPointVec candidate = {0};
        double added;

        if (!insert_point_after(ring, i, seed, &candidate)) continue;
        if (!validate_ring_geometry(ctx, original_polygon, original_boundary, &candidate)) {
            point_vec_free(&candidate);
            continue;
        }

        added = segment_length(ring->points[i], seed)
              + segment_length(seed, ring->points[next])
              - segment_length(ring->points[i], ring->points[next]);

        if (!inserted || added < best_added) {
            if (inserted) point_vec_free(&best_candidate);
            best_candidate = candidate;
            best_added = added;
            inserted = 1;
        } else {
            point_vec_free(&candidate);
        }
    }

    if (!inserted) return 0;

    point_vec_free(ring);
    *ring = best_candidate;
    return 1;
}

static int create_full_schema(sqlite3 *db) {
    const char *sql_schema =
        "CREATE TABLE IF NOT EXISTS locations ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  easting INT,"
        "  northing INT,"
        "  lat REAL,"
        "  lon REAL,"
        "  UNIQUE(easting, northing)"
        ");"
        "CREATE TABLE IF NOT EXISTS boats ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  start_location_id INTEGER REFERENCES locations(id),"
        "  end_location_id INTEGER REFERENCES locations(id),"
        "  capacity INTEGER,"
        "  name TEXT"
        ");"
        "CREATE TABLE IF NOT EXISTS stations ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  ext_id INTEGER,"
        "  start_location_id INTEGER,"
        "  end_location_id INTEGER,"
        "  amount INTEGER,"
        "  depth_thrown INTEGER,"
        "  depth_haul INTEGER,"
        "  comment TEXT,"
        "  FOREIGN KEY (start_location_id) REFERENCES locations(id),"
        "  FOREIGN KEY (end_location_id) REFERENCES locations(id)"
        ");"
        "CREATE TABLE IF NOT EXISTS ports ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT,"
        "  location_id INTEGER,"
        "  UNIQUE(location_id),"
        "  FOREIGN KEY (location_id) REFERENCES locations(id)"
        ");"
        "CREATE TABLE IF NOT EXISTS waypoints ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  location_id INTEGER,"
        "  granularity INTEGER NOT NULL DEFAULT 1,"
        "  FOREIGN KEY (location_id) REFERENCES locations(id)"
        ");"
        "CREATE TABLE IF NOT EXISTS survey ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  boat_id INTEGER NOT NULL,"
        "  table_type INTEGER,"
        "  table_id INTEGER NOT NULL,"
        "  segment INTEGER,"
        "  FOREIGN KEY (boat_id) REFERENCES boats(id)"
        ");"
        "CREATE TABLE IF NOT EXISTS coastline ("
        "  id INTEGER PRIMARY KEY,"
        "  lat REAL,"
        "  lon REAL"
        ");"
        "CREATE TABLE IF NOT EXISTS metadata ("
        "  key TEXT PRIMARY KEY,"
        "  value TEXT"
        ");"
        "CREATE TABLE IF NOT EXISTS distances ("
        "  id INTEGER PRIMARY KEY,"
        "  from_location_id INTEGER REFERENCES locations(id),"
        "  to_location_id INTEGER REFERENCES locations(id),"
        "  distance_nm REAL,"
        "  crosses_land INTEGER DEFAULT 0,"
        "  waypoint_path TEXT"
        ");";

    return sqlite3_exec(db, sql_schema, NULL, NULL, NULL);
}

static int reset_country_stage_tables(sqlite3 *db) {
    const char *sql =
        "DELETE FROM distances;"
        "DELETE FROM survey;"
        "DELETE FROM boats;"
        "DELETE FROM stations;"
        "DELETE FROM ports;"
        "DELETE FROM waypoints;"
        "DELETE FROM coastline;"
        "DELETE FROM locations;";
    return sqlite3_exec(db, sql, NULL, NULL, NULL);
}

static int store_waypoints(sqlite3 *db, const GeoPointVec *ring, int granularity) {
    sqlite3_stmt *loc_insert_stmt = NULL;
    sqlite3_stmt *loc_select_stmt = NULL;
    sqlite3_stmt *way_stmt = NULL;
    int rc = sqlite3_prepare_v2(db,
                                "INSERT INTO locations (easting, northing, lat, lon) VALUES (?, ?, ?, ?);",
                                -1, &loc_insert_stmt, NULL);
    if (rc != SQLITE_OK) return rc;

    rc = sqlite3_prepare_v2(db,
                            "SELECT id FROM locations WHERE easting = ? AND northing = ?;",
                            -1, &loc_select_stmt, NULL);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(loc_insert_stmt);
        return rc;
    }

    rc = sqlite3_prepare_v2(db,
                            "INSERT INTO waypoints (location_id, granularity) "
                            "SELECT ?, ? "
                            "WHERE NOT EXISTS (SELECT 1 FROM waypoints WHERE location_id = ?);",
                            -1, &way_stmt, NULL);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(loc_insert_stmt);
        sqlite3_finalize(loc_select_stmt);
        return rc;
    }

    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
    for (int i = 0; i < ring->n; i++) {
        int easting = decimal_deg_to_degmin_int(ring->points[i].lat);
        int northing = decimal_lon_to_degmin_storage(ring->points[i].lon);
        int loc_id = insert_location_from_degmin(loc_insert_stmt, loc_select_stmt, easting, northing);
        if (loc_id <= 0) {
            sqlite3_finalize(loc_insert_stmt);
            sqlite3_finalize(loc_select_stmt);
            sqlite3_finalize(way_stmt);
            sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
            return SQLITE_ERROR;
        }

        sqlite3_bind_int(way_stmt, 1, loc_id);
        sqlite3_bind_int(way_stmt, 2, granularity);
        sqlite3_bind_int(way_stmt, 3, loc_id);
        if (sqlite3_step(way_stmt) != SQLITE_DONE) {
            sqlite3_finalize(loc_insert_stmt);
            sqlite3_finalize(loc_select_stmt);
            sqlite3_finalize(way_stmt);
            sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
            return SQLITE_ERROR;
        }
        sqlite3_reset(way_stmt);
        sqlite3_clear_bindings(way_stmt);
    }

    sqlite3_finalize(loc_insert_stmt);
    sqlite3_finalize(loc_select_stmt);
    sqlite3_finalize(way_stmt);
    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
    return SQLITE_OK;
}

static void write_metadata(sqlite3 *db,
                           const WaypointGenerationOptions *opts,
                           const char *coastline_file_path,
                           const char *dat_path,
                           const char *port_file,
                           const char *boat_file,
                           int loaded_seed_count,
                           int inserted_seed_count,
                           int stored_seed_count,
                           int ports_seen,
                           int ports_inserted,
                           int boats_seen,
                           int boats_inserted,
                           int port_access_waypoints_added,
                           double buffer_distance,
                           double simplify_tolerance,
                           int medium_point_count,
                           int fine_point_count) {
    sqlite3_stmt *stmt = NULL;
    const char *sql = "INSERT OR REPLACE INTO metadata(key, value) VALUES(?, ?);";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return;

    char value[256];
    struct {
        const char *key;
        const char *value;
    } pairs[] = {
        {"country_stage", "complete"},
        {"country_coastline_file", coastline_file_path ? coastline_file_path : ""},
        {"country_dat_file", dat_path ? dat_path : ""},
        {"country_port_file", port_file ? port_file : ""},
        {"country_boat_file", boat_file ? boat_file : ""},
        {"country_seed_mode", opts->seed_mode == GSP_SEED_MODE_PRESERVE_ALL ? "preserve_all" : (opts->seed_mode == GSP_SEED_MODE_HINTS_ONLY ? "hints_only" : "none")}
    };

    for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
        sqlite3_bind_text(stmt, 1, pairs[i].key, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, pairs[i].value, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
    }

    snprintf(value, sizeof(value), "%d", opts->min_points);
    sqlite3_bind_text(stmt, 1, "country_min_points", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt); sqlite3_reset(stmt); sqlite3_clear_bindings(stmt);

    snprintf(value, sizeof(value), "%d", opts->max_points);
    sqlite3_bind_text(stmt, 1, "country_max_points", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt); sqlite3_reset(stmt); sqlite3_clear_bindings(stmt);

    snprintf(value, sizeof(value), "%d", opts->target_points);
    sqlite3_bind_text(stmt, 1, "country_target_points", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt); sqlite3_reset(stmt); sqlite3_clear_bindings(stmt);

    snprintf(value, sizeof(value), "%d", loaded_seed_count);
    sqlite3_bind_text(stmt, 1, "country_loaded_seed_count", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt); sqlite3_reset(stmt); sqlite3_clear_bindings(stmt);

    snprintf(value, sizeof(value), "%d", inserted_seed_count);
    sqlite3_bind_text(stmt, 1, "country_inserted_seed_count", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt); sqlite3_reset(stmt); sqlite3_clear_bindings(stmt);

    snprintf(value, sizeof(value), "%d", stored_seed_count);
    sqlite3_bind_text(stmt, 1, "country_stored_seed_count", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt); sqlite3_reset(stmt); sqlite3_clear_bindings(stmt);

    snprintf(value, sizeof(value), "%d", ports_seen);
    sqlite3_bind_text(stmt, 1, "country_ports_seen", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt); sqlite3_reset(stmt); sqlite3_clear_bindings(stmt);

    snprintf(value, sizeof(value), "%d", ports_inserted);
    sqlite3_bind_text(stmt, 1, "country_ports_inserted", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt); sqlite3_reset(stmt); sqlite3_clear_bindings(stmt);

    snprintf(value, sizeof(value), "%d", boats_seen);
    sqlite3_bind_text(stmt, 1, "country_boats_seen", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt); sqlite3_reset(stmt); sqlite3_clear_bindings(stmt);

    snprintf(value, sizeof(value), "%d", boats_inserted);
    sqlite3_bind_text(stmt, 1, "country_boats_inserted", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt); sqlite3_reset(stmt); sqlite3_clear_bindings(stmt);

    snprintf(value, sizeof(value), "%d", port_access_waypoints_added);
    sqlite3_bind_text(stmt, 1, "country_port_access_waypoints_added", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt); sqlite3_reset(stmt); sqlite3_clear_bindings(stmt);

    snprintf(value, sizeof(value), "%.8f", buffer_distance);
    sqlite3_bind_text(stmt, 1, "country_buffer_distance_deg", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt); sqlite3_reset(stmt); sqlite3_clear_bindings(stmt);

    snprintf(value, sizeof(value), "%.8f", simplify_tolerance);
    sqlite3_bind_text(stmt, 1, "country_simplify_tolerance_deg", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt); sqlite3_reset(stmt); sqlite3_clear_bindings(stmt);

    snprintf(value, sizeof(value), "%d", medium_point_count);
    sqlite3_bind_text(stmt, 1, "country_final_waypoint_count", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt); sqlite3_reset(stmt); sqlite3_clear_bindings(stmt);

    snprintf(value, sizeof(value), "%d", fine_point_count);
    sqlite3_bind_text(stmt, 1, "country_fine_waypoint_count", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt); sqlite3_reset(stmt); sqlite3_clear_bindings(stmt);

    sqlite3_finalize(stmt);
}

int country_bootstrap_run(int argc, char **argv) {
    const char *db_path = NULL;
    const char *coastline_file_path = NULL;
    const char *waypoint_file = NULL;
    const char *port_file = NULL;
    const char *boat_file = NULL;
    WaypointGenerationOptions opts;          /* medium ring */
    WaypointGenerationOptions small_opts;    /* small/coarse ring */
    CoastlinePoints coastline = {0};
    GeoPointVec seeds = {0};
    GeoPointVec medium_ring = {0};
    GeoPointVec small_ring  = {0};
    GeoPointVec fine_candidates = {0};
    int loaded_seed_count = 0;
    int inserted_seed_count = 0;
    int stored_seed_count = 0;
    int ports_seen = 0;
    int ports_inserted = 0;
    int boats_seen = 0;
    int boats_inserted = 0;
    int port_access_added = 0;
    int rc = 1;
    sqlite3 *db = NULL;
    GEOSContextHandle_t geos_ctx = NULL;
    GEOSGeometry *original_polygon = NULL;
    GEOSGeometry *original_boundary = NULL;
    double buffer_distance = 0.0;
    double simplify_tolerance = 0.0;

    /* Medium ring defaults */
    opts.min_points = 40;
    opts.max_points = 200;
    opts.target_points = 50;
    opts.use_dat_waypoints = 0;
    opts.seed_mode = GSP_SEED_MODE_NONE;

    /* Small ring defaults */
    int small_target = 12;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--db") == 0 && i + 1 < argc) {
            db_path = argv[++i];
        } else if (strcmp(argv[i], "--coastline-file") == 0 && i + 1 < argc) {
            coastline_file_path = argv[++i];
        } else if (strcmp(argv[i], "--waypoint-file") == 0 && i + 1 < argc) {
            waypoint_file = argv[++i];
        } else if (strcmp(argv[i], "--dat") == 0 && i + 1 < argc) {
            waypoint_file = argv[++i];
        } else if (strcmp(argv[i], "--port-file") == 0 && i + 1 < argc) {
            port_file = argv[++i];
        } else if ((strcmp(argv[i], "--boat-file") == 0 || strcmp(argv[i], "--boatfile") == 0) && i + 1 < argc) {
            boat_file = argv[++i];
        } else if (strcmp(argv[i], "--preserve-all-seeds") == 0) {
            opts.seed_mode = GSP_SEED_MODE_PRESERVE_ALL;
        } else if (strcmp(argv[i], "--seed-hints-only") == 0) {
            opts.seed_mode = GSP_SEED_MODE_HINTS_ONLY;
        } else if (strcmp(argv[i], "--min-points") == 0 && i + 1 < argc) {
            opts.min_points = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--max-points") == 0 && i + 1 < argc) {
            opts.max_points = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--target-points") == 0 && i + 1 < argc) {
            opts.target_points = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--small-points") == 0 && i + 1 < argc) {
            small_target = atoi(argv[++i]);
        } else {
            die_usage(argv[0]);
            return 1;
        }
    }

    if (!db_path || !coastline_file_path) {
        die_usage(argv[0]);
        return 1;
    }
    if (opts.min_points < 3 || opts.max_points < opts.min_points) {
        fprintf(stderr, "Invalid point range: min=%d max=%d\n", opts.min_points, opts.max_points);
        return 1;
    }
    if (opts.target_points < opts.min_points || opts.target_points > opts.max_points) {
        opts.target_points = (opts.min_points + opts.max_points) / 2;
    }

    /* Small ring: bracket tightly around the target, no seeds */
    small_opts.min_points    = (int)(small_target * 0.6);
    small_opts.max_points    = (int)(small_target * 1.8) + 4;
    small_opts.target_points = small_target;
    small_opts.use_dat_waypoints = 0;
    small_opts.seed_mode     = GSP_SEED_MODE_NONE;

    opts.use_dat_waypoints = (waypoint_file != NULL) ? 1 : 0;

    if (opts.seed_mode != GSP_SEED_MODE_NONE && !opts.use_dat_waypoints) {
        fprintf(stderr, "Seed mode flags require --waypoint-file <datafile.dat>\n");
        return 1;
    }

    printf("=== GSP Country Bootstrap ===\n");
    printf("Database: %s\n", db_path);
    printf("Coastline source: %s\n", coastline_file_path);
    printf("Small ring target: %d  |  Medium ring: [%d, %d] preferred=%d\n",
           small_target, opts.min_points, opts.max_points, opts.target_points);

    if (!load_repaired_coastline(coastline_file_path, &coastline)) {
        fprintf(stderr, "Failed to load repaired coastline from %s\n", coastline_file_path);
        goto cleanup;
    }
    printf("Loaded repaired coastline with %d points\n", coastline.n);

    if (waypoint_file) {
        if (!read_waypoint_seeds_from_dat(waypoint_file, &seeds)) {
            fprintf(stderr, "Failed to read WAYP seeds from %s\n", waypoint_file);
            goto cleanup;
        }
        loaded_seed_count = seeds.n;
        printf("Loaded %d manual WAYP seeds from %s\n", loaded_seed_count, waypoint_file);
    }

    geos_ctx = GEOS_init_r();
    if (!geos_ctx) {
        fprintf(stderr, "Failed to initialize GEOS\n");
        goto cleanup;
    }

    {
        GeoPointVec coast_ring;
        point_vec_init(&coast_ring);
        for (int i = 0; i < coastline.n; i++) {
            if (!point_vec_push(&coast_ring, coastline.lat[i], coastline.lon[i])) {
                point_vec_free(&coast_ring);
                fprintf(stderr, "Out of memory while preparing coastline ring\n");
                goto cleanup;
            }
        }
        original_polygon = build_polygon_from_points(geos_ctx, &coast_ring);
        point_vec_free(&coast_ring);
    }

    if (!original_polygon || !GEOSisValid_r(geos_ctx, original_polygon)) {
        fprintf(stderr, "Failed to construct valid repaired coastline polygon\n");
        goto cleanup;
    }
    original_boundary = GEOSBoundary_r(geos_ctx, original_polygon);
    if (!original_boundary) {
        fprintf(stderr, "Failed to extract coastline boundary\n");
        goto cleanup;
    }

    /* --- Generate medium ring (primary routing ring, with seed support) --- */
    if (!choose_auto_ring(geos_ctx, original_polygon, original_boundary, &coastline, &opts,
                          &medium_ring, &buffer_distance, &simplify_tolerance)) {
        fprintf(stderr, "Failed to generate medium coastline envelope\n");
        goto cleanup;
    }
    printf("Medium ring: %d points (buffer=%.5f deg, simplify=%.5f deg)\n",
           medium_ring.n, buffer_distance, simplify_tolerance);

    /* Apply manual seeds to medium ring */
    if (seeds.n > 0) {
        for (int i = 0; i < seeds.n; i++) {
            if (opts.seed_mode == GSP_SEED_MODE_HINTS_ONLY && medium_ring.n >= opts.max_points)
                continue;
            if (insert_seed_into_ring(geos_ctx, original_polygon, original_boundary,
                                      seeds.points[i], &medium_ring)) {
                inserted_seed_count++;
            } else if (opts.seed_mode == GSP_SEED_MODE_PRESERVE_ALL) {
                fprintf(stderr,
                        "Failed to preserve manual seed at lat=%.6f lon=%.6f\n",
                        seeds.points[i].lat, seeds.points[i].lon);
                goto cleanup;
            }
        }
    }

    if (!validate_ring_geometry(geos_ctx, original_polygon, original_boundary, &medium_ring)) {
        fprintf(stderr, "Medium waypoint ring failed validation\n");
        goto cleanup;
    }

    /* --- Generate small/coarse ring (no seeds) --- */
    if (!choose_auto_ring(geos_ctx, original_polygon, original_boundary, &coastline, &small_opts,
                          &small_ring, NULL, NULL)) {
        fprintf(stderr, "Warning: failed to generate small ring – skipping\n");
        /* non-fatal: small ring is optional */
    } else {
        printf("Small ring:  %d points\n", small_ring.n);
    }

    /* --- Build fine port-access candidates from actual coastline + tiny buffer --- */
    if (!build_fine_candidates_from_coastline(geos_ctx, original_polygon, &fine_candidates)) {
        fprintf(stderr, "Warning: failed to build fine candidates – port augmentation skipped\n");
    }

    /* --- Open DB --- */
    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        goto cleanup;
    }
    if (create_full_schema(db) != SQLITE_OK) {
        fprintf(stderr, "Failed to create schema: %s\n", sqlite3_errmsg(db));
        goto cleanup;
    }
    if (reset_country_stage_tables(db) != SQLITE_OK) {
        fprintf(stderr, "Failed to reset database tables: %s\n", sqlite3_errmsg(db));
        goto cleanup;
    }
    if (replace_coastline_in_db(db, &coastline) != SQLITE_OK) {
        fprintf(stderr, "Failed to store coastline in database: %s\n", sqlite3_errmsg(db));
        goto cleanup;
    }

    /* --- Import ports --- */
    if (port_file) {
        if (store_ports_from_dat(db, port_file, &ports_seen, &ports_inserted) != SQLITE_OK) {
            fprintf(stderr, "Failed to import ports from %s: %s\n", port_file, sqlite3_errmsg(db));
            goto cleanup;
        }
        if (ports_seen == 0) {
            fprintf(stderr, "No PORT rows found in port file: %s\n", port_file);
            goto cleanup;
        }
    }

    /* --- Import boats --- */
    if (boat_file) {
        if (store_boats_from_dat(db, boat_file, &boats_seen, &boats_inserted) != SQLITE_OK) {
            fprintf(stderr, "Failed to import boats from %s: %s\n", boat_file, sqlite3_errmsg(db));
            goto cleanup;
        }
        if (boats_seen == 0) {
            fprintf(stderr, "No BOAT rows found in boat file: %s\n", boat_file);
            goto cleanup;
        }
    }

    /* --- Augment medium ring for port access (non-fatal per port) --- */
    if ((ports_seen > 0 || boats_seen > 0) && fine_candidates.n > 0) {
        augment_ring_for_port_access(db, geos_ctx, original_polygon, original_boundary,
                                     &fine_candidates, &medium_ring, &port_access_added);
    }

    /* --- Store all waypoint sets used by the routing graph --- */
    if (small_ring.n > 0) {
        if (store_waypoints(db, &small_ring, GSP_WAYPOINT_GRANULARITY_COARSE) != SQLITE_OK) {
            fprintf(stderr, "Failed to store small waypoints: %s\n", sqlite3_errmsg(db));
            goto cleanup;
        }
    }
    if (store_waypoints(db, &medium_ring, GSP_WAYPOINT_GRANULARITY_ROUTING) != SQLITE_OK) {
        fprintf(stderr, "Failed to store medium waypoints: %s\n", sqlite3_errmsg(db));
        goto cleanup;
    }
    if (seeds.n > 0) {
        if (store_waypoints(db, &seeds, GSP_WAYPOINT_GRANULARITY_BUFFERED) != SQLITE_OK) {
            fprintf(stderr, "Failed to store manual WAYP points: %s\n", sqlite3_errmsg(db));
            goto cleanup;
        }
        stored_seed_count = seeds.n;
    }
    if (fine_candidates.n > 0) {
        if (store_waypoints(db, &fine_candidates, GSP_WAYPOINT_GRANULARITY_BUFFERED) != SQLITE_OK) {
            fprintf(stderr, "Failed to store fine waypoints: %s\n", sqlite3_errmsg(db));
            goto cleanup;
        }
    }

    write_metadata(db, &opts, coastline_file_path, waypoint_file, port_file, boat_file,
                   loaded_seed_count, inserted_seed_count, stored_seed_count,
                   ports_seen, ports_inserted,
                   boats_seen, boats_inserted,
                   port_access_added, buffer_distance, simplify_tolerance, medium_ring.n, fine_candidates.n);

    printf("Stored waypoints in %s\n", db_path);
    printf("  coarse coastline approximation: %d points\n", small_ring.n);
    printf("  routing coastline ring:         %d points\n", medium_ring.n);
    printf("  buffered coastline support:     %d points\n", fine_candidates.n);
    if (waypoint_file) {
        printf("  manual WAYP: loaded=%d stored=%d inserted_into_ring=%d mode=%s\n",
               loaded_seed_count, stored_seed_count, inserted_seed_count,
               opts.seed_mode == GSP_SEED_MODE_PRESERVE_ALL ? "preserve_all" :
               (opts.seed_mode == GSP_SEED_MODE_HINTS_ONLY  ? "hints_only"   : "none"));
    }
    if (port_file) {
        printf("  ports: seen=%d inserted=%d  access_augmented=%d\n",
               ports_seen, ports_inserted, port_access_added);
    }
    if (boat_file) {
        printf("  boats: seen=%d inserted=%d\n", boats_seen, boats_inserted);
    }


    rc = 0;

cleanup:
    if (db) sqlite3_close(db);
    if (original_boundary) GEOSGeom_destroy_r(geos_ctx, original_boundary);
    if (original_polygon)  GEOSGeom_destroy_r(geos_ctx, original_polygon);
    if (geos_ctx) GEOS_finish_r(geos_ctx);
    point_vec_free(&seeds);
    point_vec_free(&medium_ring);
    point_vec_free(&small_ring);
    point_vec_free(&fine_candidates);
    free_coastline_points(&coastline);
    return rc;
}

#ifndef GSP_LIBRARY_ONLY
int main(int argc, char **argv) {
    return country_bootstrap_run(argc, argv);
}
#endif
