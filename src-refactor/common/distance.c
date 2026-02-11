/*
 * Distance Matrix Builder
 * Build distance and feasibility matrices with waypoint routing
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <math.h>
#include "../include/distance.h"

static void die(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    exit(1);
}

static void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) die("OOM");
    return p;
}

static void *xcalloc(size_t n, size_t s) {
    void *p = calloc(n, s);
    if (!p) die("OOM");
    return p;
}

/* Build waypoint-aware distance and feasibility matrices */
void build_waypoint_dist(const ExData *ex,
                        const double *Land, int nLand,
                        double **out_dist, int **out_fsb,
                        double **out_full_dist, int **out_full_fsb,
                        int *out_full_m) {
    (void)Land;
    (void)nLand;

    int m = ex->SelectedSize;
    int M = 2 * ex->SelectedSize;
    int n = 2 * ex->Size;

    /* Allocate full matrices M x M */
    int *F = (int*)xcalloc((size_t)M * (size_t)M, sizeof(int));
    double *D = (double*)xcalloc((size_t)M * (size_t)M, sizeof(double));

    /* Prepare column-major LatLon arrays for DistanceLink */
    double *latlon_cols[4];
    for (int k = 0; k < 4; k++) {
        latlon_cols[k] = (double*)xmalloc((size_t)m * sizeof(double));
    }
    for (int i = 0; i < m; i++) {
        latlon_cols[0][i] = ex->LatLonRad[i*4 + 0];
        latlon_cols[1][i] = ex->LatLonRad[i*4 + 1];
        latlon_cols[2][i] = ex->LatLonRad[i*4 + 2];
        latlon_cols[3][i] = ex->LatLonRad[i*4 + 3];
    }

    /* Type array for main route (excludes waypoints) */
    int *type_main = (int*)xmalloc((size_t)ex->Size * sizeof(int));
    for (int i = 0; i < ex->Size; i++) {
        type_main[i] = ex->Type[i];
    }

    /* Start/end coordinates */
    double start_end[4] = {
        ex->LatLonRad[0], ex->LatLonRad[1],
        ex->LatLonRad[2], ex->LatLonRad[3]
    };

    /* Call external DistanceLink function */
    if (DistanceLink(D, F, type_main, latlon_cols, start_end, ex->Size, ex->SelectedSize) != 0) {
        die("DistanceLink failed (could not read map?)");
    }

    for (int k = 0; k < 4; k++) free(latlon_cols[k]);
    free(type_main);

    /* Extract n x n submatrix for main route */
    double *dist = (double*)xmalloc((size_t)n * (size_t)n * sizeof(double));
    int *fsb = (int*)xmalloc((size_t)n * (size_t)n * sizeof(int));
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            dist[i*n + j] = D[i*M + j];
            fsb[i*n + j] = F[i*M + j];
        }
    }

    /* Output full matrices if requested */
    if (out_full_dist) *out_full_dist = D;
    else free(D);
    if (out_full_fsb) *out_full_fsb = F;
    else free(F);
    if (out_full_m) *out_full_m = M;

    *out_dist = dist;
    *out_fsb = fsb;
}

/* Load island.bin file (land polygon data) */
double *load_island_bin(const char *fname, int *out_n) {
    FILE *fp = fopen(fname, "rb");
    if (!fp) {
        perror("fopen island.bin");
        exit(1);
    }

    fseek(fp, 0, SEEK_END);
    long bytes = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (bytes <= 0 || (bytes % 4) != 0) die("island.bin size invalid");
    long nfloat = bytes / 4;
    if ((nfloat % 2) != 0) die("island.bin float count must be even");
    int n = (int)(nfloat / 2);

    float *buf = (float*)xmalloc((size_t)nfloat * sizeof(float));
    if (fread(buf, sizeof(float), (size_t)nfloat, fp) != (size_t)nfloat) {
        die("Failed to read island.bin");
    }
    fclose(fp);

    double *Land = (double*)xmalloc((size_t)nfloat * sizeof(double));
    for (long i = 0; i < nfloat; i++) {
        Land[i] = (double)buf[i];
    }
    free(buf);

    *out_n = n;
    return Land;
}

/*
 * Compute and store distances for all non-waypoint locations
 * boat_id is ignored - computes single distance matrix for all locations i,j where type != 'W'
 */
