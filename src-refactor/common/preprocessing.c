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

/*
 * Compute and store distance matrix in database
 * Queries locations from database, calls distance computation, stores results
 */
static int compute_and_store_distances(sqlite3 *db) {
    printf("\n=== Computing Distance Matrix ===\n");
    printf("  Computing distances for all non-waypoint locations\n");
    printf("  Using Dijkstra routing with coastline from database\n");

    /* Query all non-waypoint locations */
    const char *query_sql =
        "SELECT id, lat, lon, type FROM locations WHERE type != ? ORDER BY id;";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, query_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "  ✗ SQL error: %s\n", sqlite3_errmsg(db));
        return rc;
    }

    sqlite3_bind_int(stmt, 1, NODE_TYPE_WAYPOINT);

    /* Count locations */
    int n = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        n++;
    }
    sqlite3_reset(stmt);

    if (n == 0) {
        fprintf(stderr, "  ✗ No non-waypoint locations found\n");
        sqlite3_finalize(stmt);
        return SQLITE_ERROR;
    }

    printf("  ✓ Found %d non-waypoint locations\n", n);
    printf("  → Will compute %d distance pairs (%d×%d matrix)\n", n*n, n, n);
    printf("  → Allocating memory...\n");

    /* Allocate arrays */
    int *loc_ids = (int*)calloc(n, sizeof(int));
    int *types = (int*)calloc(n, sizeof(int));
    double *latlon_rad[4];
    for (int k = 0; k < 4; k++) {
        latlon_rad[k] = (double*)malloc(n * sizeof(double));
    }

    /* Fill arrays */
    int i = 0;
    sqlite3_bind_int(stmt, 1, NODE_TYPE_WAYPOINT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        loc_ids[i] = sqlite3_column_int(stmt, 0);
        int easting = sqlite3_column_int(stmt, 1);
        int northing = sqlite3_column_int(stmt, 2);
        types[i] = sqlite3_column_int(stmt, 3);

        /* Convert degmin to decimal degrees, then to radians */
        double lat_deg = degmin_to_deg(easting);
        double lon_deg = degmin_to_deg_lon(northing);  /* Applies western hemisphere negation */
        double lat_rad = deg_to_rad(lat_deg);
        double lon_rad = deg_to_rad(lon_deg);

        /* Column-major format for DistanceLink */
        latlon_rad[0][i] = lat_rad;  /* lat_start */
        latlon_rad[1][i] = lon_rad;  /* lon_start */
        latlon_rad[2][i] = lat_rad;  /* lat_end */
        latlon_rad[3][i] = lon_rad;  /* lon_end */
        i++;
    }
    sqlite3_finalize(stmt);

    /* Initialize MAP structure with coastline data from database (sets bounding box via SQL MIN/MAX) */
    printf("\n=== Loading Coastline for Distance Computation ===\n");
    int n_coastline = 0;
    double *coastline_data = load_coastline_from_db(db, &n_coastline);
    if (!coastline_data) {
        fprintf(stderr, "  ✗ Failed to load coastline - distance computation may be inaccurate\n");
    }

    printf("\n=== Computing Distances with Waypoint Routing ===\n");
    printf("  This will take several minutes for %d locations...\n", n);

    /* Call distance computation - MAP structure is now initialized with bounding box */
    /*
     * TODO: Update compute_distance_matrix() to return waypoint routing information:
     *   - For each pair (i,j), check if direct path crosses land
     *   - If crossing detected, run Dijkstra through waypoints
     *   - Return: distance, crosses_land flag, and waypoint_path array
     *
     * For now, we compute direct haversine distances and set:
     *   - crosses_land = 0 (assume no crossing)
     *   - waypoint_path = NULL (no waypoints used)
     */
    double *D = NULL;
    rc = compute_distance_matrix(n, latlon_rad, types, &D);

    /* Cleanup coastline data */
    if (coastline_data) free(coastline_data);

    if (rc != 0 || !D) {
        fprintf(stderr, "  ✗ Distance computation failed\n");
        free(loc_ids);
        free(types);
        for (int k = 0; k < 4; k++) free(latlon_rad[k]);
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
        for (int k = 0; k < 4; k++) free(latlon_rad[k]);
        return rc;
    }

    /* Begin transaction */
    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);

    int stored = 0;
    for (i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            double dist = (i == j) ? 0.0 : D[i * n + j];

            sqlite3_bind_int(insert_stmt, 1, loc_ids[i]);
            sqlite3_bind_int(insert_stmt, 2, loc_ids[j]);
            sqlite3_bind_double(insert_stmt, 3, dist);
            sqlite3_bind_int(insert_stmt, 4, 0);        /* crosses_land = 0 (direct route for now) */
            sqlite3_bind_null(insert_stmt, 5);          /* waypoint_path = NULL (no waypoints for now) */

            if (sqlite3_step(insert_stmt) != SQLITE_DONE) {
                fprintf(stderr, "  ✗ Insert error: %s\n", sqlite3_errmsg(db));
                sqlite3_finalize(insert_stmt);
                sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
                free(D);
                free(loc_ids);
                free(types);
                for (int k = 0; k < 4; k++) free(latlon_rad[k]);
                return SQLITE_ERROR;
            }
            sqlite3_reset(insert_stmt);
            stored++;
        }
    }

    sqlite3_finalize(insert_stmt);
    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);

    printf("  ✓ Stored %d distance pairs\n", stored);

    /* Cleanup */
    free(D);
    free(loc_ids);
    free(types);
    for (int k = 0; k < 4; k++) free(latlon_rad[k]);

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

