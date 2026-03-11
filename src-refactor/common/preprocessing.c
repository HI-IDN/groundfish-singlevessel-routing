/*
 * Data Preparation Utility
 * Parse .dat file and write to SQLite database
 */

#include "../include/dat_parser.h"
#include "../include/distance.h"
#include "../include/coastline_db.h"
#include "../include/constants.h"
#include "../include/geo_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <time.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* Parse comment field to extract depth values and clean comment */
static void parse_station_comment(const char *raw_comment, int *depth_thrown, int *depth_haul, char **clean_comment) {
    *depth_thrown = 0;
    *depth_haul = 0;
    *clean_comment = NULL;

    if (!raw_comment) return;

    /* Format: "# optional text \\ botndypi_kastad= 229 botndypi_hift= 222 \\" */
    char *comment_copy = strdup(raw_comment);

    /* Find botndypi_kastad */
    char *kastad_pos = strstr(comment_copy, "botndypi_kastad=");
    if (kastad_pos) {
        *depth_thrown = atoi(kastad_pos + 16);
    }

    /* Find botndypi_hift */
    char *hift_pos = strstr(comment_copy, "botndypi_hift=");
    if (hift_pos) {
        *depth_haul = atoi(hift_pos + 14);
    }

    /* Extract clean comment (text before first \\) */
    char *backslash = strstr(comment_copy, "\\\\");
    if (backslash) {
        *backslash = '\0';  /* Terminate at first \\ */

        /* Trim whitespace from end */
        char *end = backslash - 1;
        while (end >= comment_copy && (*end == ' ' || *end == '\t')) {
            *end = '\0';
            end--;
        }

        /* Skip leading # and whitespace */
        char *start = comment_copy;
        if (*start == '#') start++;
        while (*start == ' ' || *start == '\t') start++;

        /* Copy cleaned comment if not empty */
        if (*start != '\0') {
            *clean_comment = strdup(start);
        }
    } else {
        /* No backslash, use whole comment */
        char *start = comment_copy;
        if (*start == '#') start++;
        while (*start == ' ' || *start == '\t') start++;
        if (*start != '\0') {
            *clean_comment = strdup(start);
        }
    }

    free(comment_copy);
}

/* Verify that waypoints are loaded last (runtime validation) */
static int verify_waypoints_last(const int* types, int n)
{
    int seen_waypoint = 0;
    for (int i = 0; i < n; i++) {
        if (types[i] == NODE_TYPE_WAYPOINT) {
            seen_waypoint = 1;
        } else if (seen_waypoint) {
            return 0;
        }
    }
    return 1;
}

/*
 * Build survey table from boats, stations, and ports already in database
 * This must be called AFTER inserting boats/stations/ports
 */
static int build_survey_assignments(sqlite3 *db, const ItemVec *items) {
    printf("\n=== Building Survey Assignments ===\n");
    
    /* Begin transaction for survey assignments */
    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);

    sqlite3_stmt *survey_stmt;
    sqlite3_prepare_v2(db, 
        "INSERT INTO survey (boat_id, table_type, table_id, segment) "
        "VALUES (?, ?, ?, ?);",
        -1, &survey_stmt, NULL);
    
    int survey_count = 0;
    int current_boat_id = 0;
    int segment = 0;
    
    for (int i = 0; i < items->n; i++) {
        if (items->a[i].Type == tSHIP) {
            /* Get boat_id from boats table by matching start location */
            sqlite3_stmt *boat_select;
            int start_lat = items->a[i].LatLonDegMin[0];
            int start_lon = items->a[i].LatLonDegMin[1];
            
            sqlite3_prepare_v2(db, 
                "SELECT b.id FROM boats b "
                "JOIN locations l ON b.start_location_id = l.id "
                "WHERE l.easting = ? AND l.northing = ?;",
                -1, &boat_select, NULL);
            sqlite3_bind_int(boat_select, 1, start_lat);
            sqlite3_bind_int(boat_select, 2, start_lon);
            
            if (sqlite3_step(boat_select) == SQLITE_ROW) {
                current_boat_id = sqlite3_column_int(boat_select, 0);
                segment = 1;
                
                /* Insert boat as start of segment 1 */
                sqlite3_bind_int(survey_stmt, 1, current_boat_id);
                sqlite3_bind_int(survey_stmt, 2, NODE_TYPE_BOAT);
                sqlite3_bind_int(survey_stmt, 3, current_boat_id);
                sqlite3_bind_int(survey_stmt, 4, segment);
                sqlite3_step(survey_stmt);
                sqlite3_reset(survey_stmt);
                survey_count++;
            }
            sqlite3_finalize(boat_select);
            
        } else if (items->a[i].Type == tSTAT && current_boat_id > 0) {
            /* Get station_id by matching start location */
            sqlite3_stmt *stat_select;
            int start_lat = items->a[i].LatLonDegMin[0];
            int start_lon = items->a[i].LatLonDegMin[1];
            
            sqlite3_prepare_v2(db,
                "SELECT s.id FROM stations s "
                "JOIN locations l ON s.start_location_id = l.id "
                "WHERE l.easting = ? AND l.northing = ?;",
                -1, &stat_select, NULL);
            sqlite3_bind_int(stat_select, 1, start_lat);
            sqlite3_bind_int(stat_select, 2, start_lon);
            
            if (sqlite3_step(stat_select) == SQLITE_ROW) {
                int station_id = sqlite3_column_int(stat_select, 0);
                
                sqlite3_bind_int(survey_stmt, 1, current_boat_id);
                sqlite3_bind_int(survey_stmt, 2, NODE_TYPE_STATION);
                sqlite3_bind_int(survey_stmt, 3, station_id);
                sqlite3_bind_int(survey_stmt, 4, segment);
                sqlite3_step(survey_stmt);
                sqlite3_reset(survey_stmt);
                survey_count++;
            }
            sqlite3_finalize(stat_select);
            
        } else if (items->a[i].Type == tPORT && current_boat_id > 0 && items->a[i].PortSelected) {
            /* Get port_id by matching location */
            sqlite3_stmt *port_select;
            int lat = items->a[i].LatLonDegMin[0];
            int lon = items->a[i].LatLonDegMin[1];
            
            sqlite3_prepare_v2(db,
                "SELECT p.id FROM ports p "
                "JOIN locations l ON p.location_id = l.id "
                "WHERE l.easting = ? AND l.northing = ?;",
                -1, &port_select, NULL);
            sqlite3_bind_int(port_select, 1, lat);
            sqlite3_bind_int(port_select, 2, lon);
            
            if (sqlite3_step(port_select) == SQLITE_ROW) {
                int port_id = sqlite3_column_int(port_select, 0);
                
                /* Insert PORT as end of current segment */
                sqlite3_bind_int(survey_stmt, 1, current_boat_id);
                sqlite3_bind_int(survey_stmt, 2, NODE_TYPE_PORT);
                sqlite3_bind_int(survey_stmt, 3, port_id);
                sqlite3_bind_int(survey_stmt, 4, segment);
                sqlite3_step(survey_stmt);
                sqlite3_reset(survey_stmt);
                survey_count++;
                
                /* Increment segment for next leg */
                segment++;

                /* Duplicate the same PORT as start of next segment */
                sqlite3_bind_int(survey_stmt, 1, current_boat_id);
                sqlite3_bind_int(survey_stmt, 2, NODE_TYPE_PORT);
                sqlite3_bind_int(survey_stmt, 3, port_id);
                sqlite3_bind_int(survey_stmt, 4, segment);
                sqlite3_step(survey_stmt);
                sqlite3_reset(survey_stmt);
                survey_count++;
            }
            sqlite3_finalize(port_select);
        }
    }
    
    sqlite3_finalize(survey_stmt);

    /* Commit transaction */
    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);

    printf("  ✓ Created %d survey assignments\n", survey_count);

    /* Sanity check: verify table has expected number of rows */
    sqlite3_stmt *verify_stmt;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM survey;", -1, &verify_stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(verify_stmt) == SQLITE_ROW) {
            int actual_count = sqlite3_column_int(verify_stmt, 0);
            if (actual_count == survey_count) {
                printf("  ✓ Sanity check passed: survey table contains %d rows\n", actual_count);
            } else {
                fprintf(stderr, "  ✗ WARNING: Expected %d rows but found %d in survey table!\n",
                        survey_count, actual_count);
            }
        }
        sqlite3_finalize(verify_stmt);
    } else {
        fprintf(stderr, "  ✗ WARNING: Could not verify survey table row count\n");
    }

    return SQLITE_OK;
}

