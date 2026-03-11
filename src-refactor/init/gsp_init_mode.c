/* GSP Phase 0: Initialization Solver - Common Entry Point
 *
 * This is the common entry point for all initialization strategies.
 * Currently implements: NN (Nearest Neighbor)
 *
 * Responsibilities:
 * 1. Parse command-line arguments
 * 2. Load all stations and ports from database
 * 3. Load boat information (capacity, start/end locations)
 * 4. Pre-load distance matrix from database
 * 5. Call strategy-specific solver (e.g., nn_solve)
 * 6. Output results to JSON in survey format
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>

#include "nearest_neighbor.h"
#include "../include/feasibility.h"

#define MAX_LINE 1024

typedef struct {
    int id;
    double lat;
    double lon;
    double amount;
    int is_port;
} node_t;

/* Read boat ID from YAML config */
static int read_boat_id_from_yaml(const char *yaml_path) {
    FILE *fp = fopen(yaml_path, "r");
    if (!fp) {
        fprintf(stderr, "Warning: Cannot open %s, using default boat_id=2\n", yaml_path);
        return 2;
    }

    char line[MAX_LINE];
    int boat_id = 2;

    while (fgets(line, MAX_LINE, fp)) {
        if (strstr(line, "boat:")) {
            while (fgets(line, MAX_LINE, fp)) {
                if (strstr(line, "id:")) {
                    boat_id = atoi(line + strcspn(line, "0123456789"));
                    break;
                }
                if (line[0] != ' ' && line[0] != '\t') break;
            }
            break;
        }
    }

    fclose(fp);
    return boat_id;
}

/* Load all stations and ports from database */
static int load_nodes(sqlite3 *db, nn_instance_t *inst) {
    sqlite3_stmt *stmt;

    /* Count stations and ports */
    const char *count_sql =
        "SELECT (SELECT COUNT(*) FROM stations) as num_stat, "
        "       (SELECT COUNT(*) FROM ports) as num_port";

    sqlite3_prepare_v2(db, count_sql, -1, &stmt, NULL);
    int num_stations = 0, num_ports = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        num_stations = sqlite3_column_int(stmt, 0);
        num_ports = sqlite3_column_int(stmt, 1);
    }
    sqlite3_finalize(stmt);

    inst->num_stations = num_stations;
    inst->num_ports = num_ports;
    int total_nodes = num_stations + num_ports;

    printf("[LOAD] Found %d stations and %d ports\n", num_stations, num_ports);

    if (num_stations == 0) {
        fprintf(stderr, "ERROR: No stations found in database\n");
        return -1;
    }

    /* Allocate nodes array */
    inst->nodes = (nn_node_t*)malloc(total_nodes * sizeof(nn_node_t));
    if (!inst->nodes) {
        fprintf(stderr, "ERROR: Memory allocation failed\n");
        return -1;
    }

    /* Load all stations (table id + start/end location ids) */
    const char *stations_sql =
        "SELECT s.id, s.start_location_id, s.end_location_id, s.amount "
        "FROM stations s "
        "ORDER BY s.id";

    sqlite3_prepare_v2(db, stations_sql, -1, &stmt, NULL);
    int idx = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && idx < num_stations) {
        inst->nodes[idx].table_id = sqlite3_column_int(stmt, 0);
        inst->nodes[idx].start_loc_id = sqlite3_column_int(stmt, 1);
        inst->nodes[idx].end_loc_id = sqlite3_column_int(stmt, 2);
        inst->nodes[idx].amount = sqlite3_column_double(stmt, 3);
        inst->nodes[idx].is_port = 0;
        idx++;
    }
    sqlite3_finalize(stmt);

    /* Load all ports (table id + single location id) */
    const char *ports_sql =
        "SELECT p.id, p.location_id "
        "FROM ports p "
        "ORDER BY p.id";

    sqlite3_prepare_v2(db, ports_sql, -1, &stmt, NULL);
    int port_idx = num_stations;
    while (sqlite3_step(stmt) == SQLITE_ROW && port_idx < total_nodes) {
        int port_id = sqlite3_column_int(stmt, 0);
        int loc_id = sqlite3_column_int(stmt, 1);
        inst->nodes[port_idx].table_id = port_id;
        inst->nodes[port_idx].start_loc_id = loc_id;
        inst->nodes[port_idx].end_loc_id = loc_id;
        inst->nodes[port_idx].amount = 0.0;
        inst->nodes[port_idx].is_port = 1;
        port_idx++;
    }
    sqlite3_finalize(stmt);

    printf("[LOAD] Loaded %d stations + %d ports\n", num_stations, num_ports);
    return 0;
}