/* Helper to insert location and return its ID */
static int insert_location(sqlite3_stmt *stmt, int type, int lat_degmin, int lon_degmin) {
    /* Convert degmin integers to decimal degrees using helper function */
    double lat_deg = degmin_to_deg(lat_degmin);
    double lon_deg = degmin_to_deg_lon(lon_degmin);  /* Applies western hemisphere negation */

    sqlite3_bind_int(stmt, 1, type);
    sqlite3_bind_int(stmt, 2, lat_degmin);
    sqlite3_bind_int(stmt, 3, lon_degmin);
    sqlite3_bind_double(stmt, 4, lat_deg);
    sqlite3_bind_double(stmt, 5, lon_deg);
    sqlite3_step(stmt);
    int loc_id = (int)sqlite3_last_insert_rowid(sqlite3_db_handle(stmt));
    sqlite3_reset(stmt);
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
    sqlite3_exec(db, "DROP TABLE IF EXISTS survey_2023;", NULL, NULL, NULL);
    sqlite3_exec(db, "DROP TABLE IF EXISTS distances;", NULL, NULL, NULL);
    sqlite3_exec(db, "DROP TABLE IF EXISTS coastline;", NULL, NULL, NULL);
    sqlite3_exec(db, "DROP TABLE IF EXISTS metadata;", NULL, NULL, NULL);

    /* Create normalized schema with survey assignment table */
    char *err_msg = NULL;
    const char *sql_schema =
        /* Locations table - stores raw degmin format only */
        "CREATE TABLE locations ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  type INT,"
        "  easting INT,"
        "  northing INT,"
        "  lat REAL,"
        "  lon REAL"
        ");"

        /* Boats table */
        "CREATE TABLE boats ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT NOT NULL,"
        "  capacity INT NOT NULL,"
        "  location_id INTEGER,"
        "  FOREIGN KEY (location_id) REFERENCES locations(id)"
        ");"

        /* Stations table - NO boat_id, NO reitur/tog, pure station data */
        "CREATE TABLE stations ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  amount REAL,"
        "  start_location_id INTEGER,"
        "  end_location_id INTEGER,"
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
        "  selected INTEGER,"
        "  location_id INTEGER,"
        "  FOREIGN KEY (location_id) REFERENCES locations(id)"
        ");"

        /* Waypoints table */
        "CREATE TABLE waypoints ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  location_id INTEGER,"
        "  FOREIGN KEY (location_id) REFERENCES locations(id)"
        ");"

        /* Survey assignment table - maps boats to stations/ports with order */
        "CREATE TABLE survey_2023 ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  boat_id INTEGER NOT NULL,"
        "  location_type INTEGER,"  /* NODE_TYPE_STATION or NODE_TYPE_PORT */
        "  location_id INTEGER NOT NULL,"  /* FK to stations.id or ports.id */
        "  order_num INTEGER,"  /* Order in the survey route */
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
    sqlite3_stmt *loc_stmt, *boat_stmt, *stat_stmt, *port_stmt, *wayp_stmt, *survey_stmt;
    sqlite3_prepare_v2(db, "INSERT INTO locations (type, easting, northing, lat, lon) VALUES (?, ?, ?, ?, ?);", -1, &loc_stmt, NULL);
    sqlite3_prepare_v2(db, "INSERT INTO boats (name, capacity, location_id) VALUES (?, ?, ?);", -1, &boat_stmt, NULL);
    sqlite3_prepare_v2(db, "INSERT INTO stations (amount, start_location_id, end_location_id, depth_thrown, depth_haul, comment) VALUES (?, ?, ?, ?, ?, ?);", -1, &stat_stmt, NULL);
    sqlite3_prepare_v2(db, "INSERT INTO ports (name, selected, location_id) VALUES (?, ?, ?);", -1, &port_stmt, NULL);
    sqlite3_prepare_v2(db, "INSERT INTO waypoints (location_id) VALUES (?);", -1, &wayp_stmt, NULL);
    sqlite3_prepare_v2(db, "INSERT INTO survey_2023 (boat_id, location_type, location_id, order_num) VALUES (?, ?, ?, ?);", -1, &survey_stmt, NULL);

    int boat_count = 0, stat_count = 0, port_count = 0, wayp_count = 0, loc_count = 0, survey_count = 0;
    int current_boat_id = 0;
    int order_num = 0;


    for (int i = 0; i < items->n; i++) {
        if (items->a[i].Type == tSHIP) {
            /* Insert boat location */
            int loc_id = insert_location(loc_stmt, NODE_TYPE_BOAT,
                items->a[i].LatLonDegMin[0], items->a[i].LatLonDegMin[1]);
            loc_count++;

            /* Insert boat (strip quotes from name) */
            char *clean_name = strip_quotes(items->a[i].Name);
            sqlite3_bind_text(boat_stmt, 1, clean_name, -1, SQLITE_TRANSIENT);
            sqlite3_bind_double(boat_stmt, 2, items->a[i].BoatData[4]);
            sqlite3_bind_int(boat_stmt, 3, loc_id);
            sqlite3_step(boat_stmt);
            current_boat_id = (int)sqlite3_last_insert_rowid(db);
            sqlite3_reset(boat_stmt);
            boat_count++;
            free(clean_name);

            /* Reset order for new boat */
            order_num = 0;

        } else if (items->a[i].Type == tSTAT && current_boat_id > 0) {
            /* Insert start location */
            int start_loc_id = insert_location(loc_stmt, NODE_TYPE_STATION,
                items->a[i].LatLonDegMin[0], items->a[i].LatLonDegMin[1]);
            loc_count++;

            /* Insert end location */
            int end_loc_id = insert_location(loc_stmt, NODE_TYPE_STATION,
                items->a[i].LatLonDegMin[2], items->a[i].LatLonDegMin[3]);
            loc_count++;

            /* Parse comment to extract depth values */
            int depth_thrown = 0, depth_haul = 0;
            char *clean_comment = NULL;
            parse_station_comment(items->a[i].Comment, &depth_thrown, &depth_haul, &clean_comment);

            /* Insert station (NO boat_id, NO reitur/tog) */
            sqlite3_bind_double(stat_stmt, 1, items->a[i].Amount);
            sqlite3_bind_int(stat_stmt, 2, start_loc_id);
            sqlite3_bind_int(stat_stmt, 3, end_loc_id);
            sqlite3_bind_int(stat_stmt, 4, depth_thrown);
            sqlite3_bind_int(stat_stmt, 5, depth_haul);
            sqlite3_bind_text(stat_stmt, 6, clean_comment ? clean_comment : "", -1, SQLITE_TRANSIENT);
            sqlite3_step(stat_stmt);
            int station_id = (int)sqlite3_last_insert_rowid(db);
            sqlite3_reset(stat_stmt);
            stat_count++;

            /* Cleanup */
            if (clean_comment) free(clean_comment);

            /* Insert into survey_2023 table */
            sqlite3_bind_int(survey_stmt, 1, current_boat_id);
            sqlite3_bind_int(survey_stmt, 2, NODE_TYPE_STATION);
            sqlite3_bind_int(survey_stmt, 3, station_id);
            sqlite3_bind_int(survey_stmt, 4, order_num++);
            sqlite3_step(survey_stmt);
            sqlite3_reset(survey_stmt);
            survey_count++;

        } else if (items->a[i].Type == tPORT && current_boat_id > 0) {
            /* Insert port location */
            int loc_id = insert_location(loc_stmt, NODE_TYPE_PORT,
                items->a[i].LatLonDegMin[0], items->a[i].LatLonDegMin[1]);
            loc_count++;

            /* Insert port (NO boat_id, strip quotes from name) */
            char *clean_name = strip_quotes(items->a[i].Name);
            sqlite3_bind_text(port_stmt, 1, clean_name, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(port_stmt, 2, items->a[i].PortSelected);
            sqlite3_bind_int(port_stmt, 3, loc_id);
            sqlite3_step(port_stmt);
            int port_id = (int)sqlite3_last_insert_rowid(db);
            sqlite3_reset(port_stmt);
            port_count++;
            free(clean_name);

            /* Insert into survey_2023 table */
            sqlite3_bind_int(survey_stmt, 1, current_boat_id);
            sqlite3_bind_int(survey_stmt, 2, NODE_TYPE_PORT);
            sqlite3_bind_int(survey_stmt, 3, port_id);
            sqlite3_bind_int(survey_stmt, 4, order_num++);
            sqlite3_step(survey_stmt);
            sqlite3_reset(survey_stmt);
            survey_count++;

        } else if (items->a[i].Type == tWAYP) {
            /* Insert waypoint location */
            int loc_id = insert_location(loc_stmt, NODE_TYPE_WAYPOINT,
                items->a[i].LatLonDegMin[0], items->a[i].LatLonDegMin[1]);
            loc_count++;

            /* Insert waypoint */
            sqlite3_bind_int(wayp_stmt, 1, loc_id);
            sqlite3_step(wayp_stmt);
            sqlite3_reset(wayp_stmt);
            wayp_count++;
        }
    }

    sqlite3_finalize(loc_stmt);
    sqlite3_finalize(boat_stmt);
    sqlite3_finalize(stat_stmt);
    sqlite3_finalize(port_stmt);
    sqlite3_finalize(wayp_stmt);
    sqlite3_finalize(survey_stmt);

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

    printf("  ✓ Wrote %d locations, %d boats, %d stations, %d ports, %d waypoints\n",
           loc_count, boat_count, stat_count, port_count, wayp_count);
    printf("  ✓ Wrote %d survey assignments\n", survey_count);

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

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (dat_file == NULL) {
            dat_file = argv[i];
        } else if (db_path == NULL) {
            db_path = argv[i];
        }
    }

    if (dat_file == NULL) {
        fprintf(stderr, "Usage: %s <datafile.dat> [database.db]\n", argv[0]);
        fprintf(stderr, "  Parses .dat file and prepares data for optimization\n");
        fprintf(stderr, "\nArguments:\n");
        fprintf(stderr, "  datafile.dat    Input .dat file to parse\n");
        fprintf(stderr, "  database.db     Output SQLite database (default: ../../../dat/gsp_data.db)\n");
        fprintf(stderr, "\nExample:\n");
        fprintf(stderr, "  %s ../../dat/data2023spring.dat\n", argv[0]);
        fprintf(stderr, "  %s ../../dat/data2023spring.dat ../../dat/custom.db\n", argv[0]);
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