/*
 * Compute and store distance matrix in database
 * Queries locations from database, calls distance computation, stores results
 */
static char *build_waypoint_path_json(const int *path, int path_len,
                                      const int *types, const int *loc_ids, int n)
{
    int waypoint_count = 0;
    char *json_path = NULL;

    if (!path || path_len <= 2 || !types || !loc_ids) return NULL;

    for (int k = 1; k < path_len - 1; k++) {
        int node_idx = path[k];
        if (node_idx >= 0 && node_idx < n && types[node_idx] == NODE_TYPE_WAYPOINT) {
            waypoint_count++;
        }
    }

    if (waypoint_count == 0) return NULL;

    int json_buffer_size = waypoint_count * 20 + 10;
    json_path = (char*)malloc((size_t)json_buffer_size);
    if (!json_path) return NULL;

    int json_pos = 0;
    int stored_waypoints = 0;
    json_pos += snprintf(json_path + json_pos, json_buffer_size - json_pos, "[");
    for (int k = 1; k < path_len - 1; k++) {
        int node_idx = path[k];
        if (node_idx >= 0 && node_idx < n && types[node_idx] == NODE_TYPE_WAYPOINT) {
            if (stored_waypoints > 0) {
                json_pos += snprintf(json_path + json_pos, json_buffer_size - json_pos, ",");
            }
            json_pos += snprintf(json_path + json_pos, json_buffer_size - json_pos, "%d", loc_ids[node_idx]);
            stored_waypoints++;
        }
    }
    snprintf(json_path + json_pos, json_buffer_size - json_pos, "]");
    return json_path;
}

