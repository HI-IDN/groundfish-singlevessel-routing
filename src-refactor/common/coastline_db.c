/*
 * Coastline Database Utilities
 * Import and load island polygon data from SQLite database or file
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <geos_c.h>
#include "../include/coastline_db.h"

/* MAP structure - shared global for land polygon data (used by distance computation) */
struct {
    int n;
    int N[10];
    double *LatDeg[10];
    double *LonDeg[10];
    double MINLAT, MAXLAT, MINLON, MAXLON;
} MAP[1] = {{0}};  /* Initialize to zero */

void free_coastline_points(CoastlinePoints *pts) {
    if (!pts) return;
    free(pts->lat);
    free(pts->lon);
    pts->lat = NULL;
    pts->lon = NULL;
    pts->n = 0;
}

static GEOSGeometry *build_polygon_from_raw_points(GEOSContextHandle_t ctx, const float *data, int num_points) {
    GEOSCoordSequence *seq = GEOSCoordSeq_create_r(ctx, (unsigned int)(num_points + 1), 2);
    if (!seq) return NULL;

    for (int i = 0; i < num_points; i++) {
        GEOSCoordSeq_setX_r(ctx, seq, (unsigned int)i, (double)data[num_points + i]);
        GEOSCoordSeq_setY_r(ctx, seq, (unsigned int)i, (double)data[i]);
    }
    GEOSCoordSeq_setX_r(ctx, seq, (unsigned int)num_points, (double)data[num_points]);
    GEOSCoordSeq_setY_r(ctx, seq, (unsigned int)num_points, (double)data[0]);

    GEOSGeometry *ring = GEOSGeom_createLinearRing_r(ctx, seq);
    if (!ring) {
        GEOSCoordSeq_destroy_r(ctx, seq);
        return NULL;
    }

    GEOSGeometry *polygon = GEOSGeom_createPolygon_r(ctx, ring, NULL, 0);
    if (!polygon) {
        GEOSGeom_destroy_r(ctx, ring);
        return NULL;
    }
    return polygon;
}

static int extract_exterior_ring_points(GEOSContextHandle_t ctx, const GEOSGeometry *polygon, CoastlinePoints *out) {
    const GEOSGeometry *ring = GEOSGetExteriorRing_r(ctx, polygon);
    if (!ring) return 0;

    const GEOSCoordSequence *seq = GEOSGeom_getCoordSeq_r(ctx, ring);
    if (!seq) return 0;

    unsigned int size = 0;
    if (!GEOSCoordSeq_getSize_r(ctx, seq, &size) || size < 4) return 0;

    /* GEOS rings are closed; store only unique vertices (drop repeated final point). */
    unsigned int n = size - 1;
    double *lat = (double *)malloc((size_t)n * sizeof(double));
    double *lon = (double *)malloc((size_t)n * sizeof(double));
    if (!lat || !lon) {
        free(lat);
        free(lon);
        return 0;
    }

    for (unsigned int i = 0; i < n; i++) {
        GEOSCoordSeq_getX_r(ctx, seq, i, &lon[i]);
        GEOSCoordSeq_getY_r(ctx, seq, i, &lat[i]);
    }

    out->lat = lat;
    out->lon = lon;
    out->n = (int)n;
    return 1;
}

static const GEOSGeometry *pick_largest_polygon_component(GEOSContextHandle_t ctx,
                                                           const GEOSGeometry *multipolygon,
                                                           int *out_components,
                                                           double *out_area) {
    int num = GEOSGetNumGeometries_r(ctx, multipolygon);
    if (out_components) *out_components = num;
    if (num <= 0) return NULL;

    const GEOSGeometry *best = NULL;
    double best_area = -1.0;
    for (int i = 0; i < num; i++) {
        const GEOSGeometry *candidate = GEOSGetGeometryN_r(ctx, multipolygon, i);
        double area = 0.0;
        if (!candidate || !GEOSArea_r(ctx, candidate, &area)) continue;
        if (area > best_area) {
            best_area = area;
            best = candidate;
        }
    }

    if (out_area) *out_area = best_area;
    return best;
}

