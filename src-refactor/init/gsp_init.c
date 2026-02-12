/* GSP Phase 0: Initialization Solver
 * Supports: OPT, NN, GE, CI strategies
 * Currently implemented: NN (Nearest Neighbor)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sqlite3.h>

#define MAX_NODES 1000
#define MAX_LINE 1024

/* Simple YAML parser - just extract boat.id */
int read_boat_id_from_yaml(const char *yaml_path) {
    FILE *fp = fopen(yaml_path, "r");
    if (!fp) {
        fprintf(stderr, "Warning: Cannot open %s, using default boat_id=2\n", yaml_path);
        return 2;
    }

    char line[MAX_LINE];
    int in_boat_section = 0;
    int boat_id = 2;  // default

    while (fgets(line, MAX_LINE, fp)) {
        // Remove leading whitespace
        char *trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

        // Check for boat section
        if (strncmp(trimmed, "boat:", 5) == 0) {
            in_boat_section = 1;
            continue;
        }

        // If in boat section, look for id
        if (in_boat_section) {
            if (strncmp(trimmed, "id:", 3) == 0) {
                char *value = trimmed + 3;
                while (*value == ' ' || *value == '\t') value++;
                boat_id = atoi(value);
                break;
            }
            // Exit boat section if we hit another top-level key
            if (trimmed[0] != ' ' && trimmed[0] != '\t' && trimmed[0] != '#' && trimmed[0] != '\n') {
                in_boat_section = 0;
            }
        }
    }

    fclose(fp);
    return boat_id;
}

typedef struct {
    int id;
    double lat;
    double lon;
    int type;  // 0=boat, 1=station, 2=port, 3=waypoint
    double amount;
} node_t;

typedef struct {
    int num_nodes;
    node_t *nodes;
    double **dist_matrix;
} instance_t;

typedef struct {
    int *tour;
    int tour_length;
    double total_distance;
    double runtime_seconds;
} solution_t;

/* Haversine distance in nautical miles */
double haversine_nm(double lat1, double lon1, double lat2, double lon2) {
    double R = 3440.065;  // Earth radius in nautical miles
    double dLat = (lat2 - lat1) * M_PI / 180.0;
    double dLon = (lon2 - lon1) * M_PI / 180.0;
    double a = sin(dLat/2) * sin(dLat/2) +
               cos(lat1 * M_PI / 180.0) * cos(lat2 * M_PI / 180.0) *
               sin(dLon/2) * sin(dLon/2);
    double c = 2 * atan2(sqrt(a), sqrt(1-a));
    return R * c;
}

/* Load instance from database */
int load_instance(const char *db_path, int boat_id, instance_t *inst) {
    sqlite3 *db;
    sqlite3_stmt *stmt;
    int rc = sqlite3_open(db_path, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    // Count total nodes
    const char *count_sql =
        "SELECT COUNT(*) FROM ("
        "  SELECT id FROM locations WHERE id IN ("
        "    SELECT location_id FROM boats WHERE id = ?"
        "  )"
        "  UNION ALL"
        "  SELECT start_location_id FROM stations"
        "  UNION ALL"
        "  SELECT location_id FROM ports"
        ");";

    sqlite3_prepare_v2(db, count_sql, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, boat_id);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        inst->num_nodes = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    printf("[LOADING] Estimated nodes: %d\n", inst->num_nodes);

    // Allocate
    inst->nodes = (node_t*)malloc(inst->num_nodes * sizeof(node_t));
    inst->dist_matrix = (double**)malloc(inst->num_nodes * sizeof(double*));
    for (int i = 0; i < inst->num_nodes; i++) {
        inst->dist_matrix[i] = (double*)malloc(inst->num_nodes * sizeof(double));
    }

    // Load boat location
    const char *boat_sql =
        "SELECT l.id, l.lat, l.lon "
        "FROM boats b "
        "JOIN locations l ON b.location_id = l.id "
        "WHERE b.id = ?";

    sqlite3_prepare_v2(db, boat_sql, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, boat_id);

    int idx = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        inst->nodes[idx].id = sqlite3_column_int(stmt, 0);
        inst->nodes[idx].lat = sqlite3_column_double(stmt, 1);
        inst->nodes[idx].lon = sqlite3_column_double(stmt, 2);
        inst->nodes[idx].type = 0;  // boat
        inst->nodes[idx].amount = 0.0;
        idx++;
    }
    sqlite3_finalize(stmt);

    // Load stations
    const char *stations_sql =
        "SELECT s.id, start.lat, start.lon, s.amount "
        "FROM stations s "
        "JOIN locations start ON s.start_location_id = start.id";

    sqlite3_prepare_v2(db, stations_sql, -1, &stmt, NULL);

    while (sqlite3_step(stmt) == SQLITE_ROW && idx < inst->num_nodes) {
        inst->nodes[idx].id = sqlite3_column_int(stmt, 0);
        inst->nodes[idx].lat = sqlite3_column_double(stmt, 1);
        inst->nodes[idx].lon = sqlite3_column_double(stmt, 2);
        inst->nodes[idx].amount = sqlite3_column_double(stmt, 3);
        inst->nodes[idx].type = 1;  // station
        idx++;
    }
    sqlite3_finalize(stmt);

    inst->num_nodes = idx;
    printf("[LOADING] Loaded %d nodes (1 boat, %d stations)\n", inst->num_nodes, inst->num_nodes - 1);

    // Build distance matrix
    printf("[BUILDING] Distance matrix...\n");
    for (int i = 0; i < inst->num_nodes; i++) {
        for (int j = 0; j < inst->num_nodes; j++) {
            if (i == j) {
                inst->dist_matrix[i][j] = 0.0;
            } else {
                inst->dist_matrix[i][j] = haversine_nm(
                    inst->nodes[i].lat, inst->nodes[i].lon,
                    inst->nodes[j].lat, inst->nodes[j].lon
                );
            }
        }
    }

    sqlite3_close(db);
    return 0;
}

