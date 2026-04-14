/* GSP Construction / Segment Solver - Common Entry Point
 *
 * This executable stage handles:
 * 1. heuristic construction generation for nn / ge / ci
 * 2. segment-local post-optimization when writing segment.json
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <math.h>
#include <sqlite3.h>

/* Helper: get elapsed seconds from two timespec structs */
static double elapsed_seconds(struct timespec start, struct timespec end) {
    return (double)(end.tv_sec - start.tv_sec) +
           (double)(end.tv_nsec - start.tv_nsec) / 1e9;
}

#include "construction_nn.h"
#include "construction_ge.h"
#include "construction_ci.h"
#include "segment_postopt.h"
#include "../include/feasibility.h"
#include "../include/json_utils.h"
#include "../include/mip_report.h"

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

static int read_init_capacity_slack_from_yaml(const char *yaml_path) {
    FILE *fp = fopen(yaml_path, "r");
    if (!fp) {
        fprintf(stderr, "Warning: Cannot open %s, using default slack=0\n", yaml_path);
        return 0;
    }

    char line[MAX_LINE];
    int in_init = 0, in_nn = 0;
    int slack = 0;
    int slack_found = 0;

    while (fgets(line, MAX_LINE, fp)) {
        char *trimmed = line;
        while (*trimmed && isspace((unsigned char)*trimmed)) trimmed++;

        if (*trimmed == '#' || *trimmed == '\0' || *trimmed == '\n') continue;

        if (strncmp(trimmed, "init:", 5) == 0) {
            in_init = 1;
            in_nn = 0;
            continue;
        }
        if (!isspace((unsigned char)line[0]) && strncmp(trimmed, "init:", 5) != 0) {
            in_init = 0;
            in_nn = 0;
        }
        if (in_init && strncmp(trimmed, "nn:", 3) == 0) {
            in_nn = 1;
            continue;
        }
        if (in_init && strncmp(trimmed, "target_catch_slack_kg:", 22) == 0) {
            slack = atoi(trimmed + 22);
            slack_found = 1;
            continue;
        }
        if (in_nn && line[0] != ' ' && line[0] != '\t') {
            in_nn = 0;
        }
        if (!slack_found && in_nn && strncmp(trimmed, "target_catch_slack_kg:", 22) == 0) {
            slack = atoi(trimmed + 22);
            slack_found = 1;
        }
    }

    fclose(fp);
    return slack;
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
        inst->nodes[idx].amount = sqlite3_column_int(stmt, 3);
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

static int output_path_is_construction_json(const char *output_path) {
    const char *base = output_path;
    const char *slash;
    if (!output_path) return 0;
    slash = strrchr(output_path, '/');
    if (slash && slash[1] != '\0') base = slash + 1;
    slash = strrchr(base, '\\');
    if (slash && slash[1] != '\0') base = slash + 1;
    return strcmp(base, "construction.json") == 0;
}

static int is_port_location_id(const nn_instance_t *inst, int loc_id)
{
    if (!inst) return 0;
    for (int i = inst->num_stations; i < inst->num_stations + inst->num_ports; i++) {
        if (inst->nodes[i].start_loc_id == loc_id) return 1;
    }
    return 0;
}

static int segment_solution_has_valid_boundaries(const nn_instance_t *inst, const nn_solution_t *sol)
{
    if (!inst || !sol) return 0;
    if (inst->num_stations <= 0) return sol->segment_count == 0;
    if (sol->segment_count <= 0 || !sol->tour || sol->tour_length <= 0) return 0;

    for (int s = 0; s < sol->segment_count; s++) {
        int start = sol->segment_starts[s];
        int end = sol->segment_ends[s];
        if (start < 0 || end < start || end >= sol->tour_length) return 0;
        if (is_port_location_id(inst, sol->tour[start])) return 0;
        if (s < sol->segment_count - 1 && !is_port_location_id(inst, sol->tour[end])) return 0;
    }

    return 1;
}

static int segment_solution_is_capacity_feasible(const nn_solution_t *sol, double capacity)
{
    if (!sol) return 0;
    return segments_within_capacity(sol->segment_catches, sol->segment_count, capacity);
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

typedef struct {
    int from_loc_id;
    int to_loc_id;
    int *waypoint_ids;
    int waypoint_count;
} waypoint_cache_entry_t;

typedef struct {
    waypoint_cache_entry_t *entries;
    int count;
    int capacity;
    int direct_hits;
    int reverse_hits;
    int misses;
    int total_queries;
    int preload_leg_pairs;
} waypoint_cache_t;

typedef struct {
    waypoint_cache_t *cache;
    int *leg_query_count;
    int *total_waypoint_ids;
} segment_waypoint_lookup_ctx_t;

static void waypoint_cache_destroy(waypoint_cache_t *cache) {
    if (!cache) return;
    for (int i = 0; i < cache->count; i++) {
        free(cache->entries[i].waypoint_ids);
    }
    free(cache->entries);
    memset(cache, 0, sizeof(*cache));
}

static int waypoint_cache_reserve(waypoint_cache_t *cache, int needed) {
    if (!cache) return 0;
    if (needed <= cache->capacity) return 1;
    {
        int new_cap = (cache->capacity == 0) ? 64 : cache->capacity * 2;
        while (new_cap < needed) new_cap *= 2;
        waypoint_cache_entry_t *tmp = (waypoint_cache_entry_t*)realloc(
            cache->entries, (size_t)new_cap * sizeof(waypoint_cache_entry_t));
        if (!tmp) return 0;
        cache->entries = tmp;
        cache->capacity = new_cap;
    }
    return 1;
}

static int waypoint_cache_add_entry(waypoint_cache_t *cache, int from_loc_id, int to_loc_id, const char *json_text) {
    waypoint_cache_entry_t *entry;
    int *ids = NULL;
    int n_ids = 0;

    if (!cache) return 0;
    if (!waypoint_cache_reserve(cache, cache->count + 1)) return 0;

    if (json_text) {
        n_ids = parse_waypoint_path_json_local(json_text, &ids);
    }

    entry = &cache->entries[cache->count++];
    entry->from_loc_id = from_loc_id;
    entry->to_loc_id = to_loc_id;
    entry->waypoint_ids = ids;
    entry->waypoint_count = n_ids;
    return 1;
}

static int waypoint_cache_insert_pair(sqlite3_stmt *stmt, int from_loc_id, int to_loc_id) {
    if (!stmt || from_loc_id <= 0 || to_loc_id <= 0) return 0;
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    sqlite3_bind_int(stmt, 1, from_loc_id);
    sqlite3_bind_int(stmt, 2, to_loc_id);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        return 0;
    }
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    return 1;
}

static int waypoint_cache_collect_solution_legs(sqlite3_stmt *insert_stmt,
                                                const nn_solution_t *sol,
                                                int boat_start_loc_id,
                                                int boat_end_loc_id,
                                                int *preload_leg_pairs) {
    if (!insert_stmt || !sol || !preload_leg_pairs) return 0;

    for (int s = 0; s < sol->segment_count; s++) {
        int start = sol->segment_starts[s];
        int end = sol->segment_ends[s];
        int base_cap = (end - start + 1) + 2;
        int *base = (int*)malloc((size_t)base_cap * sizeof(int));
        int base_n = 0;

        if (!base) return 0;

        base[base_n++] = (s == 0) ? boat_start_loc_id : sol->tour[sol->segment_ends[s - 1]];
        for (int i = start; i <= end; i++) {
            base[base_n++] = sol->tour[i];
        }
        if (s == sol->segment_count - 1 && (base_n == 0 || base[base_n - 1] != boat_end_loc_id)) {
            base[base_n++] = boat_end_loc_id;
        }

        for (int i = 0; i < base_n - 1; i++) {
            int from_loc = base[i];
            int to_loc = base[i + 1];
            if (!waypoint_cache_insert_pair(insert_stmt, from_loc, to_loc) ||
                !waypoint_cache_insert_pair(insert_stmt, to_loc, from_loc)) {
                free(base);
                return 0;
            }
        }

        *preload_leg_pairs += (base_n > 0) ? (base_n - 1) : 0;
        free(base);
    }

    return 1;
}

static int waypoint_cache_preload(sqlite3 *db, const nn_instance_t *inst,
                                  const nn_solution_t *sol,
                                  const nn_solution_t *extra_sol,
                                  int boat_start_loc_id, int boat_end_loc_id,
                                  waypoint_cache_t *cache) {
    sqlite3_stmt *insert_stmt = NULL;
    sqlite3_stmt *select_stmt = NULL;
    int rc = SQLITE_OK;

    if (!db || !inst || !sol || !cache) return 0;
    memset(cache, 0, sizeof(*cache));

    rc = sqlite3_exec(db,
        "CREATE TEMP TABLE IF NOT EXISTS tmp_waypoint_legs ("
        "from_location_id INTEGER NOT NULL, "
        "to_location_id INTEGER NOT NULL, "
        "PRIMARY KEY (from_location_id, to_location_id)) WITHOUT ROWID;",
        NULL, NULL, NULL);
    if (rc != SQLITE_OK) return 0;

    rc = sqlite3_exec(db, "DELETE FROM tmp_waypoint_legs;", NULL, NULL, NULL);
    if (rc != SQLITE_OK) return 0;

    rc = sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO tmp_waypoint_legs(from_location_id, to_location_id) VALUES (?, ?);",
        -1, &insert_stmt, NULL);
    if (rc != SQLITE_OK) goto cleanup;

    if (!waypoint_cache_collect_solution_legs(insert_stmt, sol,
                                              boat_start_loc_id, boat_end_loc_id,
                                              &cache->preload_leg_pairs)) {
        rc = SQLITE_NOMEM;
        goto cleanup;
    }
    if (extra_sol && extra_sol->segment_count > 0) {
        if (!waypoint_cache_collect_solution_legs(insert_stmt, extra_sol,
                                                  boat_start_loc_id, boat_end_loc_id,
                                                  &cache->preload_leg_pairs)) {
            rc = SQLITE_NOMEM;
            goto cleanup;
        }
    }

    rc = sqlite3_prepare_v2(db,
        "SELECT d.from_location_id, d.to_location_id, d.waypoint_path "
        "FROM distances d "
        "JOIN tmp_waypoint_legs t "
        "  ON t.from_location_id = d.from_location_id "
        " AND t.to_location_id = d.to_location_id;",
        -1, &select_stmt, NULL);
    if (rc != SQLITE_OK) goto cleanup;

    while ((rc = sqlite3_step(select_stmt)) == SQLITE_ROW) {
        int from_loc_id = sqlite3_column_int(select_stmt, 0);
        int to_loc_id = sqlite3_column_int(select_stmt, 1);
        const unsigned char *txt = sqlite3_column_text(select_stmt, 2);
        if (!waypoint_cache_add_entry(cache, from_loc_id, to_loc_id, (const char*)txt)) {
            rc = SQLITE_NOMEM;
            goto cleanup;
        }
    }

    if (rc != SQLITE_DONE) goto cleanup;
    rc = SQLITE_OK;

cleanup:
    if (insert_stmt) sqlite3_finalize(insert_stmt);
    if (select_stmt) sqlite3_finalize(select_stmt);
    sqlite3_exec(db, "DELETE FROM tmp_waypoint_legs;", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        waypoint_cache_destroy(cache);
        return 0;
    }
    return 1;
}