/* Pre-load distance matrix from database */
static int load_distance_matrix(sqlite3 *db, nn_instance_t *inst) {
    sqlite3_stmt *stmt;

    /* Find max location ID to size the matrix */
    const char *max_sql = "SELECT MAX(id) FROM locations";
    sqlite3_prepare_v2(db, max_sql, -1, &stmt, NULL);
    int max_loc_id = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        max_loc_id = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    inst->max_loc_id = max_loc_id + 1;

    printf("[LOAD] Max location ID: %d\n", max_loc_id);

    /* Allocate distance matrix (indexed by location ID) */
    inst->distances = (double**)malloc(inst->max_loc_id * sizeof(double*));
    inst->loc_to_idx = (int*)malloc(inst->max_loc_id * sizeof(int));

    for (int i = 0; i < inst->max_loc_id; i++) {
        inst->distances[i] = (double*)malloc(inst->max_loc_id * sizeof(double));
        inst->loc_to_idx[i] = i;  /* Direct mapping for now */
    }

    /* Initialize all distances to -1 (not set) */
    for (int i = 0; i < inst->max_loc_id; i++) {
        for (int j = 0; j < inst->max_loc_id; j++) {
            inst->distances[i][j] = -1.0;
        }
    }

    /* Load distances from database */
    const char *dist_sql = "SELECT from_location_id, to_location_id, distance_nm FROM distances";
    sqlite3_prepare_v2(db, dist_sql, -1, &stmt, NULL);

    int dist_count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int from_id = sqlite3_column_int(stmt, 0);
        int to_id = sqlite3_column_int(stmt, 1);
        double dist = sqlite3_column_double(stmt, 2);

        if (from_id < inst->max_loc_id && to_id < inst->max_loc_id) {
            inst->distances[from_id][to_id] = dist;
            dist_count++;
        }
    }
    sqlite3_finalize(stmt);

    printf("[LOAD] Loaded %d distance entries\n", dist_count);
    return 0;
}

static int append_int_local(int **arr, int *n, int *cap, int v) {
    if (!arr || !n || !cap) return 0;
    if (*n >= *cap) {
        int new_cap = (*cap == 0) ? 16 : (*cap * 2);
        int *tmp = (int*)realloc(*arr, (size_t)new_cap * sizeof(int));
        if (!tmp) return 0;
        *arr = tmp;
        *cap = new_cap;
    }
    (*arr)[(*n)++] = v;
    return 1;
}

static int append_unique_int_local(int **arr, int *n, int *cap, int v) {
    if (!arr || !n || !cap) return 0;
    for (int i = 0; i < *n; i++) {
        if ((*arr)[i] == v) return 1;
    }
    return append_int_local(arr, n, cap, v);
}

static int parse_waypoint_path_json_local(const char *json_text, int **out_ids) {
    int *ids = NULL;
    int count = 0;
    const char *p;

    if (out_ids) *out_ids = NULL;
    if (!json_text || !out_ids) return 0;

    p = json_text;
    while (*p) {
        char *endptr;
        long val;
        while (*p && !((*p >= '0' && *p <= '9') || *p == '-')) p++;
        if (!*p) break;
        val = strtol(p, &endptr, 10);
        if (endptr == p) break;
        {
            int *tmp = (int*)realloc(ids, (size_t)(count + 1) * sizeof(int));
            if (!tmp) {
                free(ids);
                *out_ids = NULL;
                return 0;
            }
            ids = tmp;
            ids[count++] = (int)val;
        }
        p = endptr;
    }

    *out_ids = ids;
    return count;
}