/* Nearest Neighbor Heuristic */
int solve_nearest_neighbor(const instance_t *inst, solution_t *sol) {
    clock_t start = clock();

    sol->tour = (int*)malloc(inst->num_nodes * sizeof(int));
    sol->tour_length = 0;
    sol->total_distance = 0.0;

    int *visited = (int*)calloc(inst->num_nodes, sizeof(int));

    // Start at boat (node 0)
    int current = 0;
    sol->tour[sol->tour_length++] = current;
    visited[current] = 1;

    printf("[NN] Starting from boat (node %d)\n", current);

    // Greedily select nearest unvisited node
    while (sol->tour_length < inst->num_nodes) {
        double min_dist = 1e100;
        int nearest = -1;

        for (int i = 0; i < inst->num_nodes; i++) {
            if (!visited[i] && inst->dist_matrix[current][i] < min_dist) {
                min_dist = inst->dist_matrix[current][i];
                nearest = i;
            }
        }

        if (nearest == -1) break;

        sol->tour[sol->tour_length++] = nearest;
        visited[nearest] = 1;
        sol->total_distance += min_dist;

        current = nearest;
    }

    // Return to start
    sol->total_distance += inst->dist_matrix[current][0];

    free(visited);

    clock_t end = clock();
    sol->runtime_seconds = (double)(end - start) / CLOCKS_PER_SEC;

    printf("[NN] Tour length: %d nodes\n", sol->tour_length);
    printf("[NN] Total distance: %.2f nm\n", sol->total_distance);
    printf("[NN] Runtime: %.3f seconds\n", sol->runtime_seconds);

    return 0;
}

/* Write solution to JSON */
void write_json(const char *output_path, const instance_t *inst, const solution_t *sol, int boat_id, const char *strategy) {
    FILE *fp = fopen(output_path, "w");
    if (!fp) {
        perror("Cannot open output file");
        return;
    }

    fprintf(fp, "{\n");
    fprintf(fp, "  \"metadata\": {\n");
    fprintf(fp, "    \"solver_version\": \"1.0.0\",\n");
    fprintf(fp, "    \"timestamp\": \"%ld\",\n", time(NULL));
    fprintf(fp, "    \"mode\": \"init\",\n");
    fprintf(fp, "    \"strategy\": \"%s\",\n", strategy);
    fprintf(fp, "    \"boat_id\": %d\n", boat_id);
    fprintf(fp, "  },\n");

    fprintf(fp, "  \"problem\": {\n");
    fprintf(fp, "    \"num_nodes\": %d,\n", inst->num_nodes);
    fprintf(fp, "    \"num_stations\": %d\n", inst->num_nodes - 1);
    fprintf(fp, "  },\n");

    fprintf(fp, "  \"solution\": {\n");
    fprintf(fp, "    \"tour\": [");
    for (int i = 0; i < sol->tour_length; i++) {
        fprintf(fp, "%d", sol->tour[i]);
        if (i < sol->tour_length - 1) fprintf(fp, ", ");
    }
    fprintf(fp, "],\n");
    fprintf(fp, "    \"tour_length\": %d,\n", sol->tour_length);
    fprintf(fp, "    \"total_distance_nm\": %.2f,\n", sol->total_distance);
    fprintf(fp, "    \"feasible\": true\n");
    fprintf(fp, "  },\n");

    fprintf(fp, "  \"solver_stats\": {\n");
    fprintf(fp, "    \"status\": \"completed\",\n");
    fprintf(fp, "    \"runtime_seconds\": %.3f,\n", sol->runtime_seconds);
    fprintf(fp, "    \"method\": \"%s_heuristic\"\n", strategy);
    fprintf(fp, "  }\n");

    fprintf(fp, "}\n");

    fclose(fp);
    printf("[OUTPUT] Solution written to %s\n", output_path);
}