static int compute_and_store_distances(sqlite3 *db) {
    printf("\n=== Computing Distance Matrix ===\n");
    printf("  Computing distances with Dijkstra waypoint routing\n");
    printf("  Loading ALL locations (including waypoints for routing)\n");

    /* Query ALL locations, joining with waypoints table to identify waypoint locations.
     * Waypoints must be ordered last for Dijkstra routing. */
    const char *query_sql =
        "SELECT l.id, l.lat, l.lon, "
        "  CASE WHEN w.id IS NOT NULL THEN 1 ELSE 0 END as is_waypoint "
        "FROM locations l "
        "LEFT JOIN waypoints w ON l.id = w.location_id "
        "ORDER BY is_waypoint, l.id;";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, query_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "  ✗ SQL error: %s\n", sqlite3_errmsg(db));
        return rc;
    }

    /* No binding needed - query has no parameters */

    /* Count locations */
    int n = 0;
    int n_waypoints = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int is_waypoint = sqlite3_column_int(stmt, 3);
        if (is_waypoint) n_waypoints++;
        n++;
    }
    sqlite3_reset(stmt);

    if (n == 0) {
        fprintf(stderr, "  ✗ No locations found\n");
        sqlite3_finalize(stmt);
        return SQLITE_ERROR;
    }

    printf("  ✓ Found %d total locations (%d waypoints, %d for routing)\n",
           n, n_waypoints, n - n_waypoints);
    int upper_triangle = n * (n - 1) / 2;
    printf("  → Will compute %d upper-triangle pairs (%d×%d matrix, diagonal=0)\n", upper_triangle, n, n);
    printf("  → Allocating memory...\n");

    /* Allocate arrays */
    int *loc_ids = (int*)calloc(n, sizeof(int));
    int *types = (int*)calloc(n, sizeof(int));
    double *latlon_rad[2];
    for (int k = 0; k < 2; k++) {
        latlon_rad[k] = (double*)malloc(n * sizeof(double));
    }

    /* Fill arrays */
    int i = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        loc_ids[i] = sqlite3_column_int(stmt, 0);
        double lat_deg = sqlite3_column_double(stmt, 1);  /* Database stores decimal degrees as REAL */
        double lon_deg = sqlite3_column_double(stmt, 2);  /* Already negative for western hemisphere */
        int is_waypoint = sqlite3_column_int(stmt, 3);

        /* Set type for distance computation (NODE_TYPE_WAYPOINT = 3 for waypoints) */
        types[i] = is_waypoint ? NODE_TYPE_WAYPOINT : 0;  /* Non-waypoints can be any non-3 value */

        /* Convert decimal degrees to radians */
        double lat_rad = deg_to_rad(lat_deg);
        double lon_rad = deg_to_rad(lon_deg);

        /* Column-major format for DistanceLink */
        latlon_rad[0][i] = lat_rad;  /* lat */
        latlon_rad[1][i] = lon_rad;  /* lon */
        i++;
    }
    sqlite3_finalize(stmt);

    if (!verify_waypoints_last(types, n)) {
        fprintf(stderr, "  ✗ Location ordering error: waypoints are not at the bottom\n");
        free(loc_ids);
        free(types);
        for (int k = 0; k < 2; k++) free(latlon_rad[k]);
        return SQLITE_ERROR;
    }

    if (n_waypoints > 0) {
        printf("  ✓ Verified ordering: non-waypoints first, waypoints last\n");
    }

    /* Initialize MAP structure with coastline data from database (sets bounding box via SQL MIN/MAX) */
    printf("\n=== Loading Coastline for Distance Computation ===\n");
    int n_coastline = 0;
    double *coastline_data = load_coastline_from_db(db, &n_coastline);
    if (!coastline_data) {
        fprintf(stderr, "  ✗ Failed to load coastline - distance computation may be inaccurate\n");
    }

    printf("\n=== Computing Distances with Waypoint Routing ===\n");
    printf("  This will take several minutes for %d locations...\n", n);

    double *D = NULL; // distance matrix (in nautical miles)
    int *F = NULL; // feasibility matrix (0 = crosses land, 1 = direct route)
    rc = compute_distance_matrix(n, latlon_rad, types, &D, &F);

    /* Cleanup coastline data */
    if (coastline_data) free(coastline_data);

    if (rc != 0 || !D) {
        fprintf(stderr, "  ✗ Distance computation failed\n");
        free(loc_ids);
        free(types);
        for (int k = 0; k < 2; k++) free(latlon_rad[k]);
        return SQLITE_ERROR;
    }

    printf("\n=== Storing Distance Matrix to Database ===\n");

    /* Store in database */
    printf("  → Storing distances in database...\n");

    const char *insert_sql =
        "INSERT INTO distances (from_location_id, to_location_id, distance_nm, crosses_land, waypoint_path) "
        "VALUES (?, ?, ?, ?, ?);";

    sqlite3_stmt *insert_stmt;
    rc = sqlite3_prepare_v2(db, insert_sql, -1, &insert_stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "  ✗ SQL prepare error: %s\n", sqlite3_errmsg(db));
        free(D);
        free(loc_ids);
        free(types);
        for (int k = 0; k < 2; k++) free(latlon_rad[k]);
        return rc;
    }

    /* Begin transaction */
    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
    sqlite3_exec(db, "DELETE FROM distances;", NULL, NULL, NULL);

    int stored = 0;
    int paths_stored = 0;
    int waypoint_pairs_stored = 0;
    int diagonal_stored = 0;
    int expected_pairs = n * n;

    for (i = 0; i < n; i++) {
        sqlite3_bind_int(insert_stmt, 1, loc_ids[i]);
        sqlite3_bind_int(insert_stmt, 2, loc_ids[i]);
        sqlite3_bind_double(insert_stmt, 3, 0.0);
        sqlite3_bind_int(insert_stmt, 4, 0);
        sqlite3_bind_null(insert_stmt, 5);
        if (sqlite3_step(insert_stmt) != SQLITE_DONE) {
            fprintf(stderr, "  ✗ Insert error: %s\n", sqlite3_errmsg(db));
            sqlite3_finalize(insert_stmt);
            sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
            free(D);
            cleanup_distance_matrices();
            free(loc_ids);
            free(types);
            for (int k = 0; k < 2; k++) free(latlon_rad[k]);
            return SQLITE_ERROR;
        }
        sqlite3_reset(insert_stmt);
        sqlite3_clear_bindings(insert_stmt);
        stored++;
        diagonal_stored++;

        for (int j = i + 1; j < n; j++) {
            double dist_ij = D[i + n * j];
            double dist_ji = D[j + n * i];
            int crosses_land_ij = (F[i + n * j] == 0) ? 1 : 0;
            int crosses_land_ji = (F[j + n * i] == 0) ? 1 : 0;
            int path_len_ij = 0;
            int path_len_ji = 0;
            int *path_ij = get_dijkstra_path(i, j, &path_len_ij);
            int *path_ji = get_dijkstra_path(j, i, &path_len_ji);
            char *json_ij = build_waypoint_path_json(path_ij, path_len_ij, types, loc_ids, n);
            char *json_ji = build_waypoint_path_json(path_ji, path_len_ji, types, loc_ids, n);

            sqlite3_bind_int(insert_stmt, 1, loc_ids[i]);
            sqlite3_bind_int(insert_stmt, 2, loc_ids[j]);
            sqlite3_bind_double(insert_stmt, 3, dist_ij);
            sqlite3_bind_int(insert_stmt, 4, crosses_land_ij);
            if (json_ij) sqlite3_bind_text(insert_stmt, 5, json_ij, -1, SQLITE_TRANSIENT);
            else sqlite3_bind_null(insert_stmt, 5);
            if (sqlite3_step(insert_stmt) != SQLITE_DONE) {
                fprintf(stderr, "  ✗ Insert error: %s\n", sqlite3_errmsg(db));
                free(json_ij);
                free(json_ji);
                sqlite3_finalize(insert_stmt);
                sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
                free(D);
                cleanup_distance_matrices();
                free(loc_ids);
                free(types);
                for (int k = 0; k < 2; k++) free(latlon_rad[k]);
                return SQLITE_ERROR;
            }
            sqlite3_reset(insert_stmt);
            sqlite3_clear_bindings(insert_stmt);
            stored++;
            if (types[i] == NODE_TYPE_WAYPOINT || types[j] == NODE_TYPE_WAYPOINT) waypoint_pairs_stored++;
            if (json_ij) paths_stored++;

            sqlite3_bind_int(insert_stmt, 1, loc_ids[j]);
            sqlite3_bind_int(insert_stmt, 2, loc_ids[i]);
            sqlite3_bind_double(insert_stmt, 3, dist_ji);
            sqlite3_bind_int(insert_stmt, 4, crosses_land_ji);
            if (json_ji) sqlite3_bind_text(insert_stmt, 5, json_ji, -1, SQLITE_TRANSIENT);
            else sqlite3_bind_null(insert_stmt, 5);
            if (sqlite3_step(insert_stmt) != SQLITE_DONE) {
                fprintf(stderr, "  ✗ Insert error: %s\n", sqlite3_errmsg(db));
                free(json_ij);
                free(json_ji);
                sqlite3_finalize(insert_stmt);
                sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
                free(D);
                cleanup_distance_matrices();
                free(loc_ids);
                free(types);
                for (int k = 0; k < 2; k++) free(latlon_rad[k]);
                return SQLITE_ERROR;
            }
            sqlite3_reset(insert_stmt);
            sqlite3_clear_bindings(insert_stmt);
            stored++;
            if (types[i] == NODE_TYPE_WAYPOINT || types[j] == NODE_TYPE_WAYPOINT) waypoint_pairs_stored++;
            if (json_ji) paths_stored++;

            free(json_ij);
            free(json_ji);
        }
    }

    sqlite3_finalize(insert_stmt);
    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);

    printf("  ✓ Stored %d distance rows (expected %d = %d×%d)\n", stored, expected_pairs, n, n);
    printf("  ✓ Stored %d diagonal identity rows\n", diagonal_stored);
    printf("  ✓ Stored %d waypoint-involved directed rows\n", waypoint_pairs_stored);
    printf("  ✓ Stored waypoint paths for %d routed directed rows\n", paths_stored);
    if (stored != expected_pairs) {
        fprintf(stderr, "  ✗ WARNING: distance table row count mismatch (stored=%d expected=%d)\n",
                stored, expected_pairs);
    }

    /* Cleanup */
    free(D);
    free(F);
    cleanup_distance_matrices();
    free(loc_ids);
    free(types);
    for (int k = 0; k < 2; k++) free(latlon_rad[k]);

    return SQLITE_OK;
}

/* Helper function to strip quotes from names */
static char* strip_quotes(const char *name) {
    if (!name) return NULL;
    const char *start = name;
    const char *end = name + strlen(name);
    if (*start == '"') start++;
    if (end > start && *(end-1) == '"') end--;
    size_t len = end - start;
    char *result = malloc(len + 1);
    memcpy(result, start, len);
    result[len] = '\0';
    return result;
}

/* Removed stale fast_check_feasibility_pair helper (unused, depended on undefined DistanceInputs). */