static const waypoint_cache_entry_t *waypoint_cache_find_entry(const waypoint_cache_t *cache,
                                                               int from_loc_id, int to_loc_id) {
    if (!cache) return NULL;
    for (int i = 0; i < cache->count; i++) {
        if (cache->entries[i].from_loc_id == from_loc_id &&
            cache->entries[i].to_loc_id == to_loc_id) {
            return &cache->entries[i];
        }
    }
    return NULL;
}

static const waypoint_cache_entry_t *lookup_waypoint_path_local(waypoint_cache_t *cache,
                                                                int from_loc_id, int to_loc_id,
                                                                int *is_reverse) {
    const waypoint_cache_entry_t *entry;

    if (is_reverse) *is_reverse = 0;
    if (!cache || from_loc_id <= 0 || to_loc_id <= 0) return NULL;

    cache->total_queries++;
    entry = waypoint_cache_find_entry(cache, from_loc_id, to_loc_id);
    if (entry) {
        cache->direct_hits++;
        return entry;
    }

    entry = waypoint_cache_find_entry(cache, to_loc_id, from_loc_id);
    if (entry) {
        cache->reverse_hits++;
        if (is_reverse) *is_reverse = 1;
        return entry;
    }

    cache->misses++;
    return NULL;
}

static int lookup_waypoint_path_json_cb(const void *ctx,
                                        int from_loc_id,
                                        int to_loc_id,
                                        int **out_ids,
                                        int *out_count) {
    const segment_waypoint_lookup_ctx_t *lookup_ctx = (const segment_waypoint_lookup_ctx_t*)ctx;
    const waypoint_cache_entry_t *entry;
    int is_reverse = 0;
    int *ids = NULL;

    if (out_ids) *out_ids = NULL;
    if (out_count) *out_count = 0;
    if (!lookup_ctx || !lookup_ctx->cache || !out_ids) return 0;

    entry = lookup_waypoint_path_local(lookup_ctx->cache, from_loc_id, to_loc_id, &is_reverse);
    if (lookup_ctx->leg_query_count) (*lookup_ctx->leg_query_count)++;
    if (!entry || entry->waypoint_count <= 0) return 1;

    ids = (int*)malloc((size_t)entry->waypoint_count * sizeof(int));
    if (!ids) return 0;

    if (is_reverse) {
        for (int i = 0; i < entry->waypoint_count; i++) {
            ids[i] = entry->waypoint_ids[entry->waypoint_count - 1 - i];
        }
    } else {
        memcpy(ids, entry->waypoint_ids, (size_t)entry->waypoint_count * sizeof(int));
    }

    if (lookup_ctx->total_waypoint_ids) {
        (*lookup_ctx->total_waypoint_ids) += entry->waypoint_count;
    }
    *out_ids = ids;
    if (out_count) *out_count = entry->waypoint_count;
    return 1;
}