static int lookup_waypoint_path_local(sqlite3 *db, int from_loc_id, int to_loc_id, int **out_ids) {
    static const char *sql =
        "SELECT waypoint_path FROM distances WHERE from_location_id = ? AND to_location_id = ?;";
    sqlite3_stmt *stmt = NULL;
    int count = 0;

    if (out_ids) *out_ids = NULL;
    if (!db || !out_ids || from_loc_id <= 0 || to_loc_id <= 0) return 0;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, from_loc_id);
        sqlite3_bind_int(stmt, 2, to_loc_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *txt = sqlite3_column_text(stmt, 0);
            if (txt) count = parse_waypoint_path_json_local((const char*)txt, out_ids);
            sqlite3_finalize(stmt);
            return count;
        }
        sqlite3_finalize(stmt);
    }

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, to_loc_id);
        sqlite3_bind_int(stmt, 2, from_loc_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *txt = sqlite3_column_text(stmt, 0);
            if (txt) {
                count = parse_waypoint_path_json_local((const char*)txt, out_ids);
                for (int i = 0; i < count / 2; i++) {
                    int tmp = (*out_ids)[i];
                    (*out_ids)[i] = (*out_ids)[count - 1 - i];
                    (*out_ids)[count - 1 - i] = tmp;
                }
            }
        }
        sqlite3_finalize(stmt);
    }

    return count;
}