static int sanitize_coastline_geometry(const float *data, int num_points, CoastlinePoints *out) {
    GEOSContextHandle_t ctx = GEOS_init_r();
    GEOSGeometry *polygon = NULL;
    GEOSGeometry *fixed = NULL;
    int rc = 0;

    if (!ctx || !out || !data || num_points < 3) goto cleanup;

    polygon = build_polygon_from_raw_points(ctx, data, num_points);
    if (!polygon) {
        fprintf(stderr, "  ✗ Failed to build coastline polygon from island.bin data\n");
        goto cleanup;
    }

    char *reason = NULL;
    GEOSGeometry *location = NULL;
    char is_valid = GEOSisValidDetail_r(ctx, polygon, 0, &reason, &location);
    if (!is_valid) {
        double x = 0.0, y = 0.0;
        int has_location = 0;
        if (location && GEOSGeomTypeId_r(ctx, location) == GEOS_POINT) {
            const GEOSCoordSequence *loc_seq = GEOSGeom_getCoordSeq_r(ctx, location);
            if (loc_seq && GEOSCoordSeq_getX_r(ctx, loc_seq, 0, &x) && GEOSCoordSeq_getY_r(ctx, loc_seq, 0, &y)) {
                has_location = 1;
            }
        }

        if (has_location) {
            fprintf(stderr,
                    "  Warning: Coastline polygon invalid at (lat=%.6f, lon=%.6f): %s -> Fixing with buffer(0)...\n",
                    y, x, reason ? reason : "unknown reason");
        } else {
            fprintf(stderr,
                    "  Warning: Coastline polygon invalid: %s -> Fixing with buffer(0)...\n",
                    reason ? reason : "unknown reason");
        }

        if (reason) GEOSFree_r(ctx, reason);
        reason = NULL;
        if (location) GEOSGeom_destroy_r(ctx, location);
        location = NULL;

        fixed = GEOSBuffer_r(ctx, polygon, 0.0, 8);
        if (!fixed) {
            fprintf(stderr, "  ✗ buffer(0) failed to repair coastline geometry\n");
            goto cleanup;
        }

        GEOSGeom_destroy_r(ctx, polygon);
        polygon = fixed;
        fixed = NULL;

        is_valid = GEOSisValidDetail_r(ctx, polygon, 0, &reason, &location);
        if (!is_valid) {
            fprintf(stderr, "  ✗ Coastline geometry still invalid after repair: %s\n", reason ? reason : "unknown reason");
            if (reason) GEOSFree_r(ctx, reason);
            if (location) GEOSGeom_destroy_r(ctx, location);
            goto cleanup;
        }

        fprintf(stderr, "  Warning: Coastline polygon repaired and valid after buffer(0).\n");
        if (reason) GEOSFree_r(ctx, reason);
        if (location) GEOSGeom_destroy_r(ctx, location);
    }

    if (GEOSisEmpty_r(ctx, polygon)) {
        fprintf(stderr, "  ✗ Coastline geometry repair collapsed to empty geometry\n");
        goto cleanup;
    }

    int type_id = GEOSGeomTypeId_r(ctx, polygon);
    const GEOSGeometry *selected_polygon = polygon;

    if (type_id == GEOS_MULTIPOLYGON) {
        int components = 0;
        double area = 0.0;
        selected_polygon = pick_largest_polygon_component(ctx, polygon, &components, &area);
        if (!selected_polygon || area <= 0.0) {
            fprintf(stderr, "  ✗ Coastline multipolygon repair produced no usable polygon component\n");
            goto cleanup;
        }
        fprintf(stderr,
                "  Warning: Coastline repair returned MultiPolygon (%d parts); using largest component (area=%.6f).\n",
                components, area);
    } else if (type_id != GEOS_POLYGON) {
        fprintf(stderr, "  ✗ Coastline geometry type unsupported after repair (type_id=%d)\n", type_id);
        goto cleanup;
    }

    if (!extract_exterior_ring_points(ctx, selected_polygon, out) || out->n < 3) {
        fprintf(stderr, "  ✗ Failed to extract valid coastline ring points after repair\n");
        goto cleanup;
    }

    rc = 1;

cleanup:
    if (!rc) free_coastline_points(out);
    if (fixed) GEOSGeom_destroy_r(ctx, fixed);
    if (polygon) GEOSGeom_destroy_r(ctx, polygon);
    if (ctx) GEOS_finish_r(ctx);
    return rc;
}