static int segment_leg_is_station_haul(const nn_instance_t *inst, int from_loc_id, int to_loc_id) {
    if (!inst) return 0;
    for (int i = 0; i < inst->num_stations + inst->num_ports; i++) {
        const nn_node_t *node = &inst->nodes[i];
        if (node->is_port) continue;
        if ((node->start_loc_id == from_loc_id && node->end_loc_id == to_loc_id) ||
            (node->end_loc_id == from_loc_id && node->start_loc_id == to_loc_id)) {
            return 1;
        }
    }
    return 0;
}

static double segment_distance_or_zero(const nn_instance_t *inst, int from_loc_id, int to_loc_id) {
    if (!inst || !inst->distances) return 0.0;
    if (from_loc_id == to_loc_id) return 0.0;
    if (from_loc_id < 0 || from_loc_id >= inst->max_loc_id) return 0.0;
    if (to_loc_id < 0 || to_loc_id >= inst->max_loc_id) return 0.0;
    return (inst->distances[from_loc_id][to_loc_id] > 0.0) ?
        inst->distances[from_loc_id][to_loc_id] : 0.0;
}

static void segment_accumulate_leg_distance(const nn_instance_t *inst,
                                            int from_loc_id,
                                            int to_loc_id,
                                            gsp_distance_breakdown_t *breakdown) {
    double d;
    if (!breakdown) return;
    d = segment_distance_or_zero(inst, from_loc_id, to_loc_id);
    if (segment_leg_is_station_haul(inst, from_loc_id, to_loc_id)) {
        breakdown->haul_distance_nm += d;
    } else {
        breakdown->transit_distance_nm += d;
    }
    breakdown->total_distance_nm += d;
}

static void segment_compute_segment_breakdowns(const nn_instance_t *inst,
                                               const nn_solution_t *sol,
                                               int boat_start_loc_id,
                                               int boat_end_loc_id,
                                               gsp_distance_breakdown_t *segment_breakdowns,
                                               gsp_distance_breakdown_t *total_breakdown) {
    if (!inst || !sol || !segment_breakdowns || !total_breakdown) return;
    memset(total_breakdown, 0, sizeof(*total_breakdown));
    for (int s = 0; s < sol->segment_count; s++) {
        int prev_loc = (s == 0) ? boat_start_loc_id : sol->tour[sol->segment_ends[s - 1]];
        memset(&segment_breakdowns[s], 0, sizeof(segment_breakdowns[s]));
        for (int i = sol->segment_starts[s]; i <= sol->segment_ends[s]; i++) {
            segment_accumulate_leg_distance(inst, prev_loc, sol->tour[i], &segment_breakdowns[s]);
            prev_loc = sol->tour[i];
        }
        if (s == sol->segment_count - 1 && prev_loc != boat_end_loc_id) {
            segment_accumulate_leg_distance(inst, prev_loc, boat_end_loc_id, &segment_breakdowns[s]);
        }
        total_breakdown->transit_distance_nm += segment_breakdowns[s].transit_distance_nm;
        total_breakdown->haul_distance_nm += segment_breakdowns[s].haul_distance_nm;
        total_breakdown->total_distance_nm += segment_breakdowns[s].total_distance_nm;
    }
}