/* Write NN solution to JSON in survey format */
static void write_json(sqlite3 *db, const char *output_path, const nn_instance_t *inst,
                       const nn_solution_t *sol, int boat_id,
                       const char *boat_name,
                       int boat_start_loc_id, int boat_end_loc_id,
                       double boat_capacity,
                       double boat_start_lat, double boat_start_lon,
                       int is_feasible) {
    FILE *fp = fopen(output_path, "w");
    if (!fp) {
        perror("Cannot open output file");
        return;
    }

    fprintf(fp, "{\n");
    fprintf(fp, "  \"metadata\": {\n");
    fprintf(fp, "    \"solver_version\": \"init_nn_1.0\",\n");
    fprintf(fp, "    \"timestamp\": \"%ld\",\n", (long)time(NULL));
    fprintf(fp, "    \"mode\": \"init_nn\",\n");
    fprintf(fp, "    \"strategy\": \"nn\",\n");
    fprintf(fp, "    \"boat_id\": %d,\n", boat_id);
    fprintf(fp, "    \"boat_name\": \"%s\",\n", boat_name ? boat_name : "Unknown");
    fprintf(fp, "    \"home_port\": {\"lat\": %.6f, \"lon\": %.6f},\n", boat_start_lat, boat_start_lon);
    fprintf(fp, "    \"boat_location_ids\": [%d, %d]\n", boat_start_loc_id, boat_end_loc_id);
    fprintf(fp, "  },\n");

    fprintf(fp, "  \"problem\": {\n");
    fprintf(fp, "    \"num_nodes\": %d,\n", sol->tour_length);
    fprintf(fp, "    \"num_stations\": %d,\n", inst->num_stations);
    fprintf(fp, "    \"capacity\": %.0f\n", boat_capacity);
    fprintf(fp, "  },\n");

    int *unique_waypoint_location_ids = NULL;
    int uniq_wp_n = 0, uniq_wp_cap = 0;

    fprintf(fp, "  \"solution\": {\n");

    /* Output segments: tour_segments_location_ids includes all visited locations with waypoint expansion */
    fprintf(fp, "    \"tour_segments_location_ids\": [\n");
    for (int s = 0; s < sol->segment_count; s++) {
        fprintf(fp, "      [");
        int start = sol->segment_starts[s];
        int end = sol->segment_ends[s];

        int base_cap = (end - start + 1) + 2;
        int *base = (int*)malloc((size_t)base_cap * sizeof(int));
        int base_n = 0;
        if (!base) {
            fclose(fp);
            free(unique_waypoint_location_ids);
            return;
        }

        /* Start boundary: boat start for first segment, prior segment end for others. */
        base[base_n++] = (s == 0) ? boat_start_loc_id : sol->tour[sol->segment_ends[s - 1]];
        for (int i = start; i <= end; i++) {
            base[base_n++] = sol->tour[i];
        }
        /* End boundary: boat end only for final segment; intermediate segments already end at a port. */
        if (s == sol->segment_count - 1 && (base_n == 0 || base[base_n - 1] != boat_end_loc_id)) {
            base[base_n++] = boat_end_loc_id;
        }

        if (base_n > 0) {
            fprintf(fp, "%d", base[0]);
            for (int i = 0; i < base_n - 1; i++) {
                int from_loc = base[i];
                int to_loc = base[i + 1];
                int *wps = NULL;
                int n_wps = lookup_waypoint_path_local(db, from_loc, to_loc, &wps);
                if (n_wps > 0) {
                    for (int k = 0; k < n_wps; k++) {
                        fprintf(fp, ", %d", wps[k]);
                        (void)append_unique_int_local(&unique_waypoint_location_ids, &uniq_wp_n, &uniq_wp_cap, wps[k]);
                    }
                }
                fprintf(fp, ", %d", to_loc);
                free(wps);
            }
        }

        free(base);
        fprintf(fp, "]%s\n", (s + 1 < sol->segment_count) ? "," : "");
    }
    fprintf(fp, "    ],\n");

    fprintf(fp, "    \"dock_location_ids\": [%d, %d],\n", boat_start_loc_id, boat_end_loc_id);
    fprintf(fp, "    \"unique_waypoint_location_ids\": [");
    for (int i = 0; i < uniq_wp_n; i++) {
        if (i) fprintf(fp, ", ");
        fprintf(fp, "%d", unique_waypoint_location_ids[i]);
    }
    fprintf(fp, "],\n");

    /* Output segments: station IDs only (never ports/boats) */
    fprintf(fp, "    \"tour_segments_station_ids\": [\n");
    for (int s = 0; s < sol->segment_count; s++) {
        fprintf(fp, "      [");
        int first = 1;
        for (int i = 0; i < sol->visit_station_count; i++) {
            if (sol->visit_station_segment[i] == s) {
                if (!first) fprintf(fp, ", ");
                fprintf(fp, "%d", sol->visit_station_ids[i]);
                first = 0;
            }
        }
        fprintf(fp, "]%s\n", (s + 1 < sol->segment_count) ? "," : "");
    }
    fprintf(fp, "    ],\n");

    fprintf(fp, "    \"tour_length\": [");
    for (int s = 0; s < sol->segment_count; s++) {
        fprintf(fp, "%d", sol->segment_ends[s] - sol->segment_starts[s] + 1);
        if (s + 1 < sol->segment_count) fprintf(fp, ", ");
    }
    fprintf(fp, "],\n");

    fprintf(fp, "    \"segment_count\": %d,\n", sol->segment_count);

    fprintf(fp, "    \"segment_catch_amount\": [");
    for (int s = 0; s < sol->segment_count; s++) {
        fprintf(fp, "%d", sol->segment_catches[s]);
        if (s + 1 < sol->segment_count) fprintf(fp, ", ");
    }
    fprintf(fp, "],\n");

    fprintf(fp, "    \"segment_distance_nm\": [");
    for (int s = 0; s < sol->segment_count; s++) {
        double seg_nm = sol->segment_dists[s];
        if (s == sol->segment_count - 1 && sol->tour_length > 0) {
            double final_leg = inst->distances[sol->tour[sol->tour_length - 1]][boat_end_loc_id];
            if (final_leg > 0.0) seg_nm += final_leg;
        }
        fprintf(fp, "%.2f", seg_nm);
        if (s + 1 < sol->segment_count) fprintf(fp, ", ");
    }
    fprintf(fp, "],\n");

    fprintf(fp, "    \"total_distance_nm\": %.2f,\n", sol->total_distance);
    fprintf(fp, "    \"feasible\": %s\n", is_feasible ? "true" : "false");
    fprintf(fp, "  },\n");

    fprintf(fp, "  \"solver_stats\": {\n");
    fprintf(fp, "    \"status\": \"init_complete\",\n");
    fprintf(fp, "    \"runtime_seconds\": 0.0,\n");
    fprintf(fp, "    \"method\": \"nearest_neighbor\"\n");
    fprintf(fp, "  }\n");

    fprintf(fp, "}\n");

    fclose(fp);
    free(unique_waypoint_location_ids);
    printf("[OUTPUT] Solution written to %s\n", output_path);
}