/* Helper to insert location and return its ID (reuses existing location if coordinates match) */
static int insert_location(sqlite3_stmt *insert_stmt, sqlite3_stmt *select_stmt, int lat_degmin, int lon_degmin) {
    /* Convert degmin integers to decimal degrees using helper function */
    double lat_deg = degmin_to_deg(lat_degmin);
    double lon_deg = degmin_to_deg_lon(lon_degmin);  /* Applies western hemisphere negation */

    /* First check if this location already exists */
    sqlite3_bind_int(select_stmt, 1, lat_degmin);
    sqlite3_bind_int(select_stmt, 2, lon_degmin);

    if (sqlite3_step(select_stmt) == SQLITE_ROW) {
        /* Location exists, return its ID */
        int loc_id = sqlite3_column_int(select_stmt, 0);
        sqlite3_reset(select_stmt);
        return loc_id;
    }
    sqlite3_reset(select_stmt);

    /* Location doesn't exist, insert it */
    sqlite3_bind_int(insert_stmt, 1, lat_degmin);
    sqlite3_bind_int(insert_stmt, 2, lon_degmin);
    sqlite3_bind_double(insert_stmt, 3, lat_deg);
    sqlite3_bind_double(insert_stmt, 4, lon_deg);
    sqlite3_step(insert_stmt);
    int loc_id = (int)sqlite3_last_insert_rowid(sqlite3_db_handle(insert_stmt));
    sqlite3_reset(insert_stmt);
    return loc_id;
}