static void write_solution_section(FILE *fp, const char *label,
                                   const nn_instance_t *inst, const nn_solution_t *sol,
                                   int boat_start_loc_id, int boat_end_loc_id,
                                   int is_feasible,
                                   waypoint_cache_t *cache,
                                   int *leg_query_count,
                                   int *total_waypoint_ids,
                                   const char *variant_name,
                                   int segment_distances_include_return)
{
    gsp_distance_breakdown_t *segment_breakdowns = NULL;
    gsp_distance_breakdown_t total_breakdown;
    segment_waypoint_lookup_ctx_t lookup_ctx;

    if (!fp || !inst || !sol || !cache) return;
    (void)segment_distances_include_return;

    memset(&total_breakdown, 0, sizeof(total_breakdown));
    segment_breakdowns = (gsp_distance_breakdown_t*)calloc((size_t)sol->segment_count, sizeof(gsp_distance_breakdown_t));
    if (!segment_breakdowns) {
        fprintf(stderr, "ERROR: Failed to allocate segment distance breakdowns\n");
        return;
    }
    segment_compute_segment_breakdowns(inst, sol, boat_start_loc_id, boat_end_loc_id,
                                       segment_breakdowns, &total_breakdown);
    lookup_ctx.cache = cache;
    lookup_ctx.leg_query_count = leg_query_count;
    lookup_ctx.total_waypoint_ids = total_waypoint_ids;

    if (!gsp_write_segmented_solution_entry_json(fp,
                                                 "  ",
                                                 "  ",
                                                 label,
                                                 variant_name,
                                                 inst,
                                                 sol,
                                                 boat_start_loc_id,
                                                 boat_end_loc_id,
                                                 is_feasible,
                                                 segment_breakdowns,
                                                 &total_breakdown,
                                                 lookup_waypoint_path_json_cb,
                                                 &lookup_ctx)) {
        fprintf(stderr, "ERROR: Failed to write segmented solution JSON for '%s'\n", label);
    }
    free(segment_breakdowns);
}

/* Write NN solution to JSON in survey format */
static void write_json(sqlite3 *db, const char *output_path, const nn_instance_t *inst,
                       const nn_solution_t *sol, int boat_id,
                       const nn_solution_t *pre_capacity_sol,
                       const nn_solution_t *pre_local_postopt_sol,
                       const gsp_mip_solve_detail_t *local_postopt_details,
                       int local_postopt_detail_count,
                       const char *boat_name,
                       const char *strategy_name,
                       const char *method_name,
                       int boat_start_loc_id, int boat_end_loc_id,
                       double boat_capacity,
                       double target_capacity,
                       int target_catch_slack_kg,
                       double boat_start_lat, double boat_start_lon,
                       int is_feasible,
                       struct timespec mode_start_time,
                       double preprocessing_seconds,
                       double solve_runtime_seconds,
                       double local_postopt_runtime_seconds,
                       double mip_time_limit_seconds,
                       double check_runtime_seconds,
                       double *output_runtime_seconds) {
    waypoint_cache_t cache;
    struct timespec t_output_start, t_output_preload_end, t_output_expand_end, t_output_end;
    int leg_query_count = 0;
    int total_waypoint_ids = 0;
    int has_presolve;
    int has_pre_local_postopt;
    const char *final_variant_name;
    const char *pre_local_postopt_variant_name = "baseline-capacity-feasible";
    double mip_runtime_mean = -1.0;
    double mip_runtime_max = -1.0;
    double mip_gap_mean = -1.0;
    double mip_gap_max = -1.0;

    FILE *fp = fopen(output_path, "w");
    if (!fp) {
        perror("Cannot open output file");
        if (output_runtime_seconds) *output_runtime_seconds = 0.0;
        return;
    }
    memset(&cache, 0, sizeof(cache));
    clock_gettime(CLOCK_MONOTONIC, &t_output_start);
    if (!waypoint_cache_preload(db, inst, sol, pre_capacity_sol, boat_start_loc_id, boat_end_loc_id, &cache)) {
        fprintf(stderr, "ERROR: Failed to preload waypoint paths: %s\n", sqlite3_errmsg(db));
        fclose(fp);
        if (output_runtime_seconds) *output_runtime_seconds = 0.0;
        return;
    }

    clock_gettime(CLOCK_MONOTONIC, &t_output_preload_end);
    printf("\n[OUTPUT] Writing JSON and expanding waypoint paths...\n");
    printf("[OUTPUT] Preloaded %d route legs into %d cached path rows in %.3f s\n",
           cache.preload_leg_pairs, cache.count,
           elapsed_seconds(t_output_start, t_output_preload_end));

    fprintf(fp, "{\n");
    {
        gsp_metadata_json_t metadata = {0};
        gsp_problem_json_t problem = {0};
        metadata.solver_version = "init_nn_1.0";
        metadata.mode_name = "segment";
        metadata.strategy_name = strategy_name ? strategy_name : "unknown";
        metadata.boat_id = boat_id;
        metadata.boat_name = boat_name;
        metadata.boat_lat = boat_start_lat;
        metadata.boat_lon = boat_start_lon;
        metadata.boat_location_id = boat_start_loc_id;
        gsp_write_metadata_json(fp, "  ", &metadata, 1);

        problem.has_num_nodes = 1;
        problem.num_nodes = sol->tour_length;
        problem.has_num_stations = 1;
        problem.num_stations = inst->num_stations;
        problem.has_capacity = 1;
        problem.capacity = boat_capacity;
        problem.has_target_capacity = 1;
        problem.target_capacity = target_capacity;
        problem.has_target_catch_slack_kg = 1;
        problem.target_catch_slack_kg = target_catch_slack_kg;
        gsp_write_problem_json(fp, "  ", &problem, 1);
    }
    has_presolve = pre_capacity_sol &&
                   pre_capacity_sol->visit_station_count > 0 &&
                   (strcmp(strategy_name ? strategy_name : "", "ci") == 0 ||
                    strcmp(strategy_name ? strategy_name : "", "ge") == 0);
    has_pre_local_postopt = pre_local_postopt_sol &&
                            pre_local_postopt_sol->visit_station_count > 0;
    final_variant_name = "capacity-feasible";

    fprintf(fp, "  \"solution\": {\n");
    if (has_presolve) {
        int before_feasible = segment_solution_is_capacity_feasible(pre_capacity_sol, boat_capacity) &&
                              stations_are_unique_and_complete(pre_capacity_sol->visit_station_ids,
                                                               pre_capacity_sol->visit_station_count,
                                                               inst->num_stations);
        write_solution_section(fp, "presolve", inst, pre_capacity_sol,
                               boat_start_loc_id, boat_end_loc_id,
                               before_feasible, &cache, &leg_query_count, &total_waypoint_ids,
                               "presolve", 1);
        fprintf(fp, ",\n");
    }
    if (has_pre_local_postopt) {
        int pre_local_postopt_feasible = segment_solution_is_capacity_feasible(pre_local_postopt_sol, boat_capacity) &&
                                         stations_are_unique_and_complete(pre_local_postopt_sol->visit_station_ids,
                                                                          pre_local_postopt_sol->visit_station_count,
                                                                          inst->num_stations);
        write_solution_section(fp, pre_local_postopt_variant_name, inst, pre_local_postopt_sol,
                               boat_start_loc_id, boat_end_loc_id,
                               pre_local_postopt_feasible, &cache, &leg_query_count, &total_waypoint_ids,
                               pre_local_postopt_variant_name, 0);
        fprintf(fp, ",\n");
    }
    write_solution_section(fp, final_variant_name, inst, sol, boat_start_loc_id, boat_end_loc_id,
                           is_feasible, &cache, &leg_query_count, &total_waypoint_ids,
                           final_variant_name, 0);
    fprintf(fp, "\n");
    fprintf(fp, "  },\n");
    clock_gettime(CLOCK_MONOTONIC, &t_output_expand_end);
    printf("[OUTPUT] Waypoint expansion: %d legs, %d waypoint IDs, direct=%d reverse=%d miss=%d, %.3f s\n",
           leg_query_count, total_waypoint_ids, cache.direct_hits, cache.reverse_hits, cache.misses,
           elapsed_seconds(t_output_preload_end, t_output_expand_end));

    gsp_compute_mip_summary(local_postopt_details, local_postopt_detail_count,
                            &mip_runtime_mean, &mip_runtime_max,
                            &mip_gap_mean, &mip_gap_max);
    gsp_write_segment_mip_section(fp, "l1seg", "endpaired_tsp", mip_time_limit_seconds,
                                  local_postopt_details, local_postopt_detail_count);

    {
        double distance_trajectory[3];
        double solution_runtime_trajectory[2];
        int distance_count = 0;
        int runtime_count = 0;
        double postprocessing_seconds = elapsed_seconds(t_output_start, t_output_expand_end);
        double total_runtime_seconds =
            preprocessing_seconds + solve_runtime_seconds + local_postopt_runtime_seconds +
            check_runtime_seconds + postprocessing_seconds;
        gsp_summary_json_t summary = {0};

        if (has_presolve) {
            distance_trajectory[distance_count++] = pre_capacity_sol->total_distance;
        }
        if (has_pre_local_postopt) {
            distance_trajectory[distance_count++] = pre_local_postopt_sol->total_distance;
        }
        distance_trajectory[distance_count++] = sol->total_distance;
        solution_runtime_trajectory[runtime_count++] = solve_runtime_seconds;
        if (has_pre_local_postopt) {
            solution_runtime_trajectory[runtime_count++] = local_postopt_runtime_seconds;
        }

        summary.final_name = final_variant_name;
        summary.stage_name = "segment_complete";
        summary.feasible = is_feasible;
        summary.method_name = method_name ? method_name : "unknown";
        summary.has_baseline = has_pre_local_postopt;
        summary.baseline_distance_nm = has_pre_local_postopt ? pre_local_postopt_sol->total_distance : 0.0;
        summary.distance_trajectory_nm = distance_trajectory;
        summary.distance_trajectory_count = distance_count;
        summary.final_distance_nm = sol->total_distance;
        summary.preprocessing_seconds = preprocessing_seconds;
        summary.solution_runtime_seconds = solution_runtime_trajectory;
        summary.solution_runtime_count = runtime_count;
        summary.postprocessing_seconds = postprocessing_seconds;
        summary.grandtotal_seconds = total_runtime_seconds;
        summary.include_runtime = 1;
        summary.include_mip = 1;
        summary.mip_solve_count = local_postopt_detail_count;
        summary.mip_runtime_mean = mip_runtime_mean;
        summary.mip_runtime_max = mip_runtime_max;
        summary.mip_gap_mean = mip_gap_mean;
        summary.mip_gap_max = mip_gap_max;
        gsp_write_summary_json(fp, "  ", &summary, 0);
    }

    fprintf(fp, "}\n");

    clock_gettime(CLOCK_MONOTONIC, &t_output_end);
    fclose(fp);
    waypoint_cache_destroy(&cache);
    if (output_runtime_seconds) *output_runtime_seconds = elapsed_seconds(t_output_start, t_output_end);
    printf("[OUTPUT] Solution written to %s (total output %.3f s)\n",
           output_path, elapsed_seconds(t_output_start, t_output_end));
}