/* Debug writer: metadata/problem only, no solution tour. */
static void write_metadata_only_json(const char *output_path,
                                     int boat_id,
                                     const char *boat_name,
                                     int boat_start_loc_id,
                                     int boat_end_loc_id,
                                     double boat_capacity,
                                     double boat_start_lat,
                                     double boat_start_lon,
                                     int num_stations,
                                     int num_ports) {
    FILE *fp = fopen(output_path, "w");
    if (!fp) {
        perror("Cannot open output file");
        return;
    }

    fprintf(fp, "{\n");
    fprintf(fp, "  \"metadata\": {\n");
    fprintf(fp, "    \"solver_version\": \"init_nn_debug_meta_1.0\",\n");
    fprintf(fp, "    \"timestamp\": \"%ld\",\n", (long)time(NULL));
    fprintf(fp, "    \"mode\": \"init_nn\",\n");
    fprintf(fp, "    \"strategy\": \"nn\",\n");
    fprintf(fp, "    \"boat_id\": %d,\n", boat_id);
    fprintf(fp, "    \"boat_name\": \"%s\",\n", boat_name ? boat_name : "Unknown");
    fprintf(fp, "    \"home_port\": {\"lat\": %.6f, \"lon\": %.6f},\n", boat_start_lat, boat_start_lon);
    fprintf(fp, "    \"boat_location_ids\": [%d, %d],\n", boat_start_loc_id, boat_end_loc_id);
    fprintf(fp, "    \"debug_solver_skipped\": true\n");
    fprintf(fp, "  },\n");

    fprintf(fp, "  \"problem\": {\n");
    fprintf(fp, "    \"num_stations\": %d,\n", num_stations);
    fprintf(fp, "    \"num_ports\": %d,\n", num_ports);
    fprintf(fp, "    \"capacity\": %.0f\n", boat_capacity);
    fprintf(fp, "  },\n");

    fprintf(fp, "  \"solution\": {\n");
    fprintf(fp, "    \"tour_segments_location_ids\": [],\n");
    fprintf(fp, "    \"tour_segments_station_ids\": [],\n");
    fprintf(fp, "    \"tour_length\": [],\n");
    fprintf(fp, "    \"segment_count\": 0,\n");
    fprintf(fp, "    \"segment_catch_amount\": [],\n");
    fprintf(fp, "    \"segment_distance_nm\": [],\n");
    fprintf(fp, "    \"total_distance_nm\": 0.0,\n");
    fprintf(fp, "    \"feasible\": false\n");
    fprintf(fp, "  },\n");

    fprintf(fp, "  \"solver_stats\": {\n");
    fprintf(fp, "    \"status\": \"debug_metadata_only\",\n");
    fprintf(fp, "    \"runtime_seconds\": 0.0,\n");
    fprintf(fp, "    \"method\": \"none\"\n");
    fprintf(fp, "  }\n");
    fprintf(fp, "}\n");

    fclose(fp);
    printf("[OUTPUT] Metadata-only JSON written to %s\n", output_path);
}

/* Parse command-line arguments */
static void parse_args(int argc, char **argv,
                       const char **strategy, const char **database,
                       const char **config, const char **output) {
    *strategy = NULL;
    *database = NULL;
    *config = NULL;
    *output = NULL;

    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--strategy") == 0) {
            *strategy = argv[i + 1];
        } else if (strcmp(argv[i], "--database") == 0) {
            *database = argv[i + 1];
        } else if (strcmp(argv[i], "--config") == 0) {
            *config = argv[i + 1];
        } else if (strcmp(argv[i], "--output") == 0) {
            *output = argv[i + 1];
        }
    }
}