/* Write parsed data to SQLite database */
static int write_to_sqlite(const char *db_path, const ItemVec *items, const char *source_file) {
    sqlite3 *db;
    int rc = sqlite3_open(db_path, &db);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "  ✗ Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return rc;
    }

    /* Drop existing tables if they exist */
    printf("  → Dropping existing tables (if any)...\n");
    sqlite3_exec(db, "DROP TABLE IF EXISTS boats;", NULL, NULL, NULL);
    sqlite3_exec(db, "DROP TABLE IF EXISTS stations;", NULL, NULL, NULL);
    sqlite3_exec(db, "DROP TABLE IF EXISTS ports;", NULL, NULL, NULL);
    sqlite3_exec(db, "DROP TABLE IF EXISTS waypoints;", NULL, NULL, NULL);
    sqlite3_exec(db, "DROP TABLE IF EXISTS locations;", NULL, NULL, NULL);
    sqlite3_exec(db, "DROP TABLE IF EXISTS survey;", NULL, NULL, NULL);
    sqlite3_exec(db, "DROP TABLE IF EXISTS distances;", NULL, NULL, NULL);
    sqlite3_exec(db, "DROP TABLE IF EXISTS coastline;", NULL, NULL, NULL);
    sqlite3_exec(db, "DROP TABLE IF EXISTS metadata;", NULL, NULL, NULL);

    /* Create normalized schema with survey assignment table */
    char *err_msg = NULL;
    const char *sql_schema =
        /* Locations table - stores raw degmin format only */
        "CREATE TABLE locations ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  easting INT,"
        "  northing INT,"
        "  lat REAL,"
        "  lon REAL,"
        "  UNIQUE(easting, northing)"
        ");"

        /* Boats table */
        "CREATE TABLE boats ("
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

        /* Stations table - NO boat_id, NO reitur/tog, pure station data */
        "CREATE TABLE stations ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  ext_id INTEGER,"
        "  start_location_id INTEGER,"
        "  end_location_id INTEGER,"
        "  c1 INTEGER,"
        "  c2 INTEGER,"
        "  c3 INTEGER,"
        "  amount INTEGER,"
        "  depth_thrown INTEGER,"     /* botndypi_kastad (depth when thrown/cast) */
        "  depth_haul INTEGER,"       /* botndypi_hift (depth when hauled) */
        "  comment TEXT,"             /* Cleaned comment (without depth info) */
        "  FOREIGN KEY (start_location_id) REFERENCES locations(id),"
        "  FOREIGN KEY (end_location_id) REFERENCES locations(id)"
        ");"

        /* Ports table - NO boat_id, pure port data */
        "CREATE TABLE ports ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT,"
        "  location_id INTEGER,"
        "  UNIQUE(location_id),"
        "  FOREIGN KEY (location_id) REFERENCES locations(id)"
        ");"

        /* Waypoints table */
        "CREATE TABLE waypoints ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  location_id INTEGER,"
        "  FOREIGN KEY (location_id) REFERENCES locations(id)"
        ");"

        /* Survey assignment table - maps boats to stations/ports with order */
        "CREATE TABLE survey ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT," /* Order in the survey route */
        "  boat_id INTEGER NOT NULL,"
        "  table_type INTEGER,"  /* NODE_TYPE_STATION or NODE_TYPE_PORT */
        "  table_id INTEGER NOT NULL,"  /* FK to stations.id or ports.id */
        "  segment INTEGER,"  /* Each segment is a trip at sea */
        "  FOREIGN KEY (boat_id) REFERENCES boats(id)"
        ");"

        /* Coastline table for island polygon data */
        "CREATE TABLE coastline ("
        "  id INTEGER PRIMARY KEY,"
        "  lat REAL,"       /* Decimal degrees (from island.bin) */
        "  lon REAL"        /* Decimal degrees (from island.bin) */
        ");"

        /* Metadata table */
        "CREATE TABLE metadata ("
        "  key TEXT PRIMARY KEY,"
        "  value TEXT"
        ");"

        /* Distances table - single distance matrix for all non-waypoint locations */
        "CREATE TABLE distances ("
        "  id INTEGER PRIMARY KEY,"
        "  from_location_id INTEGER REFERENCES locations(id),"
        "  to_location_id INTEGER REFERENCES locations(id),"
        "  distance_nm REAL,"
        "  crosses_land INTEGER DEFAULT 0,"  /* 1 if direct route crosses land, 0 otherwise */
        "  waypoint_path TEXT"               /* JSON array of waypoint location IDs used (NULL if direct) */
        ");";

    rc = sqlite3_exec(db, sql_schema, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "  ✗ SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return rc;
    }

    /* Begin transaction */
    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);

    /* Prepare statements */
    sqlite3_stmt *loc_insert_stmt, *loc_select_stmt, *boat_stmt, *stat_stmt, *port_stmt, *wayp_stmt;
    sqlite3_prepare_v2(db, "INSERT INTO locations (easting, northing, lat, lon) VALUES (?, ?, ?, ?);", -1, &loc_insert_stmt, NULL);
    sqlite3_prepare_v2(db, "SELECT id FROM locations WHERE easting = ? AND northing = ?;", -1, &loc_select_stmt, NULL);
    sqlite3_prepare_v2(db, "INSERT INTO boats (start_location_id, end_location_id, capacity, c1, c2, c3, c4, c5, c6, name) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);", -1, &boat_stmt, NULL);
    sqlite3_prepare_v2(db, "INSERT INTO stations (ext_id, start_location_id, end_location_id, c1, c2, c3, amount, depth_thrown, depth_haul, comment) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);", -1, &stat_stmt, NULL);
    sqlite3_prepare_v2(db, "INSERT INTO ports (name, location_id) VALUES (?, ?);", -1, &port_stmt, NULL);
    sqlite3_prepare_v2(db, "INSERT INTO waypoints (location_id) VALUES (?);", -1, &wayp_stmt, NULL);

    int boat_count = 0, stat_count = 0, port_count = 0, wayp_count = 0;


    for (int i = 0; i < items->n; i++) {
        if (items->a[i].Type == tSHIP) {
            /* Insert boat location for start */
            int start_loc_id = insert_location(loc_insert_stmt, loc_select_stmt,
                items->a[i].LatLonDegMin[0], items->a[i].LatLonDegMin[1]);

            /* Insert boat location for end */
            int end_loc_id = insert_location(loc_insert_stmt, loc_select_stmt,
                items->a[i].LatLonDegMin[2], items->a[i].LatLonDegMin[3]);

            /* Insert boat with all columns */
            char *clean_name = strip_quotes(items->a[i].Name);
            sqlite3_bind_int(boat_stmt, 1, start_loc_id);
            sqlite3_bind_int(boat_stmt, 2, end_loc_id);
            sqlite3_bind_int(boat_stmt, 3, (int)items->a[i].BoatData[4]);  /* capacity */
            sqlite3_bind_int(boat_stmt, 4, (int)items->a[i].BoatData[5]);  /* c1 */
            sqlite3_bind_int(boat_stmt, 5, (int)items->a[i].BoatData[6]);  /* c2 */
            sqlite3_bind_int(boat_stmt, 6, (int)items->a[i].BoatData[7]);  /* c3 */
            sqlite3_bind_int(boat_stmt, 7, (int)items->a[i].BoatData[8]);  /* c4 */
            sqlite3_bind_int(boat_stmt, 8, (int)items->a[i].BoatData[9]);  /* c5 */
            sqlite3_bind_int(boat_stmt, 9, (int)items->a[i].BoatData[10]);  /* c6 */
            sqlite3_bind_text(boat_stmt, 10, clean_name, -1, SQLITE_TRANSIENT);
            sqlite3_step(boat_stmt);
            sqlite3_reset(boat_stmt);
            boat_count++;
            free(clean_name);


        } else if (items->a[i].Type == tSTAT) {
            /* Insert start location */
            int start_loc_id = insert_location(loc_insert_stmt, loc_select_stmt,
                items->a[i].LatLonDegMin[0], items->a[i].LatLonDegMin[1]);

            /* Insert end location */
            int end_loc_id = insert_location(loc_insert_stmt, loc_select_stmt,
                items->a[i].LatLonDegMin[2], items->a[i].LatLonDegMin[3]);

            /* Parse comment to extract depth values */
            int depth_thrown = 0, depth_haul = 0;
            char *clean_comment = NULL;
            parse_station_comment(items->a[i].Comment, &depth_thrown, &depth_haul, &clean_comment);

            /* Insert station */
            sqlite3_bind_int(stat_stmt, 1, (int)items->a[i].StationData[0]);   /* ext_id (column 2) */
            sqlite3_bind_int(stat_stmt, 2, start_loc_id);
            sqlite3_bind_int(stat_stmt, 3, end_loc_id);
            sqlite3_bind_int(stat_stmt, 4, (int)items->a[i].StationData[1]);   /* c1 (column 3) */
            sqlite3_bind_int(stat_stmt, 5, (int)items->a[i].StationData[2]);   /* c2 (column 4) */
            sqlite3_bind_int(stat_stmt, 6, (int)items->a[i].StationData[8]);   /* c3 (column 10) */
            sqlite3_bind_int(stat_stmt, 7, (int)items->a[i].StationData[7]);   /* amount (column 9) */
            sqlite3_bind_int(stat_stmt, 8, depth_thrown);
            sqlite3_bind_int(stat_stmt, 9, depth_haul);
            sqlite3_bind_text(stat_stmt, 10, clean_comment ? clean_comment : "", -1, SQLITE_TRANSIENT);
            sqlite3_step(stat_stmt);
            sqlite3_reset(stat_stmt);
            stat_count++;

            /* Cleanup */
            if (clean_comment) free(clean_comment);


        } else if (items->a[i].Type == tPORT) {
            /* Insert port location */
            int loc_id = insert_location(loc_insert_stmt, loc_select_stmt,
                items->a[i].LatLonDegMin[0], items->a[i].LatLonDegMin[1]);

            /* Insert port (or ignore if already exists due to UNIQUE constraint) */
            char *clean_name = strip_quotes(items->a[i].Name);
            sqlite3_bind_text(port_stmt, 1, clean_name, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(port_stmt, 2, loc_id);
            sqlite3_step(port_stmt);
            sqlite3_reset(port_stmt);
            free(clean_name);
            port_count++;


        } else if (items->a[i].Type == tWAYP) {
            /* Insert waypoint location */
            int loc_id = insert_location(loc_insert_stmt, loc_select_stmt,
                items->a[i].LatLonDegMin[0], items->a[i].LatLonDegMin[1]);

            /* Insert waypoint (waypoints are NOT added to survey - they are routing helpers only) */
            sqlite3_bind_int(wayp_stmt, 1, loc_id);
            sqlite3_step(wayp_stmt);
            sqlite3_reset(wayp_stmt);
            wayp_count++;
        }
    }

    sqlite3_finalize(loc_insert_stmt);
    sqlite3_finalize(loc_select_stmt);
    sqlite3_finalize(boat_stmt);
    sqlite3_finalize(stat_stmt);
    sqlite3_finalize(port_stmt);
    sqlite3_finalize(wayp_stmt);

    /* Write metadata */
    char timestamp[64], sql[512];
    time_t now = time(NULL);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    snprintf(sql, sizeof(sql), "INSERT OR REPLACE INTO metadata VALUES ('source_file', '%s');", source_file);
    sqlite3_exec(db, sql, NULL, NULL, NULL);
    snprintf(sql, sizeof(sql), "INSERT OR REPLACE INTO metadata VALUES ('import_time', '%s');", timestamp);
    sqlite3_exec(db, sql, NULL, NULL, NULL);

    /* Commit */
    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);

    /* Import coastline data from island.bin */
    /* Derive island.bin path from database path (same directory) */
    char island_bin_path[512];
    snprintf(island_bin_path, sizeof(island_bin_path), "%s", db_path);
    char *last_slash = strrchr(island_bin_path, '/');
    if (!last_slash) last_slash = strrchr(island_bin_path, '\\');
    if (last_slash) {
        strcpy(last_slash + 1, "island.bin");
    } else {
        strcpy(island_bin_path, "island.bin");
    }

    printf("\n=== Importing Coastline Data ===\n");
    int coastline_rc = import_coastline_to_db(db, island_bin_path);
    if (coastline_rc == SQLITE_OK) {
        printf("  ✓ Coastline data imported successfully\n");
    } else {
        printf("  ⚠ Coastline import failed (continuing anyway)\n");
    }

    /* Build survey assignments (must be after entity insertion, before distance computation) */
    int survey_rc = build_survey_assignments(db, items);
    if (survey_rc != SQLITE_OK) {
        printf("  ✗ Failed to build survey assignments\n");
    }

    /* Compute distance matrix for all non-waypoint locations */
    int dist_rc = compute_and_store_distances(db);

    if (dist_rc == SQLITE_OK) {
        printf("  ✓ Distance matrix computed successfully\n");

        /* Query and report distances computed */
        sqlite3_stmt *count_stmt;
        if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM distances;", -1, &count_stmt, NULL) == SQLITE_OK) {
            if (sqlite3_step(count_stmt) == SQLITE_ROW) {
                int dist_count = sqlite3_column_int(count_stmt, 0);
                printf("  ✓ Stored %d distance pairs in distances table\n", dist_count);
            }
            sqlite3_finalize(count_stmt);
        }
    } else {
        printf("  ✗ Failed to compute distances (error code: %d)\n", dist_rc);
    }

    fflush(stdout);
    sqlite3_close(db);

    /* Query actual counts from database */
    sqlite3_open(db_path, &db);
    sqlite3_stmt *count_stmt;
    int actual_loc_count = 0;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM locations;", -1, &count_stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(count_stmt) == SQLITE_ROW) {
            actual_loc_count = sqlite3_column_int(count_stmt, 0);
        }
        sqlite3_finalize(count_stmt);
    }
    
    int survey_count = 0;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM survey;", -1, &count_stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(count_stmt) == SQLITE_ROW) {
            survey_count = sqlite3_column_int(count_stmt, 0);
        }
        sqlite3_finalize(count_stmt);
    }
    sqlite3_close(db);

    printf("  ✓ Wrote %d locations, %d boats, %d stations, %d ports, %d waypoints\n",
           actual_loc_count, boat_count, stat_count, port_count, wayp_count);
    printf("  ✓ Wrote %d survey assignments\n", survey_count);

    return SQLITE_OK;
}