int compute_boat_distances_db(sqlite3 *db, int boat_id, const char *island_bin_path) {
    (void)boat_id;  /* Unused - compute for all locations */

    printf("  → Computing distance matrix for all non-waypoint locations\n");

    /* Query all non-waypoint locations with coordinates from v_locations view */
    const char *query_sql =
        "SELECT id, lat_deg, lon_deg, type "
        "FROM v_locations WHERE type NOT IN (3) ORDER BY id;";  /* 3 = NODE_TYPE_WAYPOINT */

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, query_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "    ✗ SQL error: %s\n", sqlite3_errmsg(db));
        return rc;
    }

    /* Count non-waypoint locations */
    int n = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        n++;
    }
    sqlite3_reset(stmt);

    if (n == 0) {
        printf("    ✗ No non-waypoint locations found\n");
        sqlite3_finalize(stmt);
        return SQLITE_ERROR;
    }

    printf("    ✓ Found %d non-waypoint locations\n", n);

    /* Allocate arrays for location data */
    int *loc_ids = (int*)xcalloc((size_t)n, sizeof(int));
    int *types = (int*)xcalloc((size_t)n, sizeof(int));
    double *latlon_cols[4];
    for (int k = 0; k < 4; k++) {
        latlon_cols[k] = (double*)xmalloc((size_t)n * sizeof(double));
    }

    /* Fill arrays from query results */
    int i = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        loc_ids[i] = sqlite3_column_int(stmt, 0);
        double lat_deg = sqlite3_column_double(stmt, 1);
        double lon_deg = sqlite3_column_double(stmt, 2);
        int type_val = sqlite3_column_int(stmt, 3);

        double lat_rad = lat_deg * M_PI / 180.0;
        double lon_rad = lon_deg * M_PI / 180.0;
        types[i] = type_val;

        /* Column-major format for DistanceLink */
        latlon_cols[0][i] = lat_rad;      /* lat_start */
        latlon_cols[1][i] = lon_rad;      /* lon_start */
        latlon_cols[2][i] = lat_rad;      /* lat_end   */
        latlon_cols[3][i] = lon_rad;      /* lon_end   */
        i++;
    }
    sqlite3_finalize(stmt);

    /* Load waypoints from v_locations for Dijkstra routing */
    const char *wayp_sql =
        "SELECT lat_deg, lon_deg FROM v_locations WHERE type = 3;";  /* 3 = NODE_TYPE_WAYPOINT */

    sqlite3_stmt *wayp_stmt;
    rc = sqlite3_prepare_v2(db, wayp_sql, -1, &wayp_stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "    ✗ SQL error: %s\n", sqlite3_errmsg(db));
        free(loc_ids);
        free(types);
        for (int k = 0; k < 4; k++) free(latlon_cols[k]);
        return rc;
    }

    int n_wayp = 0;
    while (sqlite3_step(wayp_stmt) == SQLITE_ROW) {
        n_wayp++;
    }
    sqlite3_reset(wayp_stmt);

    printf("    ✓ Found %d waypoints\n", n_wayp);

    /* Allocate waypoint array (2 coords per waypoint: lat, lon in radians) */
    double *waypoints = NULL;
    if (n_wayp > 0) {
        waypoints = (double*)xmalloc((size_t)(n_wayp * 2) * sizeof(double));
        i = 0;
        while (sqlite3_step(wayp_stmt) == SQLITE_ROW) {
            double lat_deg = sqlite3_column_double(wayp_stmt, 0);
            double lon_deg = sqlite3_column_double(wayp_stmt, 1);
            waypoints[i * 2 + 0] = lat_deg * M_PI / 180.0;
            waypoints[i * 2 + 1] = lon_deg * M_PI / 180.0;
            i++;
        }
    }
    sqlite3_finalize(wayp_stmt);

    /* Load island.bin for land contours */
    int n_land = 0;
    double *Land = NULL;
    if (island_bin_path) {
        Land = load_island_bin(island_bin_path, &n_land);
        printf("    ✓ Loaded island.bin: %d land polygon points\n", n_land);
    }

    /* Allocate distance and feasibility matrices */
    double *D = (double*)xcalloc((size_t)n * (size_t)n, sizeof(double));
    int *F = (int*)xcalloc((size_t)n * (size_t)n, sizeof(int));

    /* Start/end points: first and last locations */
    double start_end[4] = {
        latlon_cols[0][0], latlon_cols[1][0],
        latlon_cols[0][n-1], latlon_cols[1][n-1]
    };

    /* Call DistanceLink (Dijkstra-based distance computation) */
    printf("    → Calling DistanceLink (Dijkstra routing)...\n");
    rc = DistanceLink(D, F, types, latlon_cols, start_end, n, n);
    if (rc != 0) {
        fprintf(stderr, "    ✗ DistanceLink failed (error %d)\n", rc);
        free(D);
        free(F);
        for (int k = 0; k < 4; k++) free(latlon_cols[k]);
        free(loc_ids);
        free(types);
        if (waypoints) free(waypoints);
        if (Land) free(Land);
        return rc;
    }

    printf("    ✓ DistanceLink computed %d×%d distance matrix\n", n, n);

    /* Store distances in database */
    const char *insert_sql = "INSERT INTO distances (from_location_id, to_location_id, distance_nm) VALUES (?, ?, ?);";
    sqlite3_stmt *insert_stmt;
    rc = sqlite3_prepare_v2(db, insert_sql, -1, &insert_stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "    ✗ SQL prepare error: %s\n", sqlite3_errmsg(db));
        free(D);
        free(F);
        for (int k = 0; k < 4; k++) free(latlon_cols[k]);
        free(loc_ids);
        free(types);
        if (waypoints) free(waypoints);
        if (Land) free(Land);
        return rc;
    }

    /* Begin transaction for inserts */
    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);

    int stored = 0;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            double dist = (i == j) ? 0.0 : D[i * n + j];

            sqlite3_bind_int(insert_stmt, 1, loc_ids[i]);
            sqlite3_bind_int(insert_stmt, 2, loc_ids[j]);
            sqlite3_bind_double(insert_stmt, 3, dist);

            if (sqlite3_step(insert_stmt) != SQLITE_DONE) {
                fprintf(stderr, "    ✗ Insert error: %s\n", sqlite3_errmsg(db));
            }
            sqlite3_reset(insert_stmt);
            stored++;
        }
    }

    sqlite3_finalize(insert_stmt);
    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);

    printf("    ✓ Stored %d distance pairs\n", stored);

    /* Cleanup */
    free(D);
    free(F);
    for (int k = 0; k < 4; k++) free(latlon_cols[k]);
    free(loc_ids);
    free(types);
    if (waypoints) free(waypoints);
    if (Land) free(Land);

    return SQLITE_OK;
}