static void write_construction_json(sqlite3 *db, const char *output_path, const nn_instance_t *inst,
                                    const nn_solution_t *sol, int boat_id,
                                    const char *boat_name,
                                    const char *strategy_name,
                                    const char *method_name,
                                    int boat_start_loc_id, int boat_end_loc_id,
                                    double boat_capacity,
                                    double target_capacity,
                                    int target_catch_slack_kg,
                                    double boat_start_lat, double boat_start_lon,
                                    double preprocessing_seconds,
                                    double solve_runtime_seconds,
                                    double *output_runtime_seconds) {
    waypoint_cache_t cache;
    struct timespec t_output_start, t_output_preload_end, t_output_expand_end, t_output_end;
    int leg_query_count = 0;
    int total_waypoint_ids = 0;
    FILE *fp = fopen(output_path, "w");

    if (!fp) {
        perror("Cannot open output file");
        if (output_runtime_seconds) *output_runtime_seconds = 0.0;
        return;
    }

    memset(&cache, 0, sizeof(cache));
    clock_gettime(CLOCK_MONOTONIC, &t_output_start);
    if (!waypoint_cache_preload(db, inst, sol, NULL, boat_start_loc_id, boat_end_loc_id, &cache)) {
        fprintf(stderr, "ERROR: Failed to preload waypoint paths: %s\n", sqlite3_errmsg(db));
        fclose(fp);
        if (output_runtime_seconds) *output_runtime_seconds = 0.0;
        return;
    }

    clock_gettime(CLOCK_MONOTONIC, &t_output_preload_end);
    fprintf(fp, "{\n");
    {
        gsp_metadata_json_t metadata = {0};
        gsp_problem_json_t problem = {0};
        metadata.solver_version = "construction_1.0";
        metadata.mode_name = "construction";
        metadata.strategy_name = strategy_name ? strategy_name : "unknown";
        metadata.boat_id = boat_id;
        metadata.boat_name = boat_name;
        metadata.boat_lat = boat_start_lat;
        metadata.boat_lon = boat_start_lon;
        metadata.boat_location_id = boat_start_loc_id;
        gsp_write_metadata_json(fp, "  ", &metadata, 1);

        problem.has_num_nodes = 1;
        problem.num_nodes = sol->tour_length;
        problem.has_num_stations = 1;
        problem.num_stations = inst->num_stations;
        problem.has_capacity = 1;
        problem.capacity = boat_capacity;
        problem.has_target_capacity = 1;
        problem.target_capacity = target_capacity;
        problem.has_target_catch_slack_kg = 1;
        problem.target_catch_slack_kg = target_catch_slack_kg;
        gsp_write_problem_json(fp, "  ", &problem, 1);
    }

    fprintf(fp, "  \"solution\": {\n");
    write_solution_section(fp, "construction", inst, sol, boat_start_loc_id, boat_end_loc_id,
                           0, &cache, &leg_query_count, &total_waypoint_ids, "construction", 0);
    fprintf(fp, "\n");
    fprintf(fp, "  },\n");
    clock_gettime(CLOCK_MONOTONIC, &t_output_expand_end);

    {
        double distance_trajectory[1];
        double solution_runtime_trajectory[1];
        double postprocessing_seconds = elapsed_seconds(t_output_start, t_output_expand_end);
        gsp_summary_json_t summary = {0};

        distance_trajectory[0] = sol->total_distance;
        solution_runtime_trajectory[0] = solve_runtime_seconds;

        summary.final_name = "construction";
        summary.stage_name = "construction_complete";
        summary.feasible = 0;
        summary.method_name = method_name ? method_name : "unknown";
        summary.has_baseline = 0;
        summary.distance_trajectory_nm = distance_trajectory;
        summary.distance_trajectory_count = 1;
        summary.final_distance_nm = sol->total_distance;
        summary.preprocessing_seconds = preprocessing_seconds;
        summary.solution_runtime_seconds = solution_runtime_trajectory;
        summary.solution_runtime_count = 1;
        summary.postprocessing_seconds = postprocessing_seconds;
        summary.grandtotal_seconds = preprocessing_seconds + solve_runtime_seconds + postprocessing_seconds;
        summary.include_runtime = 1;
        summary.include_mip = 0;
        gsp_write_summary_json(fp, "  ", &summary, 0);
    }

    fprintf(fp, "}\n");

    clock_gettime(CLOCK_MONOTONIC, &t_output_end);
    fclose(fp);
    waypoint_cache_destroy(&cache);
    if (output_runtime_seconds) *output_runtime_seconds = elapsed_seconds(t_output_start, t_output_end);
    printf("[OUTPUT] Construction written to %s (total output %.3f s)\n",
           output_path, elapsed_seconds(t_output_start, t_output_end));
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
    fprintf(fp, "    \"boat_docked_location\": {\"lat\": %.6f, \"lon\": %.6f},\n", boat_start_lat, boat_start_lon);
    fprintf(fp, "    \"boat_location_id\": %d,\n", boat_start_loc_id);
    fprintf(fp, "    \"debug_solver_skipped\": true\n");
    fprintf(fp, "  },\n");

    fprintf(fp, "  \"problem\": {\n");
    fprintf(fp, "    \"num_stations\": %d,\n", num_stations);
    fprintf(fp, "    \"num_ports\": %d,\n", num_ports);
    fprintf(fp, "    \"capacity\": %.0f\n", boat_capacity);
    fprintf(fp, "  },\n");

    fprintf(fp, "  \"solution\": {\n");
    fprintf(fp, "    \"capacity-feasible\": {\n");
    fprintf(fp, "      \"tour_segments_location_ids\": [],\n");
    fprintf(fp, "      \"tour_segments_station_ids\": [],\n");
    fprintf(fp, "      \"tour_length\": [],\n");
    fprintf(fp, "      \"segment_count\": 0,\n");
    fprintf(fp, "      \"segment_catch_amount\": [],\n");
    {
        gsp_distance_breakdown_t zero_total = {0.0, 0.0, 0.0};
        gsp_write_distance_nm_json(fp, "      ", NULL, 0, &zero_total, 1);
    }
    fprintf(fp, "      \"preprocessing_seconds\": 0.0,\n");
    fprintf(fp, "      \"method_runtime_seconds\": 0.0,\n");
    fprintf(fp, "      \"postprocessing_seconds\": 0.0,\n");
    fprintf(fp, "      \"total_runtime_seconds\": 0.0,\n");
    fprintf(fp, "      \"feasible\": false\n");
    fprintf(fp, "    }\n");
    fprintf(fp, "  },\n");

    {
        double zero = 0.0;
        gsp_summary_json_t summary = {0};
        summary.final_name = "capacity-feasible";
        summary.stage_name = "debug_metadata_only";
        summary.feasible = 0;
        summary.method_name = "none";
        summary.distance_trajectory_nm = &zero;
        summary.distance_trajectory_count = 1;
        summary.final_distance_nm = 0.0;
        summary.preprocessing_seconds = 0.0;
        summary.solution_runtime_seconds = &zero;
        summary.solution_runtime_count = 1;
        summary.postprocessing_seconds = 0.0;
        summary.grandtotal_seconds = 0.0;
        summary.include_runtime = 1;
        gsp_write_summary_json(fp, "  ", &summary, 0);
    }
    fprintf(fp, "}\n");

    fclose(fp);
    printf("[OUTPUT] Metadata-only JSON written to %s\n", output_path);
}