int main(int argc, char **argv) {
    printf("============================================================\n");
    printf("GSP Solver - Phase 0: Initialization\n");
    printf("============================================================\n\n");

    if (argc < 4) {
        fprintf(stderr, "Usage: %s <strategy> <database.db> <output.json> [config.yaml]\n", argv[0]);
        fprintf(stderr, "\nStrategies:\n");
        fprintf(stderr, "  nn   - Nearest Neighbor (implemented)\n");
        fprintf(stderr, "  ge   - Greedy Edge (not yet implemented)\n");
        fprintf(stderr, "  ci   - Cheapest Insertion (not yet implemented)\n");
        fprintf(stderr, "  opt  - Optimal NP-MIP (not yet implemented)\n");
        fprintf(stderr, "\nExample:\n");
        fprintf(stderr, "  %s nn dat/gsp_data.db sol/init_nn.json config/gsp_solver.yaml\n", argv[0]);
        return 1;
    }

    const char *strategy = argv[1];
    const char *db_path = argv[2];
    const char *output_path = argv[3];
    const char *config_path = (argc > 4) ? argv[4] : "config/gsp_solver.yaml";

    // Validate strategy
    if (strcmp(strategy, "nn") != 0 && strcmp(strategy, "ge") != 0 &&
        strcmp(strategy, "ci") != 0 && strcmp(strategy, "opt") != 0) {
        fprintf(stderr, "ERROR: Unknown strategy '%s'\n", strategy);
        fprintf(stderr, "Valid strategies: nn, ge, ci, opt\n");
        return 1;
    }

    // Check if strategy is implemented
    if (strcmp(strategy, "ge") == 0 || strcmp(strategy, "ci") == 0 || strcmp(strategy, "opt") == 0) {
        fprintf(stderr, "ERROR: Strategy '%s' is not yet implemented\n", strategy);
        fprintf(stderr, "Currently implemented: nn (Nearest Neighbor)\n");
        fprintf(stderr, "\nComing soon:\n");
        fprintf(stderr, "  - ge: Greedy Edge construction\n");
        fprintf(stderr, "  - ci: Cheapest Insertion heuristic\n");
        fprintf(stderr, "  - opt: Optimal NP-MIP solver\n");
        return 1;
    }

    // Read boat_id from YAML
    int boat_id = read_boat_id_from_yaml(config_path);

    printf("Strategy: %s (Nearest Neighbor)\n", strategy);
    printf("Config: %s\n", config_path);
    printf("Database: %s\n", db_path);
    printf("Output: %s\n", output_path);
    printf("Boat ID: %d (from config)\n\n", boat_id);

    instance_t inst;
    if (load_instance(db_path, boat_id, &inst) != 0) {
        fprintf(stderr, "Failed to load instance\n");
        return 1;
    }

    solution_t sol;
    if (solve_nearest_neighbor(&inst, &sol) != 0) {
        fprintf(stderr, "Failed to solve\n");
        return 1;
    }

    write_json(output_path, &inst, &sol, boat_id, strategy);

    printf("\n[SUCCESS] Initialization (%s) completed!\n", strategy);
    printf("============================================================\n");

    // Cleanup
    free(sol.tour);
    free(inst.nodes);
    for (int i = 0; i < inst.num_nodes; i++) {
        free(inst.dist_matrix[i]);
    }
    free(inst.dist_matrix);

    return 0;
}