int load_repaired_coastline_from_bin(const char *island_bin_path, CoastlinePoints *out) {
    FILE *fp = NULL;
    float *data = NULL;
    size_t file_size = 0;
    size_t num_floats = 0;
    int rc = 0;

    if (!island_bin_path || !out) {
        return 0;
    }

    memset(out, 0, sizeof(*out));

    fp = fopen(island_bin_path, "rb");
    if (!fp) {
        fprintf(stderr, "  Warning: Could not open %s for coastline import\n", island_bin_path);
        return 0;
    }

    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size <= 0 || (file_size % sizeof(float)) != 0) {
        fprintf(stderr, "  ✗ Invalid island.bin file size\n");
        goto cleanup;
    }

    num_floats = file_size / sizeof(float);
    if ((num_floats % 2) != 0) {
        fprintf(stderr, "  ✗ island.bin must have even number of floats (lat/lon pairs)\n");
        goto cleanup;
    }

    data = (float*)malloc(file_size);
    if (!data) {
        fprintf(stderr, "  ✗ Memory allocation failed\n");
        goto cleanup;
    }

    if (fread(data, sizeof(float), num_floats, fp) != num_floats) {
        fprintf(stderr, "  ✗ Failed to read island.bin\n");
        goto cleanup;
    }

    rc = sanitize_coastline_geometry(data, (int)(num_floats / 2), out);

cleanup:
    if (fp) fclose(fp);
    free(data);
    if (!rc) free_coastline_points(out);
    return rc;
}

int replace_coastline_in_db(sqlite3 *db, const CoastlinePoints *coastline) {
    const char *insert_sql = "INSERT INTO coastline (lat, lon) VALUES (?, ?);";
    sqlite3_stmt *stmt = NULL;
    int rc;

    if (!db || !coastline || coastline->n < 3 || !coastline->lat || !coastline->lon) {
        return SQLITE_ERROR;
    }

    rc = sqlite3_prepare_v2(db, insert_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "  ✗ Failed to prepare coastline insert: %s\n", sqlite3_errmsg(db));
        return rc;
    }

    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
    sqlite3_exec(db, "DELETE FROM coastline;", NULL, NULL, NULL);

    for (int i = 0; i < coastline->n; i++) {
        sqlite3_bind_double(stmt, 1, coastline->lat[i]);
        sqlite3_bind_double(stmt, 2, coastline->lon[i]);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            fprintf(stderr, "  ✗ Failed to insert coastline point %d\n", i);
            sqlite3_finalize(stmt);
            sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
            return SQLITE_ERROR;
        }
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
    }

    sqlite3_finalize(stmt);
    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
    printf("  Imported %d coastline polygon points\n", coastline->n);
    return SQLITE_OK;
}

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
    CoastlinePoints sanitized = {0};
    if (!load_repaired_coastline_from_bin(island_bin_path, &sanitized)) {
        return SQLITE_ERROR;
    }

    /* Insert sanitized polygon into database. */
    printf("  → Importing %d coastline points...\n", sanitized.n);
    int rc = replace_coastline_in_db(db, &sanitized);
    free_coastline_points(&sanitized);
    return rc;
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


