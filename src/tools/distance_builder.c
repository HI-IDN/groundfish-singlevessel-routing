/*
 * Distance matrix builder.
 *
 * Uses all locations plus waypoint nodes for routing, and stores the full
 * N x N distances table.
 */

#include "../include/coastline_db.h"
#include "../include/constants.h"
#include "../include/distance.h"
#include "../include/distance_builder.h"
#include "../include/geo_utils.h"
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int verify_waypoints_last(const int *types, int n) {
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

static char *build_waypoint_path_json(const int *path, int path_len,
                                      const int *types, const int *loc_ids, int n) {
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
    json_pos += snprintf(json_path + json_pos, (size_t)json_buffer_size - (size_t)json_pos, "[");
    for (int k = 1; k < path_len - 1; k++) {
        int node_idx = path[k];
        if (node_idx >= 0 && node_idx < n && types[node_idx] == NODE_TYPE_WAYPOINT) {
            if (stored_waypoints > 0) {
                json_pos += snprintf(json_path + json_pos, (size_t)json_buffer_size - (size_t)json_pos, ",");
            }
            json_pos += snprintf(json_path + json_pos, (size_t)json_buffer_size - (size_t)json_pos, "%d", loc_ids[node_idx]);
            stored_waypoints++;
        }
    }
    snprintf(json_path + json_pos, (size_t)json_buffer_size - (size_t)json_pos, "]");
    return json_path;
}

static int int_contains(const int *arr, int n, int value) {
    for (int i = 0; i < n; i++) {
        if (arr[i] == value) return 1;
    }
    return 0;
}

static int int_push_unique(int **arr, int *n, int *cap, int value) {
    if (int_contains(*arr, *n, value)) return 1;
    if (*n >= *cap) {
        int new_cap = (*cap == 0) ? 64 : (*cap * 2);
        int *tmp = (int*)realloc(*arr, (size_t)new_cap * sizeof(int));
        if (!tmp) return 0;
        *arr = tmp;
        *cap = new_cap;
    }
    (*arr)[(*n)++] = value;
    return 1;
}

static int collect_waypoint_ids_from_json(const char *json, int **used_ids, int *used_n, int *used_cap) {
    const char *p = json;
    if (!json) return 1;
    while (*p) {
        char *endptr = NULL;
        long v = strtol(p, &endptr, 10);
        if (endptr != p) {
            if (!int_push_unique(used_ids, used_n, used_cap, (int)v)) return 0;
            p = endptr;
        } else {
            p++;
        }
    }
    return 1;
}

static int report_waypoint_usage(sqlite3 *db) {
    sqlite3_stmt *path_stmt = NULL;
    sqlite3_stmt *way_stmt = NULL;
    sqlite3_stmt *meta_stmt = NULL;
    int *used_ids = NULL;
    int used_n = 0;
    int used_cap = 0;
    int total_waypoints = 0;
    int rc = SQLITE_OK;

    rc = sqlite3_prepare_v2(db,
                            "SELECT waypoint_path FROM distances WHERE waypoint_path IS NOT NULL AND waypoint_path <> '';",
                            -1, &path_stmt, NULL);
    if (rc != SQLITE_OK) goto cleanup;

    while (sqlite3_step(path_stmt) == SQLITE_ROW) {
        const char *json = (const char*)sqlite3_column_text(path_stmt, 0);
        if (!collect_waypoint_ids_from_json(json, &used_ids, &used_n, &used_cap)) {
            rc = SQLITE_NOMEM;
            goto cleanup;
        }
    }
    sqlite3_finalize(path_stmt);
    path_stmt = NULL;

    rc = sqlite3_prepare_v2(db, "SELECT location_id FROM waypoints;", -1, &way_stmt, NULL);
    if (rc != SQLITE_OK) goto cleanup;

    while (sqlite3_step(way_stmt) == SQLITE_ROW) {
        total_waypoints++;
    }
    if (sqlite3_errcode(db) != SQLITE_OK && sqlite3_errcode(db) != SQLITE_DONE) {
        rc = sqlite3_errcode(db);
        goto cleanup;
    }
    sqlite3_finalize(way_stmt);
    way_stmt = NULL;

    if (sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO metadata(key, value) VALUES(?, ?);", -1, &meta_stmt, NULL) == SQLITE_OK) {
        char value[32];

        snprintf(value, sizeof(value), "%d", total_waypoints);
        sqlite3_bind_text(meta_stmt, 1, "distance_total_waypoint_count", -1, SQLITE_STATIC);
        sqlite3_bind_text(meta_stmt, 2, value, -1, SQLITE_TRANSIENT);
        sqlite3_step(meta_stmt);
        sqlite3_reset(meta_stmt);
        sqlite3_clear_bindings(meta_stmt);

        snprintf(value, sizeof(value), "%d", used_n);
        sqlite3_bind_text(meta_stmt, 1, "distance_used_waypoint_count", -1, SQLITE_STATIC);
        sqlite3_bind_text(meta_stmt, 2, value, -1, SQLITE_TRANSIENT);
        sqlite3_step(meta_stmt);
    }
    printf("Waypoint usage: used=%d total=%d unused=%d\n", used_n, total_waypoints, total_waypoints - used_n);

cleanup:
    free(used_ids);
    sqlite3_finalize(path_stmt);
    sqlite3_finalize(way_stmt);
    sqlite3_finalize(meta_stmt);
    return rc;
}

static int compute_and_store_distances(sqlite3 *db) {
    const char *query_sql =
        "SELECT l.id, l.lat, l.lon, "
        "  CASE WHEN w.id IS NOT NULL THEN 1 ELSE 0 END as is_waypoint "
        "FROM locations l "
        "LEFT JOIN waypoints w ON l.id = w.location_id "
        "ORDER BY is_waypoint, l.id;";
    const char *insert_sql =
        "INSERT INTO distances (from_location_id, to_location_id, distance_nm, crosses_land, waypoint_path) "
        "VALUES (?, ?, ?, ?, ?);";

    sqlite3_stmt *stmt = NULL;
    sqlite3_stmt *insert_stmt = NULL;
    int rc = sqlite3_prepare_v2(db, query_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", sqlite3_errmsg(db));
        return rc;
    }

    int n = 0;
    int n_waypoints = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int is_waypoint = sqlite3_column_int(stmt, 3);
        if (is_waypoint) n_waypoints++;
        n++;
    }
    sqlite3_reset(stmt);

    if (n == 0) {
        fprintf(stderr, "No locations found\n");
        sqlite3_finalize(stmt);
        return SQLITE_ERROR;
    }

    printf("Found %d total locations\n", n);
    printf("  non-waypoints: %d\n", n - n_waypoints);
    printf("  waypoints:     %d\n", n_waypoints);

    int *loc_ids = (int*)calloc((size_t)n, sizeof(int));
    int *types = (int*)calloc((size_t)n, sizeof(int));
    double *latlon_rad[2];
    latlon_rad[0] = (double*)calloc((size_t)n, sizeof(double));
    latlon_rad[1] = (double*)calloc((size_t)n, sizeof(double));
    if (!loc_ids || !types || !latlon_rad[0] || !latlon_rad[1]) {
        sqlite3_finalize(stmt);
        free(loc_ids);
        free(types);
        free(latlon_rad[0]);
        free(latlon_rad[1]);
        return SQLITE_NOMEM;
    }

    int i = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        double lat_deg = sqlite3_column_double(stmt, 1);
        double lon_deg = sqlite3_column_double(stmt, 2);
        int is_waypoint = sqlite3_column_int(stmt, 3);

        loc_ids[i] = sqlite3_column_int(stmt, 0);
        types[i] = is_waypoint ? NODE_TYPE_WAYPOINT : 0;
        latlon_rad[0][i] = deg_to_rad(lat_deg);
        latlon_rad[1][i] = deg_to_rad(lon_deg);
        i++;
    }
    sqlite3_finalize(stmt);

    if (!verify_waypoints_last(types, n)) {
        fprintf(stderr, "Location ordering error: waypoints are not last\n");
        free(loc_ids);
        free(types);
        free(latlon_rad[0]);
        free(latlon_rad[1]);
        return SQLITE_ERROR;
    }

    int coastline_count_unused = 0;
    double *coastline_data = load_coastline_from_db(db, &coastline_count_unused);
    (void)coastline_count_unused;
    if (!coastline_data) {
        fprintf(stderr, "Failed to load coastline from database\n");
        free(loc_ids);
        free(types);
        free(latlon_rad[0]);
        free(latlon_rad[1]);
        return SQLITE_ERROR;
    }

    double *D = NULL;
    int *F = NULL;
    rc = compute_distance_matrix(n, latlon_rad, types, &D, &F);
    free(coastline_data);
    if (rc != 0 || !D || !F) {
        fprintf(stderr, "Distance computation failed\n");
        free(loc_ids);
        free(types);
        free(latlon_rad[0]);
        free(latlon_rad[1]);
        return SQLITE_ERROR;
    }

    rc = sqlite3_prepare_v2(db, insert_sql, -1, &insert_stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL prepare error: %s\n", sqlite3_errmsg(db));
        free(D);
        free(F);
        cleanup_distance_matrices();
        free(loc_ids);
        free(types);
        free(latlon_rad[0]);
        free(latlon_rad[1]);
        return rc;
    }

    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
    sqlite3_exec(db, "DELETE FROM distances;", NULL, NULL, NULL);

    int stored = 0;
    int stored_paths = 0;
    int expected = n * n;

    for (i = 0; i < n; i++) {
        sqlite3_bind_int(insert_stmt, 1, loc_ids[i]);
        sqlite3_bind_int(insert_stmt, 2, loc_ids[i]);
        sqlite3_bind_double(insert_stmt, 3, 0.0);
        sqlite3_bind_int(insert_stmt, 4, 0);
        sqlite3_bind_null(insert_stmt, 5);
        if (sqlite3_step(insert_stmt) != SQLITE_DONE) {
            fprintf(stderr, "Insert error: %s\n", sqlite3_errmsg(db));
            sqlite3_finalize(insert_stmt);
            sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
            free(D);
            free(F);
            cleanup_distance_matrices();
            free(loc_ids);
            free(types);
            free(latlon_rad[0]);
            free(latlon_rad[1]);
            return SQLITE_ERROR;
        }
        sqlite3_reset(insert_stmt);
        sqlite3_clear_bindings(insert_stmt);
        stored++;

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
                fprintf(stderr, "Insert error: %s\n", sqlite3_errmsg(db));
                free(json_ij);
                free(json_ji);
                sqlite3_finalize(insert_stmt);
                sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
                free(D);
                free(F);
                cleanup_distance_matrices();
                free(loc_ids);
                free(types);
                free(latlon_rad[0]);
                free(latlon_rad[1]);
                return SQLITE_ERROR;
            }
            sqlite3_reset(insert_stmt);
            sqlite3_clear_bindings(insert_stmt);
            stored++;
            if (json_ij) stored_paths++;

            sqlite3_bind_int(insert_stmt, 1, loc_ids[j]);
            sqlite3_bind_int(insert_stmt, 2, loc_ids[i]);
            sqlite3_bind_double(insert_stmt, 3, dist_ji);
            sqlite3_bind_int(insert_stmt, 4, crosses_land_ji);
            if (json_ji) sqlite3_bind_text(insert_stmt, 5, json_ji, -1, SQLITE_TRANSIENT);
            else sqlite3_bind_null(insert_stmt, 5);
            if (sqlite3_step(insert_stmt) != SQLITE_DONE) {
                fprintf(stderr, "Insert error: %s\n", sqlite3_errmsg(db));
                free(json_ij);
                free(json_ji);
                sqlite3_finalize(insert_stmt);
                sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
                free(D);
                free(F);
                cleanup_distance_matrices();
                free(loc_ids);
                free(types);
                free(latlon_rad[0]);
                free(latlon_rad[1]);
                return SQLITE_ERROR;
            }
            sqlite3_reset(insert_stmt);
            sqlite3_clear_bindings(insert_stmt);
            stored++;
            if (json_ji) stored_paths++;

            free(json_ij);
            free(json_ji);
        }
    }

    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
    sqlite3_finalize(insert_stmt);

    printf("Stored %d distance rows\n", stored);
    printf("Expected rows: %d\n", expected);
    printf("Stored routed waypoint paths: %d\n", stored_paths);

    free(D);
    free(F);
    cleanup_distance_matrices();
    free(loc_ids);
    free(types);
    free(latlon_rad[0]);
    free(latlon_rad[1]);
    return SQLITE_OK;
}

int distance_builder_run(int argc, char **argv) {
    const char *db_path = "../../../dat/gsp_data.db";

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "--db") == 0) && i + 1 < argc) {
            db_path = argv[++i];
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 1;
        } else {
            db_path = argv[i];
        }
    }

    sqlite3 *db = NULL;
    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }

    printf("=== GSP Distances ===\n");
    printf("Database: %s\n", db_path);
    printf("Routing graph includes waypoints; distances table stores all N x N pairs.\n");
    printf("Dijkstra is used only when both endpoints are non-waypoints.\n");

    int rc = compute_and_store_distances(db);
    if (rc == SQLITE_OK) rc = report_waypoint_usage(db);
    sqlite3_close(db);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Distance build failed\n");
        return 1;
    }

    printf("Distance build complete\n");
    return 0;
}

#ifndef GSP_LIBRARY_ONLY
int main(int argc, char **argv) {
    return distance_builder_run(argc, argv);
}
#endif