static int run_construction_strategy(const char *strategy,
                                     int construction_output,
                                     const nn_instance_t *inst,
                                     nn_solution_t *sol,
                                     nn_solution_t *pre_capacity_sol,
                                     int boat_start_loc_id,
                                     int boat_end_loc_id,
                                     int target_capacity,
                                     const char **out_method_name) {
    const char *method_name = NULL;
    int rc = -1;

    if (strcmp(strategy, "nn") == 0) {
        method_name = "nearest_neighbor";
        rc = construction_output
            ? nn_construction_solve(inst, sol, boat_start_loc_id, boat_end_loc_id)
            : nn_solve(inst, sol, boat_start_loc_id, boat_end_loc_id, target_capacity);
    } else if (strcmp(strategy, "ge") == 0) {
        method_name = "greedy_edge";
        rc = construction_output
            ? ge_construction_solve(inst, sol, boat_start_loc_id, boat_end_loc_id)
            : gi_solve(inst, sol, pre_capacity_sol, boat_start_loc_id, boat_end_loc_id, target_capacity);
    } else if (strcmp(strategy, "ci") == 0) {
        method_name = "cheapest_insertion";
        rc = construction_output
            ? ci_construction_solve(inst, sol, boat_start_loc_id, boat_end_loc_id)
            : ci_solve(inst, sol, pre_capacity_sol, boat_start_loc_id, boat_end_loc_id, target_capacity);
    }

    if (out_method_name) *out_method_name = method_name;
    return rc;
}