/*
 * Compute all distances for all non-waypoint locations
 * Single distance matrix: all locations i,j where type != 'W'
 */
int compute_all_distances_db(sqlite3 *db, const char *island_bin_path) {
    printf("\n=== Computing Waypoint-Aware Distances ===\n");
    printf("  Computing single distance matrix for all non-waypoint locations\n");
    printf("  Distances between all i,j where i,j != waypoints\n");
    printf("  Using Dijkstra routing with island.bin contours\n");
    printf("  Island.bin path: %s\n", island_bin_path ? island_bin_path : "dat/island.bin");

    /* Query ALL non-waypoint locations */
    const char *sql_locs =
        "SELECT id FROM locations WHERE type NOT IN (3) ORDER BY id;";  /* 3 = NODE_TYPE_WAYPOINT */

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql_locs, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "  ✗ SQL error: %s\n", sqlite3_errmsg(db));
        return rc;
    }

    /* Count non-waypoint locations */
    int n_locs = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        n_locs++;
    }
    sqlite3_finalize(stmt);

    printf("  → Found %d non-waypoint locations\n", n_locs);
    printf("  → Computing %d×%d distance matrix\n", n_locs, n_locs);

    /* Call compute_boat_distances_db to compute for all locations */
    rc = compute_boat_distances_db(db, 0, island_bin_path);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "  ✗ Distance computation failed\n");
        return rc;
    }

    printf("\n  ✓ Computed distances for all %d non-waypoint locations\n", n_locs);
    printf("  ✓ Distance matrix cached in distances table\n");

    return SQLITE_OK;
}

