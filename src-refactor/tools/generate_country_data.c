#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sqlite3.h>
#include <geos_c.h>

#include "../include/dat_parser.h"
#include "../include/geo_utils.h"
#include "../include/coastline_db.h"

typedef struct {
    double lat;
    double lon;
} GeoPoint;

typedef struct {
    GeoPoint *points;
    int n;
    int cap;
} GeoPointVec;

static void die_usage(const char *argv0) {
    fprintf(stderr,
            "Usage: %s --db <gsp_data.db> --island-bin <island.bin> [options]\n"
            "\n"
            "Options:\n"
            "  --dat <datafile.dat>        Optional DAT file for manual WAYP seeds\n"
            "  --use-dat-waypoints        Load WAYP lines from --dat\n"
            "  --preserve-all-seeds       Force all loaded seed waypoints into final ring\n"
            "  --seed-hints-only          Use loaded seed waypoints only when insertion stays valid\n"
            "  --min-points <N>           Minimum target waypoint count (default: 30)\n"
            "  --max-points <N>           Maximum target waypoint count (default: 40)\n"
            "  --target-points <N>        Preferred waypoint count inside range (default: midpoint)\n",
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
    ItemVec items;
    item_vec_init(&items);
    point_vec_init(out);

    read_dat_file_all_boats(dat_path, &items, 0);

    double tol2 = 1e-10;
    for (int i = 0; i < items.n; i++) {
        if (items.a[i].Type != tWAYP) continue;
        double lat = degmin_to_deg((int)items.a[i].LatLonDegMin[0]);
        double lon = degmin_to_deg_lon((int)items.a[i].LatLonDegMin[1]);
        if (!append_unique_point(out, lat, lon, tol2)) {
            item_vec_free(&items);
            point_vec_free(out);
            return 0;
        }
    }

    item_vec_free(&items);
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
        "  c1 INTEGER,"
        "  c2 INTEGER,"
        "  c3 INTEGER,"
        "  c4 INTEGER,"
        "  c5 INTEGER,"
        "  c6 INTEGER,"
        "  name TEXT"
        ");"
        "CREATE TABLE IF NOT EXISTS stations ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  ext_id INTEGER,"
        "  start_location_id INTEGER,"
        "  end_location_id INTEGER,"
        "  c1 INTEGER,"
        "  c2 INTEGER,"
        "  c3 INTEGER,"
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

static int store_waypoints(sqlite3 *db, const GeoPointVec *ring) {
    sqlite3_stmt *loc_stmt = NULL;
    sqlite3_stmt *way_stmt = NULL;
    int rc = sqlite3_prepare_v2(db,
                                "INSERT INTO locations (easting, northing, lat, lon) VALUES (NULL, NULL, ?, ?);",
                                -1, &loc_stmt, NULL);
    if (rc != SQLITE_OK) return rc;

    rc = sqlite3_prepare_v2(db,
                            "INSERT INTO waypoints (location_id) VALUES (?);",
                            -1, &way_stmt, NULL);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(loc_stmt);
        return rc;
    }

    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
    for (int i = 0; i < ring->n; i++) {
        sqlite3_bind_double(loc_stmt, 1, ring->points[i].lat);
        sqlite3_bind_double(loc_stmt, 2, ring->points[i].lon);
        if (sqlite3_step(loc_stmt) != SQLITE_DONE) {
            sqlite3_finalize(loc_stmt);
            sqlite3_finalize(way_stmt);
            sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
            return SQLITE_ERROR;
        }
        sqlite3_reset(loc_stmt);
        sqlite3_clear_bindings(loc_stmt);

        sqlite3_bind_int(way_stmt, 1, (int)sqlite3_last_insert_rowid(db));
        if (sqlite3_step(way_stmt) != SQLITE_DONE) {
            sqlite3_finalize(loc_stmt);
            sqlite3_finalize(way_stmt);
            sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
            return SQLITE_ERROR;
        }
        sqlite3_reset(way_stmt);
        sqlite3_clear_bindings(way_stmt);
    }

    sqlite3_finalize(loc_stmt);
    sqlite3_finalize(way_stmt);
    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
    return SQLITE_OK;
}

static void write_metadata(sqlite3 *db,
                           const WaypointGenerationOptions *opts,
                           const char *island_bin_path,
                           const char *dat_path,
                           int loaded_seed_count,
                           int inserted_seed_count,
                           double buffer_distance,
                           double simplify_tolerance,
                           int final_point_count) {
    sqlite3_stmt *stmt = NULL;
    const char *sql = "INSERT OR REPLACE INTO metadata(key, value) VALUES(?, ?);";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return;

    char value[256];
    struct {
        const char *key;
        const char *value;
    } pairs[] = {
        {"country_stage", "complete"},
        {"country_island_bin", island_bin_path ? island_bin_path : ""},
        {"country_dat_file", dat_path ? dat_path : ""},
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

    snprintf(value, sizeof(value), "%.8f", buffer_distance);
    sqlite3_bind_text(stmt, 1, "country_buffer_distance_deg", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt); sqlite3_reset(stmt); sqlite3_clear_bindings(stmt);

    snprintf(value, sizeof(value), "%.8f", simplify_tolerance);
    sqlite3_bind_text(stmt, 1, "country_simplify_tolerance_deg", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt); sqlite3_reset(stmt); sqlite3_clear_bindings(stmt);

    snprintf(value, sizeof(value), "%d", final_point_count);
    sqlite3_bind_text(stmt, 1, "country_final_waypoint_count", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt); sqlite3_reset(stmt); sqlite3_clear_bindings(stmt);

    sqlite3_finalize(stmt);
}

int main(int argc, char **argv) {
    const char *db_path = NULL;
    const char *island_bin_path = NULL;
    const char *dat_path = NULL;
    WaypointGenerationOptions opts;
    CoastlinePoints coastline = {0};
    GeoPointVec seeds = {0};
    GeoPointVec final_ring = {0};
    int loaded_seed_count = 0;
    int inserted_seed_count = 0;
    int rc = 1;
    sqlite3 *db = NULL;
    GEOSContextHandle_t geos_ctx = NULL;
    GEOSGeometry *original_polygon = NULL;
    GEOSGeometry *original_boundary = NULL;
    double buffer_distance = 0.0;
    double simplify_tolerance = 0.0;

    opts.min_points = 30;
    opts.max_points = 40;
    opts.target_points = 35;
    opts.use_dat_waypoints = 0;
    opts.seed_mode = GSP_SEED_MODE_NONE;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--db") == 0 && i + 1 < argc) {
            db_path = argv[++i];
        } else if (strcmp(argv[i], "--island-bin") == 0 && i + 1 < argc) {
            island_bin_path = argv[++i];
        } else if (strcmp(argv[i], "--dat") == 0 && i + 1 < argc) {
            dat_path = argv[++i];
        } else if (strcmp(argv[i], "--use-dat-waypoints") == 0) {
            opts.use_dat_waypoints = 1;
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
        } else {
            die_usage(argv[0]);
            return 1;
        }
    }

    if (!db_path || !island_bin_path) {
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
    if (opts.seed_mode != GSP_SEED_MODE_NONE && !opts.use_dat_waypoints) {
        fprintf(stderr, "Seed mode flags require --use-dat-waypoints\n");
        return 1;
    }
    if (opts.use_dat_waypoints && !dat_path) {
        fprintf(stderr, "--use-dat-waypoints requires --dat <datafile.dat>\n");
        return 1;
    }

    printf("=== GSP Country Bootstrap ===\n");
    printf("Database: %s\n", db_path);
    printf("Coastline source: %s\n", island_bin_path);
    printf("Waypoint target range: [%d, %d], preferred=%d\n", opts.min_points, opts.max_points, opts.target_points);

    if (!load_repaired_coastline_from_bin(island_bin_path, &coastline)) {
        fprintf(stderr, "Failed to load repaired coastline from %s\n", island_bin_path);
        goto cleanup;
    }
    printf("Loaded repaired coastline with %d points\n", coastline.n);

    if (opts.use_dat_waypoints) {
        if (!read_waypoint_seeds_from_dat(dat_path, &seeds)) {
            fprintf(stderr, "Failed to read WAYP seeds from %s\n", dat_path);
            goto cleanup;
        }
        loaded_seed_count = seeds.n;
        printf("Loaded %d manual WAYP seeds from %s\n", loaded_seed_count, dat_path);
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

    if (!choose_auto_ring(geos_ctx, original_polygon, original_boundary, &coastline, &opts,
                          &final_ring, &buffer_distance, &simplify_tolerance)) {
        fprintf(stderr, "Failed to generate valid coastline envelope\n");
        goto cleanup;
    }

    printf("Auto-generated %d coastline envelope points (buffer=%.5f deg, simplify=%.5f deg)\n",
           final_ring.n, buffer_distance, simplify_tolerance);

    if (opts.use_dat_waypoints && seeds.n > 0) {
        for (int i = 0; i < seeds.n; i++) {
            if (opts.seed_mode == GSP_SEED_MODE_HINTS_ONLY && final_ring.n >= opts.max_points) {
                continue;
            }
            if (insert_seed_into_ring(geos_ctx, original_polygon, original_boundary, seeds.points[i], &final_ring)) {
                inserted_seed_count++;
            } else if (opts.seed_mode == GSP_SEED_MODE_PRESERVE_ALL) {
                fprintf(stderr,
                        "Failed to preserve manual seed at lat=%.6f lon=%.6f without violating coastline constraints\n",
                        seeds.points[i].lat, seeds.points[i].lon);
                goto cleanup;
            }
        }
    }

    if (!validate_ring_geometry(geos_ctx, original_polygon, original_boundary, &final_ring)) {
        fprintf(stderr, "Final waypoint ring failed validation\n");
        goto cleanup;
    }

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
    if (store_waypoints(db, &final_ring) != SQLITE_OK) {
        fprintf(stderr, "Failed to store waypoints in database: %s\n", sqlite3_errmsg(db));
        goto cleanup;
    }

    write_metadata(db, &opts, island_bin_path, dat_path, loaded_seed_count,
                   inserted_seed_count, buffer_distance, simplify_tolerance, final_ring.n);

    printf("Stored %d generated waypoints in %s\n", final_ring.n, db_path);
    if (opts.use_dat_waypoints) {
        printf("Seed handling: loaded=%d inserted=%d mode=%s\n",
               loaded_seed_count,
               inserted_seed_count,
               opts.seed_mode == GSP_SEED_MODE_PRESERVE_ALL ? "preserve_all" :
               (opts.seed_mode == GSP_SEED_MODE_HINTS_ONLY ? "hints_only" : "none"));
    }

    printf("\nGenerated WAYP-style coordinates (decimal degrees):\n");
    printf("  idx   latitude    longitude\n");
    printf("  ----  ----------  -----------\n");
    for (int i = 0; i < final_ring.n; i++) {
        printf("  %4d  %10.6f  %11.6f\n", i + 1, final_ring.points[i].lat, final_ring.points[i].lon);
    }

    printf("\nPlot hint:\n");
    printf("  Rscript R/plot_country_waypoints.R %s\n", db_path);

    rc = 0;

cleanup:
    if (db) sqlite3_close(db);
    if (original_boundary) GEOSGeom_destroy_r(geos_ctx, original_boundary);
    if (original_polygon) GEOSGeom_destroy_r(geos_ctx, original_polygon);
    if (geos_ctx) GEOS_finish_r(geos_ctx);
    point_vec_free(&seeds);
    point_vec_free(&final_ring);
    free_coastline_points(&coastline);
    return rc;
}


