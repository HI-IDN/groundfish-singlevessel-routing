/*
 * Coastline Database Utilities
 * Import and load island polygon data from SQLite database or file
 */

#include <stdio.h>
#include <stdlib.h>
#include <sqlite3.h>
#include "../include/coastline_db.h"

/* MAP structure - shared global for land polygon data (used by distance computation) */
struct {
    int n;
    int N[10];
    double *LatDeg[10];
    double *LonDeg[10];
    double MINLAT, MAXLAT, MINLON, MAXLON;
} MAP[1] = {{0}};  /* Initialize to zero */

/* Load island.bin file (land polygon data) - initializes global MAP structure */
double *load_island_bin(const char *fname, int *out_n) {
    int i;
    FILE *fp;
    float *land_data;
    size_t fileSize, numElements;

    if (!fname) {
        fprintf(stderr, "Error: island.bin path is NULL\n");
        return NULL;
    }

    fp = fopen(fname, "rb");
    if (!fp) {
        perror("fopen island.bin");
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    fileSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (fileSize <= 0 || (fileSize % 4) != 0) {
        fprintf(stderr, "island.bin size invalid\n");
        fclose(fp);
        return NULL;
    }

    numElements = fileSize / sizeof(float);
    if ((numElements % 2) != 0) {
        fprintf(stderr, "island.bin float count must be even\n");
        fclose(fp);
        return NULL;
    }

    land_data = (float *)malloc(fileSize);
    if (!land_data) {
        perror("Memory allocation failed");
        fclose(fp);
        return NULL;
    }

    if (fread(land_data, sizeof(float), numElements, fp) != numElements) {
        fprintf(stderr, "Failed to read island.bin\n");
        free(land_data);
        fclose(fp);
        return NULL;
    }
    fclose(fp);

    /* Initialize global MAP structure (required by distance_link) */
    MAP[0].n = 1;
    MAP[0].N[0] = numElements / 2;
    MAP[0].LatDeg[0] = (double *) malloc(MAP[0].N[0] * sizeof(double));
    MAP[0].LonDeg[0] = (double *) malloc(MAP[0].N[0] * sizeof(double));

    if (!MAP[0].LatDeg[0] || !MAP[0].LonDeg[0]) {
        fprintf(stderr, "Memory allocation failed for MAP\n");
        free(land_data);
        free(MAP[0].LatDeg[0]);
        free(MAP[0].LonDeg[0]);
        return NULL;
    }

    /* Convert float land data to double arrays */
    for (i = 0; i < MAP[0].N[0]; i++) {
        MAP[0].LatDeg[0][i] = (double)land_data[i];
        MAP[0].LonDeg[0][i] = (double)land_data[MAP[0].N[0] + i];
    }

    /* Compute actual Iceland bounds from polygon data */
    MAP[0].MINLAT = MAP[0].MAXLAT = MAP[0].LatDeg[0][0];
    MAP[0].MINLON = MAP[0].MAXLON = MAP[0].LonDeg[0][0];

    for (i = 1; i < MAP[0].N[0]; i++) {
        double lat = MAP[0].LatDeg[0][i];
        double lon = MAP[0].LonDeg[0][i];

        if (lat < MAP[0].MINLAT) MAP[0].MINLAT = lat;
        if (lat > MAP[0].MAXLAT) MAP[0].MAXLAT = lat;
        if (lon < MAP[0].MINLON) MAP[0].MINLON = lon;
        if (lon > MAP[0].MAXLON) MAP[0].MAXLON = lon;
    }

    printf("  Iceland bounding box: Lat [%.2f, %.2f], Lon [%.2f, %.2f]\n",
           MAP[0].MINLAT, MAP[0].MAXLAT, MAP[0].MINLON, MAP[0].MAXLON);


    /* Convert and return as double array */
    double *land = (double*)malloc((size_t)numElements * sizeof(double));
    if (!land) {
        fprintf(stderr, "Memory allocation failed\n");
        free(land_data);
        return NULL;
    }

    for (long j = 0; j < (long)numElements; j++) {
        land[j] = (double)land_data[j];
    }
    free(land_data);

    *out_n = MAP[0].N[0];
    return land;
}

/* Import island.bin polygon data into coastline table */
int import_coastline_to_db(sqlite3 *db, const char *island_bin_path) {
    FILE *fp = fopen(island_bin_path, "rb");
    if (!fp) {
        fprintf(stderr, "  ⚠ Warning: Could not open %s for coastline import\n", island_bin_path);
        return SQLITE_ERROR;
    }

    /* Get file size */
    fseek(fp, 0, SEEK_END);
    size_t file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size <= 0 || (file_size % sizeof(float)) != 0) {
        fprintf(stderr, "  ✗ Invalid island.bin file size\n");
        fclose(fp);
        return SQLITE_ERROR;
    }

    size_t num_floats = file_size / sizeof(float);
    if ((num_floats % 2) != 0) {
        fprintf(stderr, "  ✗ island.bin must have even number of floats (lat/lon pairs)\n");
        fclose(fp);
        return SQLITE_ERROR;
    }

    /* Read all data */
    float *data = (float*)malloc(file_size);
    if (!data) {
        fprintf(stderr, "  ✗ Memory allocation failed\n");
        fclose(fp);
        return SQLITE_ERROR;
    }

    if (fread(data, sizeof(float), num_floats, fp) != num_floats) {
        fprintf(stderr, "  ✗ Failed to read island.bin\n");
        free(data);
        fclose(fp);
        return SQLITE_ERROR;
    }
    fclose(fp);

    /* Insert into database */
    int num_points = num_floats / 2;
    printf("  → Importing %d coastline points...\n", num_points);

    const char *insert_sql = "INSERT INTO coastline (lat, lon) VALUES (?, ?);";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, insert_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "  ✗ Failed to prepare coastline insert: %s\n", sqlite3_errmsg(db));
        free(data);
        return rc;
    }

    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);

    for (int i = 0; i < num_points; i++) {
        double lat = (double)data[i];
        double lon = (double)data[num_points + i];

        sqlite3_bind_double(stmt, 1, lat);
        sqlite3_bind_double(stmt, 2, lon);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            fprintf(stderr, "  ✗ Failed to insert coastline point %d\n", i);
            sqlite3_finalize(stmt);
            sqlite3_finalize(stmt);
            sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
            free(data);
            return SQLITE_ERROR;
        }
        sqlite3_reset(stmt);
    }

    sqlite3_finalize(stmt);
    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
    free(data);

    printf("  ✓ Imported %d coastline polygon points\n", num_points);
    return SQLITE_OK;
}