static int run_segment_postopt(const char *config,
                               const nn_instance_t *inst,
                               int boat_start_loc_id,
                               int boat_end_loc_id,
                               nn_solution_t *sol,
                               nn_solution_t *pre_local_postopt_sol,
                               double *out_mip_time_limit_seconds,
                               double *out_local_postopt_runtime_seconds,
                               int *out_local_postopt_segment_solve_count,
                               gsp_mip_solve_detail_t **out_local_postopt_details,
                               int *out_local_postopt_detail_count) {
    double mip_time_limit_seconds = read_segment_mip_time_limit_from_yaml(config);
    double segment_haul_distance_scale = read_segment_haul_distance_scale_from_yaml(config);

    if (!segment_copy_solution(sol, pre_local_postopt_sol)) {
        return 0;
    }
    if (!segment_apply_local_postopt(inst, pre_local_postopt_sol,
                                     boat_start_loc_id, boat_end_loc_id,
                                     mip_time_limit_seconds,
                                     segment_haul_distance_scale,
                                     sol,
                                     out_local_postopt_runtime_seconds,
                                     out_local_postopt_segment_solve_count,
                                     out_local_postopt_details,
                                     out_local_postopt_detail_count)) {
        return 0;
    }

    if (out_mip_time_limit_seconds) *out_mip_time_limit_seconds = mip_time_limit_seconds;
    return 1;
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
int mode_construction(int argc, char **argv) {
    printf("============================================================\n");
    printf("GSP Solver - Construction / Segment\n");
    printf("============================================================\n\n");

    struct timespec t_mode_start;
    clock_gettime(CLOCK_MONOTONIC, &t_mode_start);

    const char *strategy, *database, *config, *output;
    parse_args(argc, argv, &strategy, &database, &config, &output);

    if (!strategy || !database || !config) {
        fprintf(stderr, "ERROR: Missing required arguments\n");
        fprintf(stderr, "Usage: gsp --mode construction --strategy nn --database <db> --config <yaml> --output <json>\n");
        return 1;
    }

    if (strcmp(strategy, "nn") != 0 && strcmp(strategy, "ge") != 0 && strcmp(strategy, "ci") != 0) {
        fprintf(stderr, "ERROR: Only 'nn', 'ge', and 'ci' strategies are currently implemented\n");
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
    int target_catch_slack_kg = read_init_capacity_slack_from_yaml(config);
    int boat_start_loc_id = 0;
    int boat_end_loc_id = 0;
    double boat_start_lat = 0.0;
    double boat_start_lon = 0.0;
    char boat_name[256] = "Unknown";

    const char *boat_sql =
        "SELECT b.name, b.capacity, b.location_id, l.lat, l.lon "
        "FROM boats b "
        "JOIN locations l ON l.id = b.location_id "
        "WHERE b.id = ?";

    sqlite3_prepare_v2(db, boat_sql, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, boat_id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *name_txt = sqlite3_column_text(stmt, 0);
        if (name_txt) snprintf(boat_name, sizeof(boat_name), "%s", (const char*)name_txt);
        boat_capacity = sqlite3_column_double(stmt, 1);
        boat_start_loc_id = sqlite3_column_int(stmt, 2);
        boat_end_loc_id = boat_start_loc_id;
        boat_start_lat = sqlite3_column_double(stmt, 3);
        boat_start_lon = sqlite3_column_double(stmt, 4);
    }
    sqlite3_finalize(stmt);

    printf("[LOAD] Boat capacity: %.0f\n", boat_capacity);
    printf("[LOAD] Target catch slack: %d\n", target_catch_slack_kg);
    printf("[LOAD] Boat docked location: %d\n", boat_start_loc_id);

    double target_capacity = boat_capacity - (double)target_catch_slack_kg;
    if (target_capacity <= 0.0) {
        fprintf(stderr, "ERROR: target_capacity <= 0 (capacity=%.0f slack=%d)\n",
                boat_capacity, target_catch_slack_kg);
        sqlite3_close(db);
        return 1;
    }
    printf("[LOAD] Target capacity: %.0f\n", target_capacity);

    struct timespec t_preproc_end;
    clock_gettime(CLOCK_MONOTONIC, &t_preproc_end);
    double preprocessing_seconds = elapsed_seconds(t_mode_start, t_preproc_end);
    printf("[LOAD] Done in %.4f s\n\n", preprocessing_seconds);

    /* Solve selected init heuristic */
    nn_solution_t sol = {0};
    nn_solution_t pre_capacity_sol = {0};
    nn_solution_t pre_local_postopt_sol = {0};
    const char *method_name = NULL;
    int construction_output = output_path_is_construction_json(output);
    struct timespec t_solve_start;
    clock_gettime(CLOCK_MONOTONIC, &t_solve_start);
    if (run_construction_strategy(strategy, construction_output,
                                  &inst, &sol, &pre_capacity_sol,
                                  boat_start_loc_id, boat_end_loc_id,
                                  (int)target_capacity,
                                  &method_name) != 0) {
        fprintf(stderr, "ERROR: Failed to solve %s\n", strategy ? strategy : "construction");
        sqlite3_close(db);
        return 1;
    }
    struct timespec t_solve_end;
    clock_gettime(CLOCK_MONOTONIC, &t_solve_end);
    double solve_runtime_seconds = elapsed_seconds(t_solve_start, t_solve_end);
    printf("[CONSTRUCTION] %s done in %.4f s\n",
           method_name ? method_name : (strategy ? strategy : "unknown"),
           solve_runtime_seconds);

    double mip_time_limit_seconds = 0.0;
    double local_postopt_runtime_seconds = 0.0;
    int local_postopt_segment_solve_count = 0;
    gsp_mip_solve_detail_t *local_postopt_details = NULL;
    int local_postopt_detail_count = 0;
    if (!construction_output) {
        if (!run_segment_postopt(config, &inst, boat_start_loc_id, boat_end_loc_id,
                                 &sol, &pre_local_postopt_sol,
                                 &mip_time_limit_seconds,
                                 &local_postopt_runtime_seconds,
                                 &local_postopt_segment_solve_count,
                                 &local_postopt_details,
                                 &local_postopt_detail_count)) {
            fprintf(stderr, "ERROR: Failed to apply local post optimization to init solution\n");
            sqlite3_close(db);
            return 1;
        }
        printf("[POSTOPT] Segment local post-opt: solves=%d runtime=%.4f s time_limit=%s%.0f\n",
               local_postopt_segment_solve_count,
               local_postopt_runtime_seconds,
               (mip_time_limit_seconds > 0.0) ? "" : "uncapped ",
               (mip_time_limit_seconds > 0.0) ? mip_time_limit_seconds : 0.0);
        printf("[POSTOPT] Distance: %.2f -> %.2f nm\n",
               pre_local_postopt_sol.total_distance,
               sol.total_distance);
    }

    /* Write JSON output (feasibility check NOT included in runtime) */
    printf("\n[CHECK] Feasibility check starting\n");
    printf("[CHECK] Rules: segment weight <= %.0f, unique+complete station set, boat at start/end, ports only as segment boundaries\n",
           boat_capacity);
    struct timespec t_check_start, t_check_end;
    clock_gettime(CLOCK_MONOTONIC, &t_check_start);

    int is_feasible = 0;
    int stations_ok = stations_are_unique_and_complete(sol.visit_station_ids, sol.visit_station_count, inst.num_stations);
    int boundaries_ok = segment_solution_has_valid_boundaries(&inst, &sol);
    if (!construction_output) {
        int capacity_ok = segments_within_capacity(sol.segment_catches, sol.segment_count, boat_capacity);
        is_feasible = capacity_ok && stations_ok && boundaries_ok;
        clock_gettime(CLOCK_MONOTONIC, &t_check_end);
        printf("[CHECK] Capacity: %s (%d segments)\n", capacity_ok ? "OK" : "FAIL", sol.segment_count);
        printf("[CHECK] Stations: %s (%d visited, expected %d)\n",
               stations_ok ? "OK" : "FAIL", sol.visit_station_count, inst.num_stations);
        printf("[CHECK] Boundaries: %s\n", boundaries_ok ? "OK" : "FAIL");
    } else {
        clock_gettime(CLOCK_MONOTONIC, &t_check_end);
        printf("[CHECK] Construction output: skipping capacity-feasible classification\n");
        printf("[CHECK] Stations: %s (%d visited, expected %d)\n",
               stations_ok ? "OK" : "FAIL", sol.visit_station_count, inst.num_stations);
        printf("[CHECK] Boundaries: %s\n", boundaries_ok ? "OK" : "FAIL");
    }
    printf("[CHECK] Done in %.6f s\n", elapsed_seconds(t_check_start, t_check_end));

    {
        double output_runtime_seconds = 0.0;
        if (construction_output) {
            write_construction_json(db, output, &inst, &sol, boat_id,
                                    boat_name, strategy, method_name,
                                    boat_start_loc_id, boat_end_loc_id,
                                    boat_capacity, target_capacity, target_catch_slack_kg,
                                    boat_start_lat, boat_start_lon,
                                    preprocessing_seconds, solve_runtime_seconds,
                                    &output_runtime_seconds);
        } else {
            write_json(db, output, &inst, &sol, boat_id,
                       ((strcmp(strategy, "ci") == 0) || (strcmp(strategy, "ge") == 0)) ? &pre_capacity_sol : NULL,
                       &pre_local_postopt_sol,
                       local_postopt_details, local_postopt_detail_count,
                       boat_name, strategy, method_name,
                       boat_start_loc_id, boat_end_loc_id, boat_capacity, target_capacity, target_catch_slack_kg,
                       boat_start_lat, boat_start_lon, is_feasible,
                       t_mode_start,
                       preprocessing_seconds, solve_runtime_seconds, local_postopt_runtime_seconds,
                       mip_time_limit_seconds,
                       elapsed_seconds(t_check_start, t_check_end),
                       &output_runtime_seconds);
        }
    }

    printf("\n[SUCCESS] %s complete!\n", construction_output ? "Construction" : "Segment");
    printf("============================================================\n");

    /* Cleanup */
    sqlite3_close(db);
    segment_free_solution(&sol);
    segment_free_solution(&pre_capacity_sol);
    segment_free_solution(&pre_local_postopt_sol);
    free(local_postopt_details);
    free(inst.nodes);
    for (int i = 0; i < inst.max_loc_id; i++) {
        free(inst.distances[i]);
    }
    free(inst.distances);
    free(inst.loc_to_idx);

    return 0;
}

