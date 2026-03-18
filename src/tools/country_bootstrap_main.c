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

static void die_usage(const char *argv0) {
    fprintf(stderr,
            "Usage: %s --db <gsp_data.db> --coastline-file <island.tsv> [options]\n"
            "\n"
            "Options:\n"
            "  --waypoint-file <datafile.dat> Optional DAT file for manual WAYP seeds\n"
            "  --dat <datafile.dat>        Deprecated alias for --waypoint-file\n"
            "  --port-file <datafile.dat>  DAT file used to import PORT rows into DB\n"
            "  --boat-file <datafile.dat>  DAT file used to import BOAT rows into DB\n"
            "  --skip-waypoints            Only load coastline, ports, and boats\n"
            "  --waypoints-only            Only rebuild waypoint rows on top of existing country/station data\n"
            "Current coastline-derived waypoint sets are controlled by constants in constants.h:\n"
            "  coarse coastline ring\n"
            "  buffered coastline support\n",
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
        if (dst->n >= target_max) break;
    }
    if (dst->n < target_max &&
        (dst->n == 0 || point_dist2(dst->points[dst->n - 1], src->points[src->n - 1]) > 1e-12)) {
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

static GEOSGeometry *repair_polygon_geometry(GEOSContextHandle_t ctx,
                                             GEOSGeometry *geom,
                                             const char *label) {
    if (!geom) return NULL;
    if (GEOSisValid_r(ctx, geom)) return geom;

    GEOSGeometry *repaired = GEOSBuffer_r(ctx, geom, 0.0, 8);
    if (!repaired || !GEOSisValid_r(ctx, repaired)) {
        if (repaired) GEOSGeom_destroy_r(ctx, repaired);
        GEOSGeom_destroy_r(ctx, geom);
        return NULL;
    }

    printf("  WARN %s invalid after geometry operation -> repaired with buffer(0)\n", label);
    GEOSGeom_destroy_r(ctx, geom);
    return repaired;
}

static int validate_offset_points_outside_coastline(GEOSContextHandle_t ctx,
                                                    const GEOSGeometry *coastline_polygon,
                                                    const GeoPointVec *ring) {
    if (!ring || ring->n <= 0) return 0;
    for (int i = 0; i < ring->n; i++) {
        GEOSCoordSequence *seq = GEOSCoordSeq_create_r(ctx, 1, 2);
        GEOSGeometry *pt = NULL;
        if (!seq) return 0;
        GEOSCoordSeq_setX_r(ctx, seq, 0, ring->points[i].lon);
        GEOSCoordSeq_setY_r(ctx, seq, 0, ring->points[i].lat);
        pt = GEOSGeom_createPoint_r(ctx, seq);
        if (!pt) return 0;
        if (!GEOSDisjoint_r(ctx, pt, coastline_polygon)) {
            GEOSGeom_destroy_r(ctx, pt);
            return 0;
        }
        GEOSGeom_destroy_r(ctx, pt);
    }
    return 1;
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

/*
 * Build fine port-access candidates by buffering the raw coastline polygon by
 * a tiny amount (~0.005 deg ≈ 500 m).  All ring points are kept – no subsampling –
 * so even narrow fjords get good coverage.
 */
static int build_offset_ring_from_coastline(GEOSContextHandle_t ctx,
                                            const GEOSGeometry *coastline_polygon,
                                            const char *label,
                                            double offset_deg,
                                            double simplify_tolerance_deg,
                                            int target_max_points,
                                            GeoPointVec *out) {
    /* 32 segments gives a smooth curve that hugs the coast closely */
    GEOSGeometry *buffered = GEOSBuffer_r(ctx, coastline_polygon, offset_deg, 32);
    GEOSGeometry *simplified = NULL;
    GeoPointVec repaired_ring = {0};
    GeoPointVec simplified_ring = {0};
    if (!buffered) return 0;

    buffered = repair_polygon_geometry(ctx, buffered, label);
    if (!buffered) return 0;

    if (!extract_ring_points_from_polygon(ctx, buffered, &repaired_ring)) {
        GEOSGeom_destroy_r(ctx, buffered);
        return 0;
    }
    printf("  %s ring after repair: %d points\n", label, repaired_ring.n);

    simplified = GEOSTopologyPreserveSimplify_r(ctx, buffered, simplify_tolerance_deg);
    GEOSGeom_destroy_r(ctx, buffered);
    if (!simplified) {
        point_vec_free(&repaired_ring);
        return 0;
    }
    simplified = repair_polygon_geometry(ctx, simplified, label);
    if (!simplified) {
        point_vec_free(&repaired_ring);
        return 0;
    }
    if (!extract_ring_points_from_polygon(ctx, simplified, &simplified_ring)) {
        GEOSGeom_destroy_r(ctx, simplified);
        point_vec_free(&repaired_ring);
        return 0;
    }
    printf("  %s ring after simplify: %d points\n", label, simplified_ring.n);
    GEOSGeom_destroy_r(ctx, simplified);

    if (!downsample_points_stride(&simplified_ring, target_max_points, out)) {
        point_vec_free(&repaired_ring);
        point_vec_free(&simplified_ring);
        return 0;
    }
    if (!validate_offset_points_outside_coastline(ctx, coastline_polygon, out)) {
        point_vec_free(out);
        point_vec_free(&repaired_ring);
        point_vec_free(&simplified_ring);
        return 0;
    }
    point_vec_free(&repaired_ring);
    point_vec_free(&simplified_ring);
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

static int reset_waypoint_stage_tables(sqlite3 *db) {
    const char *sql =
        "DELETE FROM distances;"
        "DELETE FROM waypoints;"
        "DELETE FROM locations "
        "WHERE id NOT IN (SELECT start_location_id FROM stations) "
        "  AND id NOT IN (SELECT end_location_id FROM stations) "
        "  AND id NOT IN (SELECT start_location_id FROM boats) "
        "  AND id NOT IN (SELECT end_location_id FROM boats) "
        "  AND id NOT IN (SELECT location_id FROM ports);";
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
                           const char *coastline_file_path,
                           const char *waypoint_file_path,
                           const char *port_file,
                           const char *boat_file,
                           int loaded_manual_waypoint_count,
                           int stored_manual_waypoint_count,
                           int ports_loaded_count,
                           int ports_stored_count,
                           int boats_loaded_count,
                           int boats_stored_count,
                           int coarse_point_count,
                           int buffered_point_count) {
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
        {"country_waypoint_file", waypoint_file_path ? waypoint_file_path : ""},
        {"country_port_file", port_file ? port_file : ""},
        {"country_boat_file", boat_file ? boat_file : ""}
    };

    for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
        sqlite3_bind_text(stmt, 1, pairs[i].key, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, pairs[i].value, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
    }

    snprintf(value, sizeof(value), "%d", loaded_manual_waypoint_count);
    sqlite3_bind_text(stmt, 1, "country_manual_waypoints_loaded", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt); sqlite3_reset(stmt); sqlite3_clear_bindings(stmt);

    snprintf(value, sizeof(value), "%d", stored_manual_waypoint_count);
    sqlite3_bind_text(stmt, 1, "country_manual_waypoints_stored", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt); sqlite3_reset(stmt); sqlite3_clear_bindings(stmt);

    snprintf(value, sizeof(value), "%d", ports_loaded_count);
    sqlite3_bind_text(stmt, 1, "country_ports_loaded", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt); sqlite3_reset(stmt); sqlite3_clear_bindings(stmt);

    snprintf(value, sizeof(value), "%d", ports_stored_count);
    sqlite3_bind_text(stmt, 1, "country_ports_stored", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt); sqlite3_reset(stmt); sqlite3_clear_bindings(stmt);

    snprintf(value, sizeof(value), "%d", boats_loaded_count);
    sqlite3_bind_text(stmt, 1, "country_boats_loaded", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt); sqlite3_reset(stmt); sqlite3_clear_bindings(stmt);

    snprintf(value, sizeof(value), "%d", boats_stored_count);
    sqlite3_bind_text(stmt, 1, "country_boats_stored", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt); sqlite3_reset(stmt); sqlite3_clear_bindings(stmt);

    snprintf(value, sizeof(value), "%.8f", COARSE_COASTLINE_OFFSET_DEG);
    sqlite3_bind_text(stmt, 1, "country_coarse_offset_deg", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt); sqlite3_reset(stmt); sqlite3_clear_bindings(stmt);

    snprintf(value, sizeof(value), "%.8f", BUFFERED_COASTLINE_OFFSET_DEG);
    sqlite3_bind_text(stmt, 1, "country_buffered_offset_deg", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt); sqlite3_reset(stmt); sqlite3_clear_bindings(stmt);

    snprintf(value, sizeof(value), "%d", coarse_point_count);
    sqlite3_bind_text(stmt, 1, "country_coarse_waypoint_count", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt); sqlite3_reset(stmt); sqlite3_clear_bindings(stmt);

    snprintf(value, sizeof(value), "%d", buffered_point_count);
    sqlite3_bind_text(stmt, 1, "country_buffered_waypoint_count", -1, SQLITE_STATIC);
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
    int skip_waypoints = 0;
    int waypoints_only = 0;
    CoastlinePoints coastline = {0};
    GeoPointVec seeds = {0};
    GeoPointVec coarse_ring = {0};
    GeoPointVec buffered_ring = {0};
    int loaded_seed_count = 0;
    int stored_seed_count = 0;
    int ports_seen = 0;
    int ports_inserted = 0;
    int boats_seen = 0;
    int boats_inserted = 0;
    int rc = 1;
    sqlite3 *db = NULL;
    GEOSContextHandle_t geos_ctx = NULL;
    GEOSGeometry *original_polygon = NULL;
    GEOSGeometry *original_boundary = NULL;

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
        } else if (strcmp(argv[i], "--skip-waypoints") == 0) {
            skip_waypoints = 1;
        } else if (strcmp(argv[i], "--waypoints-only") == 0) {
            waypoints_only = 1;
        } else {
            die_usage(argv[0]);
            return 1;
        }
    }

    if (!db_path || !coastline_file_path) {
        die_usage(argv[0]);
        return 1;
    }
    if (skip_waypoints && waypoints_only) {
        fprintf(stderr, "--skip-waypoints and --waypoints-only cannot be used together\n");
        return 1;
    }

    printf("=== GSP Country Bootstrap ===\n");
    printf("Database: %s\n", db_path);
    printf("Coastline source: %s\n", coastline_file_path);
    printf("Coarse coastline ring:  offset=%.2f nm max_points=%d\n",
           COARSE_COASTLINE_OFFSET_NM, COARSE_COASTLINE_MAX_POINTS);
    printf("Buffered coastline set: offset=%.2f nm max_points=%d\n",
           BUFFERED_COASTLINE_OFFSET_NM, BUFFERED_COASTLINE_MAX_POINTS);

    if (!load_repaired_coastline(coastline_file_path, &coastline)) {
        fprintf(stderr, "Failed to load repaired coastline from %s\n", coastline_file_path);
        goto cleanup;
    }
    printf("Loaded repaired coastline with %d points\n", coastline.n);

    if (!skip_waypoints && waypoint_file) {
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

    /* --- Generate coarse offshore ring --- */
    if (!build_offset_ring_from_coastline(geos_ctx, original_polygon,
                                          "Coarse coastline",
                                          COARSE_COASTLINE_OFFSET_DEG,
                                          COARSE_COASTLINE_SIMPLIFY_TOLERANCE_DEG,
                                          COARSE_COASTLINE_MAX_POINTS,
                                          &coarse_ring)) {
        fprintf(stderr, "Failed to generate coarse coastline ring\n");
        goto cleanup;
    }
    printf("Coarse ring: %d points\n", coarse_ring.n);

    /* --- Build buffered coastline support set --- */
    if (!build_offset_ring_from_coastline(geos_ctx, original_polygon,
                                          "Buffered coastline",
                                          BUFFERED_COASTLINE_OFFSET_DEG,
                                          BUFFERED_COASTLINE_SIMPLIFY_TOLERANCE_DEG,
                                          BUFFERED_COASTLINE_MAX_POINTS,
                                          &buffered_ring)) {
        fprintf(stderr, "Failed to build buffered coastline support set\n");
        goto cleanup;
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
    if (waypoints_only) {
        if (reset_waypoint_stage_tables(db) != SQLITE_OK) {
            fprintf(stderr, "Failed to reset waypoint tables: %s\n", sqlite3_errmsg(db));
            goto cleanup;
        }
    } else {
        if (reset_country_stage_tables(db) != SQLITE_OK) {
            fprintf(stderr, "Failed to reset database tables: %s\n", sqlite3_errmsg(db));
            goto cleanup;
        }
        if (replace_coastline_in_db(db, &coastline) != SQLITE_OK) {
            fprintf(stderr, "Failed to store coastline in database: %s\n", sqlite3_errmsg(db));
            goto cleanup;
        }
    }

    /* --- Import ports --- */
    if (!waypoints_only && port_file) {
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
    if (!waypoints_only && boat_file) {
        if (store_boats_from_dat(db, boat_file, &boats_seen, &boats_inserted) != SQLITE_OK) {
            fprintf(stderr, "Failed to import boats from %s: %s\n", boat_file, sqlite3_errmsg(db));
            goto cleanup;
        }
        if (boats_seen == 0) {
            fprintf(stderr, "No BOAT rows found in boat file: %s\n", boat_file);
            goto cleanup;
        }
    }

    /* --- Store all waypoint sets used by the routing graph --- */
    if (coarse_ring.n > 0) {
        if (store_waypoints(db, &coarse_ring, GSP_WAYPOINT_GRANULARITY_COARSE) != SQLITE_OK) {
            fprintf(stderr, "Failed to store coarse coastline ring: %s\n", sqlite3_errmsg(db));
            goto cleanup;
        }
    }

    if (skip_waypoints) {
        write_metadata(db, coastline_file_path, waypoint_file, port_file, boat_file,
                       0, 0, ports_seen, ports_inserted, boats_seen, boats_inserted, 0, 0);
        rc = 0;
        goto cleanup;
    }
    if (seeds.n > 0) {
        if (store_waypoints(db, &seeds, GSP_WAYPOINT_GRANULARITY_MANUAL) != SQLITE_OK) {
            fprintf(stderr, "Failed to store manual WAYP points: %s\n", sqlite3_errmsg(db));
            goto cleanup;
        }
        stored_seed_count = seeds.n;
    }
    if (buffered_ring.n > 0) {
        if (store_waypoints(db, &buffered_ring, GSP_WAYPOINT_GRANULARITY_BUFFERED) != SQLITE_OK) {
            fprintf(stderr, "Failed to store buffered coastline support: %s\n", sqlite3_errmsg(db));
            goto cleanup;
        }
    }

    write_metadata(db, coastline_file_path, waypoint_file, port_file, boat_file,
                   loaded_seed_count, stored_seed_count,
                   ports_seen, ports_inserted,
                   boats_seen, boats_inserted,
                   coarse_ring.n, buffered_ring.n);

    printf("Stored waypoints in %s\n", db_path);
    printf("  coarse coastline ring:          %d points\n", coarse_ring.n);
    printf("  buffered coastline support:     %d points\n", buffered_ring.n);
    if (waypoint_file) {
        printf("  manual WAYP: loaded=%d stored=%d\n",
               loaded_seed_count, stored_seed_count);
    }
    if (port_file) {
        printf("  ports: loaded=%d stored=%d\n", ports_seen, ports_inserted);
    }
    if (boat_file) {
        printf("  boats: loaded=%d stored=%d\n", boats_seen, boats_inserted);
    }


    rc = 0;

cleanup:
    if (db) sqlite3_close(db);
    if (original_boundary) GEOSGeom_destroy_r(geos_ctx, original_boundary);
    if (original_polygon)  GEOSGeom_destroy_r(geos_ctx, original_polygon);
    if (geos_ctx) GEOS_finish_r(geos_ctx);
    point_vec_free(&seeds);
    point_vec_free(&coarse_ring);
    point_vec_free(&buffered_ring);
    free_coastline_points(&coastline);
    return rc;
}

#ifndef GSP_LIBRARY_ONLY
int main(int argc, char **argv) {
    return country_bootstrap_run(argc, argv);
}
#endif