/* Load island polygon data from database (stub - needs MAP structure access) */
double *load_coastline_from_db(sqlite3 *db, int *out_n) {
    /* Get bounding box and count in one query */
    const char *bounds_sql =
        "SELECT COUNT(*), MIN(lat), MAX(lat), MIN(lon), MAX(lon) FROM coastline;";
    sqlite3_stmt *bounds_stmt;

    if (sqlite3_prepare_v2(db, bounds_sql, -1, &bounds_stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Error: Failed to query coastline bounds\n");
        return NULL;
    }

    int num_points = 0;
    double min_lat, max_lat, min_lon, max_lon;

    if (sqlite3_step(bounds_stmt) == SQLITE_ROW) {
        num_points = sqlite3_column_int(bounds_stmt, 0);
        min_lat = sqlite3_column_double(bounds_stmt, 1);
        max_lat = sqlite3_column_double(bounds_stmt, 2);
        min_lon = sqlite3_column_double(bounds_stmt, 3);
        max_lon = sqlite3_column_double(bounds_stmt, 4);
    }
    sqlite3_finalize(bounds_stmt);

    if (num_points == 0) {
        fprintf(stderr, "Error: No coastline data in database\n");
        return NULL;
    }

    /* Set MAP bounding box (no margin - exact bounds) */
    MAP[0].MINLAT = min_lat;
    MAP[0].MAXLAT = max_lat;
    MAP[0].MINLON = min_lon;
    MAP[0].MAXLON = max_lon;

    printf("  Iceland bounding box: Lat [%.2f, %.2f], Lon [%.2f, %.2f]\n",
           MAP[0].MINLAT, MAP[0].MAXLAT, MAP[0].MINLON, MAP[0].MAXLON);

    /* Initialize MAP structure */
    MAP[0].n = 1;
    MAP[0].N[0] = num_points;
    MAP[0].LatDeg[0] = (double*)malloc((size_t)num_points * sizeof(double));
    MAP[0].LonDeg[0] = (double*)malloc((size_t)num_points * sizeof(double));

    if (!MAP[0].LatDeg[0] || !MAP[0].LonDeg[0]) {
        fprintf(stderr, "Error: Memory allocation failed for MAP\n");
        free(MAP[0].LatDeg[0]);
        free(MAP[0].LonDeg[0]);
        return NULL;
    }

    /* Load coastline data */
    const char *query_sql = "SELECT lat, lon FROM coastline ORDER BY id;";
    sqlite3_stmt *stmt;

    if (sqlite3_prepare_v2(db, query_sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Error: Failed to query coastline data\n");
        free(MAP[0].LatDeg[0]);
        free(MAP[0].LonDeg[0]);
        return NULL;
    }

    /* Allocate flat array: [lat1..latN, lon1..lonN] */
    double *land = (double*)malloc((size_t)(num_points * 2) * sizeof(double));
    if (!land) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        sqlite3_finalize(stmt);
        free(MAP[0].LatDeg[0]);
        free(MAP[0].LonDeg[0]);
        return NULL;
    }

    int i = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        double lat = sqlite3_column_double(stmt, 0);
        double lon = sqlite3_column_double(stmt, 1);

        land[i] = lat;
        land[num_points + i] = lon;

        /* Also populate MAP structure for distance_link */
        MAP[0].LatDeg[0][i] = lat;
        MAP[0].LonDeg[0][i] = lon;

        i++;
    }
    sqlite3_finalize(stmt);

    *out_n = num_points;
    return land;
}