/* Main entry point */
int mode_init(int argc, char **argv) {
    printf("============================================================\n");
    printf("GSP Solver - Phase 0: Initialization\n");
    printf("============================================================\n\n");

    const char *strategy, *database, *config, *output;
    parse_args(argc, argv, &strategy, &database, &config, &output);

    if (!strategy || !database || !config) {
        fprintf(stderr, "ERROR: Missing required arguments\n");
        fprintf(stderr, "Usage: gsp --mode init --strategy nn --database <db> --config <yaml> --output <json>\n");
        return 1;
    }

    if (strcmp(strategy, "nn") != 0) {
        fprintf(stderr, "ERROR: Only 'nn' strategy is currently implemented\n");
        return 1;
    }

    int boat_id = read_boat_id_from_yaml(config);
    printf("Strategy: %s\n", strategy);
    printf("Database: %s\n", database);
    printf("Config: %s\n", config);
    printf("Output: %s\n", output);
    printf("Boat ID: %d\n\n", boat_id);

    /* Open database */
    sqlite3 *db;
    if (sqlite3_open(database, &db) != SQLITE_OK) {
        fprintf(stderr, "ERROR: Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }

    /* Load nodes (stations + ports) */
    nn_instance_t inst = {0};
    if (load_nodes(db, &inst) != 0) {
        sqlite3_close(db);
        return 1;
    }

    /* Load distance matrix */
    if (load_distance_matrix(db, &inst) != 0) {
        sqlite3_close(db);
        return 1;
    }

    /* Get boat info */
    sqlite3_stmt *stmt;
    double boat_capacity = 0.0;
    int boat_start_loc_id = 0;
    int boat_end_loc_id = 0;
    double boat_start_lat = 0.0;
    double boat_start_lon = 0.0;
    char boat_name[256] = "Unknown";

    const char *boat_sql =
        "SELECT b.name, b.capacity, b.start_location_id, b.end_location_id, l.lat, l.lon "
        "FROM boats b "
        "JOIN locations l ON l.id = b.start_location_id "
        "WHERE b.id = ?";

    sqlite3_prepare_v2(db, boat_sql, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, boat_id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *name_txt = sqlite3_column_text(stmt, 0);
        if (name_txt) snprintf(boat_name, sizeof(boat_name), "%s", (const char*)name_txt);
        boat_capacity = sqlite3_column_double(stmt, 1);
        boat_start_loc_id = sqlite3_column_int(stmt, 2);
        boat_end_loc_id = sqlite3_column_int(stmt, 3);
        boat_start_lat = sqlite3_column_double(stmt, 4);
        boat_start_lon = sqlite3_column_double(stmt, 5);
    }
    sqlite3_finalize(stmt);

    printf("[LOAD] Boat capacity: %.0f\n", boat_capacity);
    printf("[LOAD] Boat start: %d, end: %d\n\n", boat_start_loc_id, boat_end_loc_id);

    /* Solve NN */
    nn_solution_t sol = {0};
    if (nn_solve(&inst, &sol, boat_start_loc_id, boat_end_loc_id, boat_capacity) != 0) {
        fprintf(stderr, "ERROR: Failed to solve\n");
        sqlite3_close(db);
        return 1;
    }

    /* Write JSON output */
    printf("\n");
    int is_feasible = 1;
    if (!stations_have_no_duplicates(sol.visit_station_ids, sol.visit_station_count)) {
        is_feasible = 0;
    }
    if (!segments_within_capacity(sol.segment_catches, sol.segment_count, boat_capacity)) {
        is_feasible = 0;
    }

    write_json(db, output, &inst, &sol, boat_id, boat_name, boat_start_loc_id, boat_end_loc_id, boat_capacity,
               boat_start_lat, boat_start_lon, is_feasible);

    printf("\n[SUCCESS] Initialization complete!\n");
    printf("============================================================\n");

    /* Cleanup */
    sqlite3_close(db);
    free(sol.tour);
    free(sol.segment_starts);
    free(sol.segment_ends);
    free(sol.segment_catches);
    free(sol.segment_dists);
    free(sol.visit_station_ids);
    free(sol.visit_station_segment);
    free(inst.nodes);
    for (int i = 0; i < inst.max_loc_id; i++) {
        free(inst.distances[i]);
    }
    free(inst.distances);
    free(inst.loc_to_idx);

    return 0;
}