/* Fast single-pair debug: compute direct crossing + haversine and update only two rows. */
static double haversine_km_from_deg(double lat1_deg, double lon1_deg, double lat2_deg, double lon2_deg)
{
    double lat1 = deg_to_rad(lat1_deg);
    double lon1 = deg_to_rad(lon1_deg);
    double lat2 = deg_to_rad(lat2_deg);
    double lon2 = deg_to_rad(lon2_deg);

    double dlat = lat2 - lat1;
    double dlon = lon2 - lon1;
    double a = sin(dlat / 2.0) * sin(dlat / 2.0) +
               cos(lat1) * cos(lat2) * sin(dlon / 2.0) * sin(dlon / 2.0);
    double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
    return 6371.0 * c;
}

static int debug_update_single_pair(sqlite3 *db, int from_id, int to_id)
{
    sqlite3_stmt *loc_stmt = NULL;
    const char *loc_sql =
        "SELECT id, lat, lon FROM locations WHERE id = ? OR id = ?;";

    if (sqlite3_prepare_v2(db, loc_sql, -1, &loc_stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "  ✗ SQL prepare error: %s\n", sqlite3_errmsg(db));
        return SQLITE_ERROR;
    }

    sqlite3_bind_int(loc_stmt, 1, from_id);
    sqlite3_bind_int(loc_stmt, 2, to_id);

    int got_from = 0, got_to = 0;
    double from_lat = 0.0, from_lon = 0.0, to_lat = 0.0, to_lon = 0.0;
    while (sqlite3_step(loc_stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(loc_stmt, 0);
        double lat = sqlite3_column_double(loc_stmt, 1);
        double lon = sqlite3_column_double(loc_stmt, 2);
        if (id == from_id) {
            got_from = 1;
            from_lat = lat;
            from_lon = lon;
        } else if (id == to_id) {
            got_to = 1;
            to_lat = lat;
            to_lon = lon;
        }
    }
    sqlite3_finalize(loc_stmt);

    if (!got_from || !got_to) {
        fprintf(stderr, "  ✗ Could not find both location IDs (%d, %d) in locations table\n", from_id, to_id);
        return SQLITE_ERROR;
    }

    int n_coastline = 0;
    double *coastline_data = load_coastline_from_db(db, &n_coastline);
    if (!coastline_data) {
        fprintf(stderr, "  ✗ Failed to load coastline\n");
        return SQLITE_ERROR;
    }

    int crosses = check_land_crossing_deg(from_lat, from_lon, to_lat, to_lon);
    free(coastline_data);

    double dist_km = haversine_km_from_deg(from_lat, from_lon, to_lat, to_lon);
    double dist_nm = dist_km / 1.852;
    double stored_nm_ij = crosses ? DIJKSTRA_INFINITY : dist_nm;
    double stored_nm_ji = stored_nm_ij;
    char *path_json_ij = NULL;
    char *path_json_ji = NULL;

    if (crosses) {
        /* Use the same core pipeline as normal mode, but on a reduced graph:
         * [from, to, all waypoints]. */
        sqlite3_stmt *wp_stmt = NULL;
        const char *wp_sql =
            "SELECT l.id, l.lat, l.lon "
            "FROM waypoints w "
            "JOIN locations l ON l.id = w.location_id "
            "ORDER BY l.id;";

        if (sqlite3_prepare_v2(db, wp_sql, -1, &wp_stmt, NULL) != SQLITE_OK) {
            fprintf(stderr, "  ✗ SQL prepare error: %s\n", sqlite3_errmsg(db));
            return SQLITE_ERROR;
        }

        int n_wp = 0;
        while (sqlite3_step(wp_stmt) == SQLITE_ROW) n_wp++;
        sqlite3_reset(wp_stmt);

        int m = n_wp + 2;
        int *node_ids = (int*)calloc((size_t)m, sizeof(int));
        int *node_types = (int*)calloc((size_t)m, sizeof(int));
        double *latlon_rad[2];
        latlon_rad[0] = (double*)calloc((size_t)m, sizeof(double));
        latlon_rad[1] = (double*)calloc((size_t)m, sizeof(double));

        if (!node_ids || !node_types || !latlon_rad[0] || !latlon_rad[1]) {
            sqlite3_finalize(wp_stmt);
            free(node_ids); free(node_types); free(latlon_rad[0]); free(latlon_rad[1]);
            return SQLITE_NOMEM;
        }

        /* Index 0/1 are non-waypoint endpoints. */
        node_ids[0] = from_id;
        node_ids[1] = to_id;
        node_types[0] = 0;
        node_types[1] = 0;
        latlon_rad[0][0] = deg_to_rad(from_lat);
        latlon_rad[1][0] = deg_to_rad(from_lon);
        latlon_rad[0][1] = deg_to_rad(to_lat);
        latlon_rad[1][1] = deg_to_rad(to_lon);

        int idx = 2;
        while (sqlite3_step(wp_stmt) == SQLITE_ROW) {
            node_ids[idx] = sqlite3_column_int(wp_stmt, 0);
            node_types[idx] = NODE_TYPE_WAYPOINT;
            latlon_rad[0][idx] = deg_to_rad(sqlite3_column_double(wp_stmt, 1));
            latlon_rad[1][idx] = deg_to_rad(sqlite3_column_double(wp_stmt, 2));
            idx++;
        }
        sqlite3_finalize(wp_stmt);

        double *D = NULL;
        int *F = NULL;
        if (compute_distance_matrix(m, latlon_rad, node_types, &D, &F) == 0 && D && F) {
            stored_nm_ij = D[0 * m + 1];
            stored_nm_ji = D[1 * m + 0];

            int path_len_ij = 0;
            int *path_ij = get_dijkstra_path(0, 1, &path_len_ij);
            if (path_ij && path_len_ij > 2) {
                int wp_count = 0;
                for (int k = 1; k < path_len_ij - 1; k++) {
                    if (node_types[path_ij[k]] == NODE_TYPE_WAYPOINT) wp_count++;
                }
                if (wp_count > 0) {
                    int buf = wp_count * 20 + 8;
                    path_json_ij = (char*)malloc((size_t)buf);
                    int pos = 0, added = 0;
                    pos += snprintf(path_json_ij + pos, (size_t)buf - (size_t)pos, "[");
                    for (int k = 1; k < path_len_ij - 1; k++) {
                        if (node_types[path_ij[k]] == NODE_TYPE_WAYPOINT) {
                            if (added++ > 0) pos += snprintf(path_json_ij + pos, (size_t)buf - (size_t)pos, ",");
                            pos += snprintf(path_json_ij + pos, (size_t)buf - (size_t)pos, "%d", node_ids[path_ij[k]]);
                        }
                    }
                    snprintf(path_json_ij + pos, (size_t)buf - (size_t)pos, "]");
                }
            }

            int path_len_ji = 0;
            int *path_ji = get_dijkstra_path(1, 0, &path_len_ji);
            if (path_ji && path_len_ji > 2) {
                int wp_count = 0;
                for (int k = 1; k < path_len_ji - 1; k++) {
                    if (node_types[path_ji[k]] == NODE_TYPE_WAYPOINT) wp_count++;
                }
                if (wp_count > 0) {
                    int buf = wp_count * 20 + 8;
                    path_json_ji = (char*)malloc((size_t)buf);
                    int pos = 0, added = 0;
                    pos += snprintf(path_json_ji + pos, (size_t)buf - (size_t)pos, "[");
                    for (int k = 1; k < path_len_ji - 1; k++) {
                        if (node_types[path_ji[k]] == NODE_TYPE_WAYPOINT) {
                            if (added++ > 0) pos += snprintf(path_json_ji + pos, (size_t)buf - (size_t)pos, ",");
                            pos += snprintf(path_json_ji + pos, (size_t)buf - (size_t)pos, "%d", node_ids[path_ji[k]]);
                        }
                    }
                    snprintf(path_json_ji + pos, (size_t)buf - (size_t)pos, "]");
                }
            }

            if (!path_json_ij || !path_json_ji) {
                stored_nm_ij = DIJKSTRA_INFINITY;
                stored_nm_ji = DIJKSTRA_INFINITY;
                free(path_json_ij);
                free(path_json_ji);
                path_json_ij = NULL;
                path_json_ji = NULL;
            }
        }

        free(D);
        free(F);
        cleanup_distance_matrices();
        free(node_ids);
        free(node_types);
        free(latlon_rad[0]);
        free(latlon_rad[1]);
    }

    printf("  Pair endpoints:\n");
    printf("    from_id=%d lat=%.6f lon=%.6f\n", from_id, from_lat, from_lon);
    printf("    to_id=%d   lat=%.6f lon=%.6f\n", to_id, to_lat, to_lon);
    printf("  Direct haversine: %.3f nm (%.3f km)\n", dist_nm, dist_km);
    printf("  Crosses land: %s\n", crosses ? "YES" : "NO");
    if (path_json_ij) printf("  Dijkstra waypoint path %d->%d: %s\n", from_id, to_id, path_json_ij);
    if (path_json_ji) printf("  Dijkstra waypoint path %d->%d: %s\n", to_id, from_id, path_json_ji);

    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);

    sqlite3_stmt *del_stmt = NULL;
    sqlite3_stmt *ins_stmt = NULL;
    const char *del_sql = "DELETE FROM distances WHERE (from_location_id = ? AND to_location_id = ?) OR (from_location_id = ? AND to_location_id = ?);";
    const char *ins_sql = "INSERT INTO distances (from_location_id, to_location_id, distance_nm, crosses_land, waypoint_path) VALUES (?, ?, ?, ?, ?);";

    if (sqlite3_prepare_v2(db, del_sql, -1, &del_stmt, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(db, ins_sql, -1, &ins_stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "  ✗ SQL prepare error: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(del_stmt);
        sqlite3_finalize(ins_stmt);
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        return SQLITE_ERROR;
    }

    sqlite3_bind_int(del_stmt, 1, from_id);
    sqlite3_bind_int(del_stmt, 2, to_id);
    sqlite3_bind_int(del_stmt, 3, to_id);
    sqlite3_bind_int(del_stmt, 4, from_id);
    sqlite3_step(del_stmt);
    sqlite3_finalize(del_stmt);

    sqlite3_bind_int(ins_stmt, 1, from_id);
    sqlite3_bind_int(ins_stmt, 2, to_id);
    sqlite3_bind_double(ins_stmt, 3, stored_nm_ij);
    sqlite3_bind_int(ins_stmt, 4, crosses);
    if (path_json_ij) sqlite3_bind_text(ins_stmt, 5, path_json_ij, -1, SQLITE_TRANSIENT);
    else sqlite3_bind_null(ins_stmt, 5);
    if (sqlite3_step(ins_stmt) != SQLITE_DONE) {
        fprintf(stderr, "  ✗ Insert error: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(ins_stmt);
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        return SQLITE_ERROR;
    }
    sqlite3_reset(ins_stmt);
    sqlite3_clear_bindings(ins_stmt);

    sqlite3_bind_int(ins_stmt, 1, to_id);
    sqlite3_bind_int(ins_stmt, 2, from_id);
    sqlite3_bind_double(ins_stmt, 3, stored_nm_ji);
    sqlite3_bind_int(ins_stmt, 4, crosses);
    if (path_json_ji) sqlite3_bind_text(ins_stmt, 5, path_json_ji, -1, SQLITE_TRANSIENT);
    else sqlite3_bind_null(ins_stmt, 5);
    if (sqlite3_step(ins_stmt) != SQLITE_DONE) {
        fprintf(stderr, "  ✗ Insert error: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(ins_stmt);
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        return SQLITE_ERROR;
    }
    sqlite3_finalize(ins_stmt);

    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
    free(path_json_ij);
    free(path_json_ji);
    return SQLITE_OK;
}

int main(int argc, char **argv) {
#ifdef _WIN32
    /* Set console to UTF-8 mode on Windows */
    SetConsoleOutputCP(CP_UTF8);
    setvbuf(stdout, NULL, _IOFBF, 1000);
#endif

    const char *dat_file = NULL;
    const char *db_path = NULL;
    int debug_distance_mode = 0;
    int from_location_id = -1;
    int to_location_id = -1;

    /* Parse arguments (positional + flags). */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug-distance") == 0) {
            debug_distance_mode = 1;
        } else if (strcmp(argv[i], "--db") == 0 && (i + 1) < argc) {
            db_path = argv[++i];
        } else if ((strcmp(argv[i], "--fromid") == 0 || strcmp(argv[i], "--from-id") == 0) && (i + 1) < argc) {
            from_location_id = atoi(argv[++i]);
        } else if ((strcmp(argv[i], "--toid") == 0 || strcmp(argv[i], "--to-id") == 0) && (i + 1) < argc) {
            to_location_id = atoi(argv[++i]);
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 1;
        } else if (dat_file == NULL) {
            dat_file = argv[i];
        } else if (db_path == NULL) {
            db_path = argv[i];
        }
    }

    if (debug_distance_mode) {
        if (db_path == NULL) db_path = "dat/gsp_data.db";

        if ((from_location_id > 0 && to_location_id <= 0) ||
            (to_location_id > 0 && from_location_id <= 0)) {
            fprintf(stderr, "  ✗ Provide both --fromid and --toid together\n");
            return 1;
        }

        sqlite3 *db = NULL;
        if (sqlite3_open(db_path, &db) != SQLITE_OK) {
            fprintf(stderr, "  ✗ Cannot open database: %s\n", sqlite3_errmsg(db));
            sqlite3_close(db);
            return 1;
        }

        printf("=== GSP Distance Debug ===\n");
        printf("Database: %s\n", db_path);

        int rc;
        if (from_location_id > 0 && to_location_id > 0) {
            printf("Mode: single-pair debug update (%d <-> %d)\n", from_location_id, to_location_id);
            rc = debug_update_single_pair(db, from_location_id, to_location_id);
        } else {
            printf("Mode: recompute distances table from existing DB\n");
            rc = compute_and_store_distances(db);
        }

        if (rc == SQLITE_OK && from_location_id > 0 && to_location_id > 0) {
            sqlite3_stmt *pair_stmt = NULL;
            const char *pair_sql =
                "SELECT from_location_id, to_location_id, distance_nm, crosses_land, "
                "COALESCE(waypoint_path, 'null') "
                "FROM distances "
                "WHERE (from_location_id = ? AND to_location_id = ?) "
                "   OR (from_location_id = ? AND to_location_id = ?);";

            if (sqlite3_prepare_v2(db, pair_sql, -1, &pair_stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_int(pair_stmt, 1, from_location_id);
                sqlite3_bind_int(pair_stmt, 2, to_location_id);
                sqlite3_bind_int(pair_stmt, 3, to_location_id);
                sqlite3_bind_int(pair_stmt, 4, from_location_id);

                printf("\nPair check (%d <-> %d):\n", from_location_id, to_location_id);
                while (sqlite3_step(pair_stmt) == SQLITE_ROW) {
                    int f = sqlite3_column_int(pair_stmt, 0);
                    int t = sqlite3_column_int(pair_stmt, 1);
                    double d = sqlite3_column_double(pair_stmt, 2);
                    int c = sqlite3_column_int(pair_stmt, 3);
                    const unsigned char *p = sqlite3_column_text(pair_stmt, 4);
                    printf("  %d -> %d | dist_nm=%.6f | crosses_land=%d | waypoint_path=%s\n",
                           f, t, d, c, p ? (const char*)p : "null");
                }
            }
            sqlite3_finalize(pair_stmt);
        }

        sqlite3_close(db);

        if (rc != SQLITE_OK) {
            printf("  ✗ Distance debug failed (error code: %d)\n", rc);
            return 1;
        }

        printf("  ✓ Distance debug complete\n");
        return 0;
    }

    if (dat_file == NULL) {
        fprintf(stderr, "Usage: %s <datafile.dat> [database.db]\n", argv[0]);
        fprintf(stderr, "  Parses .dat file and prepares data for optimization\n");
        fprintf(stderr, "\nDebug mode:\n");
        fprintf(stderr, "  %s --debug-distance [--db path/to/gsp_data.db] [--fromid N --toid M]\n", argv[0]);
        fprintf(stderr, "\nArguments:\n");
        fprintf(stderr, "  datafile.dat    Input .dat file to parse\n");
        fprintf(stderr, "  database.db     Output SQLite database (default: ../../../dat/gsp_data.db)\n");
        return 1;
    }

    /* Use default database path if not provided */
    if (db_path == NULL) {
        db_path = "../../../dat/gsp_data.db";
    }


    printf("=== GSP Data Preparation ===\n");
    printf("Input file: %s\n\n", dat_file);

    /* Parse entire .dat file (all boats) */
    printf("Parsing .dat file...\n");
    ItemVec all_items;
    item_vec_init(&all_items);
    read_dat_file_all_boats(dat_file, &all_items, 0);

    printf("  ✓ Loaded %d total items\n", all_items.n);

    /* List all boats found */
    char **boat_names = NULL;
    int n_boats = get_boat_names(&all_items, &boat_names);
    printf("\n=== Boats Found: %d ===\n", n_boats);
    for (int i = 0; i < n_boats; i++) {
        /* Get boat capacity */
        double cap = 0.0;
        for (int j = 0; j < all_items.n; j++) {
            if (all_items.a[j].Type == tSHIP &&
                strcmp(all_items.a[j].Name, boat_names[i]) == 0) {
                cap = all_items.a[j].BoatData[4];
                break;
            }
        }
        printf("  [%d] %-30s (capacity: %.0f)\n", i, boat_names[i], cap);
    }

    if (n_boats == 0) {
        printf("  ✗ No boats found in file!\n");
        item_vec_free(&all_items);
        return 1;
    }

    /* Count items by type across all boats */
    int total_stat = 0, total_port = 0, total_wayp = 0;
    int total_port_selected = 0;
    for (int i = 0; i < all_items.n; i++) {
        switch (all_items.a[i].Type) {
            case tSTAT: total_stat++; break;
            case tPORT:
                total_port++;
                if (all_items.a[i].PortSelected) total_port_selected++;
                break;
            case tWAYP: total_wayp++; break;
        }
    }

    printf("\n=== Data Summary ===\n");
    printf("  Boats:       %d\n", n_boats);
    printf("  Stations:    %d\n", total_stat);
    printf("  Ports:       %d (%d selected)\n", total_port, total_port_selected);
    printf("  Waypoints:   %d\n", total_wayp);
    printf("  Total items: %d\n", all_items.n);

    /* Show statistics per boat */
    printf("\n=== Per-Boat Statistics ===\n");
    fflush(stdout);

    for (int boat_idx = 0; boat_idx < n_boats; boat_idx++) {
        ItemVec boat_items;
        item_vec_init(&boat_items);
        double ship_cap = 0.0;

        printf("  Processing boat [%d]...\n", boat_idx);
        fflush(stdout);

        filter_items_by_boat(&all_items, boat_idx, &boat_items, &ship_cap);

        int count_stat = 0, count_port = 0, count_port_sel = 0;
        for (int i = 0; i < boat_items.n; i++) {
            if (boat_items.a[i].Type == tSTAT) count_stat++;
            else if (boat_items.a[i].Type == tPORT) {
                count_port++;
                if (boat_items.a[i].PortSelected) count_port_sel++;
            }
        }

        printf("  [%d] %s\n", boat_idx, boat_names[boat_idx]);
        printf("      Capacity: %.0f\n", ship_cap);
        printf("      Stations: %d\n", count_stat);
        printf("      Ports:    %d (%d selected)\n", count_port, count_port_sel);
        fflush(stdout);

        item_vec_free(&boat_items);
    }

    printf("\n=== Writing to SQLite Database ===\n");
    fflush(stdout);

    printf("  Database: %s\n", db_path);

    int db_rc = write_to_sqlite(db_path, &all_items, dat_file);
    if (db_rc == SQLITE_OK) {
        printf("  ✓ Successfully wrote data to database\n");
    } else {
        printf("  ✗ Failed to write to database (error code: %d)\n", db_rc);
    }

    printf("\nData preparation complete.\n");
    printf("\nDatabase location: dat/gsp_data.db\n");
    printf("\nQuery examples:\n");
    printf("  sqlite3 dat/gsp_data.db \"SELECT COUNT(*) FROM distances;\"\n");
    printf("  sqlite3 dat/gsp_data.db \"SELECT * FROM boats;\"\n");
    printf("  sqlite3 dat/gsp_data.db \"SELECT * FROM distances LIMIT 10;\"\n");
    fflush(stdout);

    /* Cleanup */
    item_vec_free(&all_items);
    for (int i = 0; i < n_boats; i++) {
        free(boat_names[i]);
    }
    free(boat_names);

    return 0;
}

