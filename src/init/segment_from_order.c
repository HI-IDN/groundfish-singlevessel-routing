#include <sqlite3.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "../include/feasibility.h"
#include "../include/init_types.h"
#include "../include/json_utils.h"
#include "../include/mip_report.h"
#include "init_utils.h"
#include "segment_postopt.h"

#define MAX_LINE 1024

static const char *skip_ws(const char *p);
static const char *find_matching_brace(const char *p);
static int extract_final_solution_variant_from_json(const char *json_path,
                                                    char **out_variant_name,
                                                    char **out_variant_object);
static int append_int_local(int **arr, int *n, int *cap, int v);
static void free_segmented_station_order(int **segments, int *counts, int segment_count);
static int lookup_waypoint_path_local(sqlite3 *db, int from_loc_id, int to_loc_id, int **out_ids);
static int lookup_waypoint_path_json_cb(const void *ctx,
                                        int from_loc_id,
                                        int to_loc_id,
                                        int **out_ids,
                                        int *out_count);

static double elapsed_seconds(struct timespec start, struct timespec end) {
    return (double)(end.tv_sec - start.tv_sec) +
           (double)(end.tv_nsec - start.tv_nsec) / 1e9;
}

static void normalize_json_text_newlines(char *text) {
    char *src = text;
    char *dst = text;
    if (!text) return;
    while (*src) {
        if (*src != '\r') {
            *dst++ = *src;
        }
        src++;
    }
    *dst = '\0';
}

static int read_boat_id_from_yaml(const char *yaml_path) {
    FILE *fp = fopen(yaml_path, "r");
    char line[MAX_LINE];
    int boat_id = 2;

    if (!fp) {
        fprintf(stderr, "Warning: Cannot open %s, using default boat_id=2\n", yaml_path);
        return 2;
    }

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

static int load_nodes(sqlite3 *db, nn_instance_t *inst) {
    sqlite3_stmt *stmt = NULL;
    const char *count_sql =
        "SELECT (SELECT COUNT(*) FROM stations), (SELECT COUNT(*) FROM ports);";
    int num_stations = 0;
    int num_ports = 0;
    int idx = 0;
    int port_idx;

    if (sqlite3_prepare_v2(db, count_sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        num_stations = sqlite3_column_int(stmt, 0);
        num_ports = sqlite3_column_int(stmt, 1);
    }
    sqlite3_finalize(stmt);

    inst->num_stations = num_stations;
    inst->num_ports = num_ports;
    inst->nodes = (nn_node_t*)calloc((size_t)(num_stations + num_ports), sizeof(nn_node_t));
    if (!inst->nodes) return -1;

    if (sqlite3_prepare_v2(db,
            "SELECT id, start_location_id, end_location_id, amount FROM stations ORDER BY id;",
            -1, &stmt, NULL) != SQLITE_OK) return -1;
    while (sqlite3_step(stmt) == SQLITE_ROW && idx < num_stations) {
        inst->nodes[idx].table_id = sqlite3_column_int(stmt, 0);
        inst->nodes[idx].start_loc_id = sqlite3_column_int(stmt, 1);
        inst->nodes[idx].end_loc_id = sqlite3_column_int(stmt, 2);
        inst->nodes[idx].amount = sqlite3_column_int(stmt, 3);
        inst->nodes[idx].is_port = 0;
        idx++;
    }
    sqlite3_finalize(stmt);

    if (sqlite3_prepare_v2(db,
            "SELECT id, location_id FROM ports ORDER BY id;",
            -1, &stmt, NULL) != SQLITE_OK) return -1;
    port_idx = num_stations;
    while (sqlite3_step(stmt) == SQLITE_ROW && port_idx < num_stations + num_ports) {
        int loc_id = sqlite3_column_int(stmt, 1);
        inst->nodes[port_idx].table_id = sqlite3_column_int(stmt, 0);
        inst->nodes[port_idx].start_loc_id = loc_id;
        inst->nodes[port_idx].end_loc_id = loc_id;
        inst->nodes[port_idx].amount = 0;
        inst->nodes[port_idx].is_port = 1;
        port_idx++;
    }
    sqlite3_finalize(stmt);
    return 0;
}

static int load_distance_matrix(sqlite3 *db, nn_instance_t *inst) {
    sqlite3_stmt *stmt = NULL;
    int max_loc_id = 0;

    if (sqlite3_prepare_v2(db, "SELECT MAX(id) FROM locations;", -1, &stmt, NULL) != SQLITE_OK) return -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) max_loc_id = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    inst->max_loc_id = max_loc_id + 1;
    inst->distances = (double**)malloc((size_t)inst->max_loc_id * sizeof(double*));
    inst->loc_to_idx = (int*)malloc((size_t)inst->max_loc_id * sizeof(int));
    if (!inst->distances || !inst->loc_to_idx) return -1;

    for (int i = 0; i < inst->max_loc_id; i++) {
        inst->distances[i] = (double*)malloc((size_t)inst->max_loc_id * sizeof(double));
        if (!inst->distances[i]) return -1;
        inst->loc_to_idx[i] = i;
        for (int j = 0; j < inst->max_loc_id; j++) inst->distances[i][j] = -1.0;
    }

    if (sqlite3_prepare_v2(db,
            "SELECT from_location_id, to_location_id, distance_nm FROM distances;",
            -1, &stmt, NULL) != SQLITE_OK) return -1;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int from_id = sqlite3_column_int(stmt, 0);
        int to_id = sqlite3_column_int(stmt, 1);
        double dist = sqlite3_column_double(stmt, 2);
        if (from_id >= 0 && from_id < inst->max_loc_id &&
            to_id >= 0 && to_id < inst->max_loc_id) {
            inst->distances[from_id][to_id] = dist;
        }
    }
    sqlite3_finalize(stmt);
    return 0;
}

static int order_leg_is_station_haul(const nn_instance_t *inst, int from_loc_id, int to_loc_id) {
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

static double order_distance_or_zero(const nn_instance_t *inst, int from_loc_id, int to_loc_id) {
    if (!inst || !inst->distances) return 0.0;
    if (from_loc_id == to_loc_id) return 0.0;
    if (from_loc_id < 0 || from_loc_id >= inst->max_loc_id) return 0.0;
    if (to_loc_id < 0 || to_loc_id >= inst->max_loc_id) return 0.0;
    return (inst->distances[from_loc_id][to_loc_id] > 0.0) ?
        inst->distances[from_loc_id][to_loc_id] : 0.0;
}

static void order_accumulate_leg_distance(const nn_instance_t *inst,
                                          int from_loc_id,
                                          int to_loc_id,
                                          gsp_distance_breakdown_t *breakdown) {
    double d;
    if (!breakdown) return;
    d = order_distance_or_zero(inst, from_loc_id, to_loc_id);
    if (order_leg_is_station_haul(inst, from_loc_id, to_loc_id)) {
        breakdown->haul_distance_nm += d;
    } else {
        breakdown->transit_distance_nm += d;
    }
    breakdown->total_distance_nm += d;
}

static void order_compute_segment_breakdowns(const nn_instance_t *inst,
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
            order_accumulate_leg_distance(inst, prev_loc, sol->tour[i], &segment_breakdowns[s]);
            prev_loc = sol->tour[i];
        }
        if (s == sol->segment_count - 1 && prev_loc != boat_end_loc_id) {
            order_accumulate_leg_distance(inst, prev_loc, boat_end_loc_id, &segment_breakdowns[s]);
        }
        total_breakdown->transit_distance_nm += segment_breakdowns[s].transit_distance_nm;
        total_breakdown->haul_distance_nm += segment_breakdowns[s].haul_distance_nm;
        total_breakdown->total_distance_nm += segment_breakdowns[s].total_distance_nm;
    }
}

static void write_order_solution_section(FILE *fp,
                                         sqlite3 *db,
                                         const char *label,
                                         const char *variant_name,
                                         const nn_instance_t *inst,
                                         const nn_solution_t *sol,
                                         int boat_start_loc_id,
                                         int boat_end_loc_id,
                                         int is_feasible,
                                         const gsp_distance_breakdown_t *segment_breakdowns,
                                         const gsp_distance_breakdown_t *total_breakdown) {
    if (!fp || !sol) return;
    gsp_write_segmented_solution_entry_json(fp,
                                            "    ",
                                            "    ",
                                            label,
                                            variant_name,
                                            inst,
                                            sol,
                                            boat_start_loc_id,
                                            boat_end_loc_id,
                                            is_feasible,
                                            segment_breakdowns,
                                            total_breakdown,
                                            lookup_waypoint_path_json_cb,
                                            db);
}

static int lookup_waypoint_path_json_cb(const void *ctx,
                                        int from_loc_id,
                                        int to_loc_id,
                                        int **out_ids,
                                        int *out_count) {
    sqlite3 *db = (sqlite3*)ctx;
    int count = lookup_waypoint_path_local(db, from_loc_id, to_loc_id, out_ids);
    if (out_count) *out_count = count;
    return 1;
}

static int append_int_local(int **arr, int *n, int *cap, int v) {
    int *tmp;
    int new_cap;
    if (*n >= *cap) {
        new_cap = (*cap == 0) ? 16 : (*cap * 2);
        tmp = (int*)realloc(*arr, (size_t)new_cap * sizeof(int));
        if (!tmp) return 0;
        *arr = tmp;
        *cap = new_cap;
    }
    (*arr)[(*n)++] = v;
    return 1;
}

static void free_segmented_station_order(int **segments, int *counts, int segment_count) {
    (void)counts;
    if (!segments) return;
    for (int i = 0; i < segment_count; i++) free(segments[i]);
    free(segments);
    free(counts);
}

static int parse_waypoint_path_json_local(const char *json_text, int **out_ids) {
    int *ids = NULL;
    int count = 0;
    const char *p = json_text;

    if (out_ids) *out_ids = NULL;
    if (!json_text || !out_ids) return 0;

    while (*p) {
        char *endptr = NULL;
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

static void write_json(sqlite3 *db, const char *output_path, const nn_instance_t *inst,
                       const nn_solution_t *sol, int boat_id,
                       const nn_solution_t *pre_local_postopt_sol,
                       const gsp_mip_solve_detail_t *local_postopt_details,
                       int local_postopt_detail_count,
                       const char *order_input_path,
                       const char *boat_name,
                       const char *strategy_name,
                       const char *method_name,
                       int boat_start_loc_id, int boat_end_loc_id,
                       double boat_capacity,
                       double boat_start_lat, double boat_start_lon,
                       int is_feasible,
                       double preprocessing_seconds,
                       double solve_runtime_seconds,
                       double local_postopt_runtime_seconds,
                       double mip_time_limit_seconds) {
    const char *final_variant_name = "capacity-feasible";
    const char *pre_local_postopt_variant_name = "baseline-capacity-feasible";
    FILE *fp = fopen(output_path, "w");
    int has_pre_local_postopt = pre_local_postopt_sol &&
                                pre_local_postopt_sol->visit_station_count > 0;
    char *original_order_variant_name = NULL;
    char *original_order_variant_object = NULL;
    int has_original_order = 0;
    double mip_runtime_mean = -1.0;
    double mip_runtime_max = -1.0;
    double mip_gap_mean = -1.0;
    double mip_gap_max = -1.0;
    gsp_distance_breakdown_t *pre_segment_breakdowns = NULL;
    gsp_distance_breakdown_t pre_total_breakdown;
    gsp_distance_breakdown_t *final_segment_breakdowns = NULL;
    gsp_distance_breakdown_t final_total_breakdown;

    if (!fp) {
        perror("Cannot open output file");
        return;
    }

    memset(&pre_total_breakdown, 0, sizeof(pre_total_breakdown));
    memset(&final_total_breakdown, 0, sizeof(final_total_breakdown));
    if (has_pre_local_postopt && pre_local_postopt_sol->segment_count > 0) {
        pre_segment_breakdowns = (gsp_distance_breakdown_t*)calloc(
            (size_t)pre_local_postopt_sol->segment_count, sizeof(gsp_distance_breakdown_t));
        if (pre_segment_breakdowns) {
            order_compute_segment_breakdowns(inst, pre_local_postopt_sol, boat_start_loc_id, boat_end_loc_id,
                                             pre_segment_breakdowns, &pre_total_breakdown);
        }
    }
    if (sol->segment_count > 0) {
        final_segment_breakdowns = (gsp_distance_breakdown_t*)calloc(
            (size_t)sol->segment_count, sizeof(gsp_distance_breakdown_t));
        if (final_segment_breakdowns) {
            order_compute_segment_breakdowns(inst, sol, boat_start_loc_id, boat_end_loc_id,
                                             final_segment_breakdowns, &final_total_breakdown);
        }
    }

    fprintf(fp, "{\n");
    {
        gsp_metadata_json_t metadata = {0};
        gsp_problem_json_t problem = {0};
        metadata.solver_version = "init_from_order_1.0";
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
        gsp_write_problem_json(fp, "  ", &problem, 1);
    }

    has_original_order = extract_final_solution_variant_from_json(order_input_path,
                                                                  &original_order_variant_name,
                                                                  &original_order_variant_object);
    normalize_json_text_newlines(original_order_variant_object);

    fprintf(fp, "  \"solution\": {\n");
    if (has_original_order) {
        fprintf(fp, "    \"%s\": %s,\n",
                original_order_variant_name,
                original_order_variant_object);
    }
    if (has_pre_local_postopt) {
        write_order_solution_section(fp, db, pre_local_postopt_variant_name,
                                     pre_local_postopt_variant_name, inst,
                                     pre_local_postopt_sol, boat_start_loc_id,
                                     boat_end_loc_id, is_feasible,
                                     pre_segment_breakdowns, &pre_total_breakdown);
        fprintf(fp, ",\n");
    }

    write_order_solution_section(fp, db, final_variant_name, final_variant_name, inst,
                                 sol, boat_start_loc_id, boat_end_loc_id, is_feasible,
                                 final_segment_breakdowns, &final_total_breakdown);
    fprintf(fp, "\n");
    fprintf(fp, "  },\n");

    gsp_compute_mip_summary(local_postopt_details, local_postopt_detail_count,
                            &mip_runtime_mean, &mip_runtime_max,
                            &mip_gap_mean, &mip_gap_max);
    gsp_write_segment_mip_section(fp, "l1seg", "endpaired_tsp", mip_time_limit_seconds,
                                  local_postopt_details, local_postopt_detail_count);

    {
        double distance_trajectory[2];
        double solution_runtime_trajectory[2];
        int distance_count = 0;
        int runtime_count = 0;
        gsp_summary_json_t summary = {0};
        if (has_pre_local_postopt) {
            distance_trajectory[distance_count++] = pre_local_postopt_sol->total_distance;
        }
        distance_trajectory[distance_count++] = sol->total_distance;
        solution_runtime_trajectory[runtime_count++] = solve_runtime_seconds;
        solution_runtime_trajectory[runtime_count++] = local_postopt_runtime_seconds;

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
        summary.postprocessing_seconds = 0.0;
        summary.grandtotal_seconds = preprocessing_seconds + solve_runtime_seconds + local_postopt_runtime_seconds;
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

    fclose(fp);
    free(pre_segment_breakdowns);
    free(final_segment_breakdowns);
    free(original_order_variant_name);
    free(original_order_variant_object);
}

static int read_file_text(const char *path, char **out_text) {
    FILE *fp = fopen(path, "rb");
    long size;
    char *buf;
    if (out_text) *out_text = NULL;
    if (!fp) return 0;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return 0;
    }
    size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return 0;
    }
    rewind(fp);
    buf = (char*)malloc((size_t)size + 1);
    if (!buf) {
        fclose(fp);
        return 0;
    }
    if (fread(buf, 1, (size_t)size, fp) != (size_t)size) {
        free(buf);
        fclose(fp);
        return 0;
    }
    buf[size] = '\0';
    fclose(fp);
    *out_text = buf;
    return 1;
}

static const char *skip_ws(const char *p) {
    while (p && *p && isspace((unsigned char)*p)) p++;
    return p;
}

static const char *find_matching_brace(const char *p) {
    int depth = 0;
    int in_string = 0;
    int escape = 0;

    if (!p || *p != '{') return NULL;

    for (; *p; p++) {
        if (in_string) {
            if (escape) escape = 0;
            else if (*p == '\\') escape = 1;
            else if (*p == '"') in_string = 0;
            continue;
        }
        if (*p == '"') in_string = 1;
        else if (*p == '{') depth++;
        else if (*p == '}') {
            depth--;
            if (depth == 0) return p;
        }
    }

    return NULL;
}

static int extract_final_solution_variant_from_json(const char *json_path,
                                                    char **out_variant_name,
                                                    char **out_variant_object) {
    char *text = NULL;
    char *summary = NULL;
    char *final_key = NULL;
    char *final_value = NULL;
    char *final_end = NULL;
    char *solution = NULL;
    char *variant_key = NULL;
    char *colon = NULL;
    const char *object_start = NULL;
    const char *object_end = NULL;
    char *name_copy = NULL;
    char *object_copy = NULL;
    size_t name_len;
    size_t object_len;

    if (out_variant_name) *out_variant_name = NULL;
    if (out_variant_object) *out_variant_object = NULL;
    if (!json_path || !out_variant_name || !out_variant_object) return 0;
    if (!read_file_text(json_path, &text)) return 0;

    summary = strstr(text, "\"summary\"");
    if (!summary) goto cleanup;
    final_key = strstr(summary, "\"status\"");
    if (final_key) final_key = strstr(final_key, "\"final\"");
    if (!final_key) final_key = strstr(summary, "\"final\"");
    if (!final_key) goto cleanup;
    final_value = strchr(final_key + strlen("\"final\""), '"');
    if (!final_value) goto cleanup;
    final_value++;
    final_end = strchr(final_value, '"');
    if (!final_end || final_end == final_value) goto cleanup;

    name_len = (size_t)(final_end - final_value);
    name_copy = (char*)malloc(name_len + 1);
    if (!name_copy) goto cleanup;
    memcpy(name_copy, final_value, name_len);
    name_copy[name_len] = '\0';

    solution = strstr(text, "\"solution\"");
    if (!solution) goto cleanup;
    {
        char variant_pattern[128];
        snprintf(variant_pattern, sizeof(variant_pattern), "\"%s\"", name_copy);
        variant_key = strstr(solution, variant_pattern);
    }
    if (!variant_key) goto cleanup;

    colon = strchr(variant_key, ':');
    if (!colon) goto cleanup;
    object_start = skip_ws(colon + 1);
    if (!object_start || *object_start != '{') goto cleanup;
    object_end = find_matching_brace(object_start);
    if (!object_end) goto cleanup;

    object_len = (size_t)(object_end - object_start + 1);
    object_copy = (char*)malloc(object_len + 1);
    if (!object_copy) goto cleanup;
    memcpy(object_copy, object_start, object_len);
    object_copy[object_len] = '\0';

    *out_variant_name = name_copy;
    *out_variant_object = object_copy;
    free(text);
    return 1;

cleanup:
    free(name_copy);
    free(object_copy);
    free(text);
    return 0;
}

static int parse_station_order_from_order_json(const char *json_path, int **out_ids, int *out_n) {
    char *text = NULL;
    char *key = NULL;
    char *outer = NULL;
    char *p = NULL;
    int *ids = NULL;
    int count = 0;
    int cap = 0;
    int depth = 0;

    if (out_ids) *out_ids = NULL;
    if (out_n) *out_n = 0;
    if (!read_file_text(json_path, &text)) return 0;

    key = strstr(text, "\"tour_segments_station_ids\"");
    if (!key) {
        free(text);
        return 0;
    }
    outer = strchr(key, '[');
    if (!outer) {
        free(text);
        return 0;
    }
    p = outer + 1;
    depth = 1;
    while (*p) {
        char *endptr = NULL;
        long val;
        if (*p == ']') {
            depth--;
            if (depth == 0) break;
            p++;
            continue;
        }
        if (*p == '[') {
            depth++;
            p++;
            continue;
        }
        if (!((*p >= '0' && *p <= '9') || *p == '-')) {
            p++;
            continue;
        }
        val = strtol(p, &endptr, 10);
        if (endptr == p) {
            p++;
            continue;
        }
        if (!append_int_local(&ids, &count, &cap, (int)val)) {
            free(ids);
            free(text);
            return 0;
        }
        p = endptr;
    }

    free(text);
    if (out_ids) *out_ids = ids;
    if (out_n) *out_n = count;
    return 1;
}

static int parse_int_array_after_key_text(const char *text,
                                          const char *key_name,
                                          int **out_ids,
                                          int *out_n) {
    char pattern[128];
    char *key = NULL;
    char *outer = NULL;
    char *p = NULL;
    int *ids = NULL;
    int count = 0;
    int cap = 0;
    int depth = 0;

    if (out_ids) *out_ids = NULL;
    if (out_n) *out_n = 0;
    if (!text || !key_name || !out_ids || !out_n) return 0;

    snprintf(pattern, sizeof(pattern), "\"%s\"", key_name);
    key = strstr((char*)text, pattern);
    if (!key) return 0;
    outer = strchr(key, '[');
    if (!outer) return 0;

    p = outer + 1;
    depth = 1;
    while (*p) {
        char *endptr = NULL;
        long val;
        if (*p == ']') {
            depth--;
            if (depth == 0) break;
            p++;
            continue;
        }
        if (*p == '[') {
            depth++;
            p++;
            continue;
        }
        if (!((*p >= '0' && *p <= '9') || *p == '-')) {
            p++;
            continue;
        }
        val = strtol(p, &endptr, 10);
        if (endptr == p) {
            p++;
            continue;
        }
        if (!append_int_local(&ids, &count, &cap, (int)val)) {
            free(ids);
            return 0;
        }
        p = endptr;
    }

    *out_ids = ids;
    *out_n = count;
    return 1;
}

static int parse_segmented_station_order_json(const char *json_path,
                                              int ***out_segments,
                                              int **out_counts,
                                              int *out_segment_count,
                                              int **out_dock_location_ids,
                                              int *out_dock_count) {
    char *text = NULL;
    char *key = NULL;
    char *outer = NULL;
    char *p = NULL;
    int **segments = NULL;
    int *counts = NULL;
    int segment_cap = 0;
    int segment_count = 0;
    int current_cap = 0;
    int *current = NULL;
    int current_n = 0;
    int depth = 0;
    int in_segment = 0;

    if (out_segments) *out_segments = NULL;
    if (out_counts) *out_counts = NULL;
    if (out_segment_count) *out_segment_count = 0;
    if (out_dock_location_ids) *out_dock_location_ids = NULL;
    if (out_dock_count) *out_dock_count = 0;

    if (!json_path || !out_segments || !out_counts || !out_segment_count) return 0;
    if (!read_file_text(json_path, &text)) return 0;

    key = strstr(text, "\"tour_segments_station_ids\"");
    if (!key) goto fail;
    outer = strchr(key, '[');
    if (!outer) goto fail;

    p = outer + 1;
    depth = 1;
    while (*p) {
        if (*p == '[') {
            depth++;
            if (depth == 2) {
                current = NULL;
                current_cap = 0;
                current_n = 0;
                in_segment = 1;
            }
            p++;
            continue;
        }
        if (*p == ']') {
            if (depth == 2 && in_segment) {
                int **segments_tmp;
                int *counts_tmp;
                if (segment_count >= segment_cap) {
                    int new_cap = (segment_cap == 0) ? 8 : (segment_cap * 2);
                    segments_tmp = (int**)realloc(segments, (size_t)new_cap * sizeof(int*));
                    counts_tmp = (int*)realloc(counts, (size_t)new_cap * sizeof(int));
                    if (!segments_tmp || !counts_tmp) {
                        free(segments_tmp);
                        free(counts_tmp);
                        goto fail;
                    }
                    segments = segments_tmp;
                    counts = counts_tmp;
                    segment_cap = new_cap;
                }
                segments[segment_count] = current;
                counts[segment_count] = current_n;
                segment_count++;
                current = NULL;
                current_cap = 0;
                current_n = 0;
                in_segment = 0;
            }
            depth--;
            if (depth == 0) break;
            p++;
            continue;
        }
        if (((*p >= '0' && *p <= '9') || *p == '-') && in_segment) {
            char *endptr = NULL;
            long val = strtol(p, &endptr, 10);
            if (endptr != p) {
                if (!append_int_local(&current, &current_n, &current_cap, (int)val)) goto fail;
                p = endptr;
                continue;
            }
        }
        p++;
    }

    if (out_dock_location_ids && out_dock_count) {
        if (!parse_int_array_after_key_text(text, "dock_location_ids", out_dock_location_ids, out_dock_count)) {
            *out_dock_location_ids = NULL;
            *out_dock_count = 0;
        }
    }

    free(text);
    *out_segments = segments;
    *out_counts = counts;
    *out_segment_count = segment_count;
    return 1;

fail:
    free(current);
    free(text);
    free_segmented_station_order(segments, counts, segment_count);
    return 0;
}

static const char *infer_strategy_from_path(const char *path) {
    if (!path) return "noport";
    if (strstr(path, "fixedport") || strstr(path, "fixed_port") || strstr(path, "fixed-port")) return "fixedport";
    if (strstr(path, "noport") || strstr(path, "no_port") || strstr(path, "no-port")) return "noport";
    if (strstr(path, "\\nn\\") || strstr(path, "/nn/")) return "nn";
    if (strstr(path, "\\ge\\") || strstr(path, "/ge/")) return "ge";
    if (strstr(path, "\\ci\\") || strstr(path, "/ci/")) return "ci";
    return "order";
}

static int find_station_idx_by_table_id(const nn_instance_t *inst, int station_id) {
    for (int i = 0; i < inst->num_stations; i++) {
        if (inst->nodes[i].table_id == station_id) return i;
    }
    return -1;
}

static int segment_from_order(const nn_instance_t *inst,
                              const int *station_order,
                              int station_order_n,
                              nn_solution_t *sol,
                              int boat_start_loc_id,
                              int boat_end_loc_id,
                              int boat_capacity) {
    int tour_cap = 256;
    int *tour = (int*)malloc((size_t)tour_cap * sizeof(int));
    int *segment_starts = NULL, seg_starts_cap = 0;
    int *segment_ends = NULL, seg_ends_cap = 0;
    int *segment_catches = NULL, seg_catches_cap = 0;
    double *segment_dists = NULL; int seg_dists_cap = 0;
    int *visit_station_ids = NULL, visit_ids_cap = 0;
    int *visit_station_segment = NULL, visit_seg_cap = 0;
    int *visit_station_direction = NULL, visit_dir_cap = 0;
    int segment_count = 0;
    int visit_station_count = 0;
    int tour_len = 0;
    int current_loc_id = boat_start_loc_id;
    int current_load = 0;
    double current_segment_dist = 0.0;
    int segment_start_idx = 0;

    if (!tour) return -1;

    for (int ord = 0; ord < station_order_n; ord++) {
        int station_id = station_order[ord];
        int station_idx = find_station_idx_by_table_id(inst, station_id);
        int station_amount;
        int stat_entry = -1;
        int stat_exit = -1;
        double stat_added = 0.0;
        int stat_dir = 0;

        if (station_idx < 0) {
            fprintf(stderr, "Station %d from ordered input not found in DB\n", station_id);
            return -1;
        }

        station_amount = inst->nodes[station_idx].amount;
        if (current_load > 0 && current_load + station_amount > boat_capacity) {
            int nearest_port = find_nearest_port(inst, current_loc_id);
            int new_loc = 0;
            int new_seg_start = 0;
            if (nearest_port < 0) {
                fprintf(stderr, "No port available before station %d overflow\n", station_id);
                return -1;
            }
            if (!insert_port_segment(inst, nearest_port, current_loc_id,
                    &tour, &tour_cap, &tour_len,
                    &segment_starts, &seg_starts_cap,
                    &segment_ends, &seg_ends_cap,
                    &segment_catches, &seg_catches_cap,
                    &segment_dists, &seg_dists_cap,
                    &segment_count, segment_start_idx,
                    current_load, &current_segment_dist,
                    &new_loc, &new_seg_start)) {
                return -1;
            }
            current_loc_id = new_loc;
            current_load = 0;
            segment_start_idx = new_seg_start;
        }

        if (!choose_station_orientation_with_dir(inst, current_loc_id, station_idx,
                                                 &stat_entry, &stat_exit,
                                                 &stat_added, &stat_dir)) {
            fprintf(stderr, "Cannot orient station %d from location %d\n", station_id, current_loc_id);
            return -1;
        }

        if (!grow_int_array(&tour, &tour_cap, tour_len + ((stat_exit != stat_entry) ? 2 : 1)) ||
            !grow_int_array(&visit_station_ids, &visit_ids_cap, visit_station_count + 1) ||
            !grow_int_array(&visit_station_segment, &visit_seg_cap, visit_station_count + 1) ||
            !grow_int_array(&visit_station_direction, &visit_dir_cap, visit_station_count + 1)) {
            return -1;
        }

        if (stat_added > 0.0) current_segment_dist += stat_added;
        tour[tour_len++] = stat_entry;
        if (stat_exit != stat_entry) tour[tour_len++] = stat_exit;
        (void)stat_dir;

        current_loc_id = stat_exit;
        current_load += station_amount;
        visit_station_ids[visit_station_count] = inst->nodes[station_idx].table_id;
        visit_station_segment[visit_station_count] = segment_count;
        visit_station_direction[visit_station_count] = stat_dir;
        visit_station_count++;

        if (current_load >= boat_capacity && ord + 1 < station_order_n) {
            int nearest_port = find_nearest_port(inst, current_loc_id);
            int new_loc = 0;
            int new_seg_start = 0;
            if (nearest_port >= 0) {
                if (!insert_port_segment(inst, nearest_port, current_loc_id,
                        &tour, &tour_cap, &tour_len,
                        &segment_starts, &seg_starts_cap,
                        &segment_ends, &seg_ends_cap,
                        &segment_catches, &seg_catches_cap,
                        &segment_dists, &seg_dists_cap,
                        &segment_count, segment_start_idx,
                        current_load, &current_segment_dist,
                        &new_loc, &new_seg_start)) {
                    return -1;
                }
                current_loc_id = new_loc;
                current_load = 0;
                segment_start_idx = new_seg_start;
            }
        }
    }

    if (current_load > 0 || segment_count == 0) {
        if (!flush_final_segment(&segment_starts, &seg_starts_cap,
                                 &segment_ends, &seg_ends_cap,
                                 &segment_catches, &seg_catches_cap,
                                 &segment_dists, &seg_dists_cap,
                                 &segment_count, segment_start_idx,
                                 tour_len, current_load, current_segment_dist)) {
            return -1;
        }
    }

    sol->tour = tour;
    sol->tour_length = tour_len;
    sol->visit_station_ids = visit_station_ids;
    sol->visit_station_count = visit_station_count;
    sol->visit_station_segment = visit_station_segment;
    sol->visit_station_direction = visit_station_direction;
    sol->segment_count = segment_count;
    sol->segment_starts = segment_starts;
    sol->segment_ends = segment_ends;
    sol->segment_catches = segment_catches;
    sol->segment_dists = segment_dists;
    sol->total_distance = 0.0;
    sol->total_catch = 0;

    for (int i = 0; i < segment_count; i++) {
        sol->total_distance += segment_dists[i];
        sol->total_catch += segment_catches[i];
    }
    if (tour_len > 0) {
        double return_dist = get_distance(inst, tour[tour_len - 1], boat_end_loc_id);
        if (return_dist > 0.0) sol->total_distance += return_dist;
    } else {
        double return_dist = get_distance(inst, boat_start_loc_id, boat_end_loc_id);
        if (return_dist > 0.0) sol->total_distance += return_dist;
    }

    return 0;
}

static int segment_from_segmented_input(const nn_instance_t *inst,
                                        int **station_segments,
                                        const int *station_segment_counts,
                                        int segment_count,
                                        const int *dock_location_ids,
                                        int dock_count,
                                        nn_solution_t *sol,
                                        int boat_start_loc_id,
                                        int boat_end_loc_id,
                                        int boat_capacity) {
    int *tour = NULL, tour_cap = 0, tour_len = 0;
    int *segment_starts = NULL, seg_starts_cap = 0;
    int *segment_ends = NULL, seg_ends_cap = 0;
    int *segment_catches = NULL, seg_catches_cap = 0;
    double *segment_dists = NULL;
    int seg_dists_cap = 0;
    int *visit_station_ids = NULL, visit_ids_cap = 0;
    int *visit_station_segment = NULL, visit_seg_cap = 0;
    int *visit_station_direction = NULL, visit_dir_cap = 0;
    int visit_station_count = 0;

    if (!inst || !station_segments || !station_segment_counts || !sol) return -1;
    if (segment_count <= 0 || !dock_location_ids || dock_count != segment_count + 1) return -1;

    memset(sol, 0, sizeof(*sol));

    for (int s = 0; s < segment_count; s++) {
        int current_loc_id = dock_location_ids[s];
        int segment_start_idx = tour_len;
        int segment_catch = 0;
        double segment_dist = 0.0;
        int end_dock_loc_id = dock_location_ids[s + 1];

        for (int i = 0; i < station_segment_counts[s]; i++) {
            int signed_station_id = station_segments[s][i];
            int station_id = abs(signed_station_id);
            int station_idx = find_station_idx_by_table_id(inst, station_id);
            int stat_entry;
            int stat_exit;
            int stat_dir;
            double transit_dist;
            double haul_dist;

            if (station_idx < 0) goto fail;

            stat_dir = (signed_station_id < 0) ? -1 : 1;
            stat_entry = (stat_dir < 0) ? inst->nodes[station_idx].end_loc_id : inst->nodes[station_idx].start_loc_id;
            stat_exit = (stat_dir < 0) ? inst->nodes[station_idx].start_loc_id : inst->nodes[station_idx].end_loc_id;
            transit_dist = order_distance_or_zero(inst, current_loc_id, stat_entry);
            haul_dist = order_distance_or_zero(inst, stat_entry, stat_exit);

            if (!grow_int_array(&tour, &tour_cap, tour_len + ((stat_exit != stat_entry) ? 2 : 1)) ||
                !grow_int_array(&visit_station_ids, &visit_ids_cap, visit_station_count + 1) ||
                !grow_int_array(&visit_station_segment, &visit_seg_cap, visit_station_count + 1) ||
                !grow_int_array(&visit_station_direction, &visit_dir_cap, visit_station_count + 1)) goto fail;

            segment_dist += transit_dist + haul_dist;
            tour[tour_len++] = stat_entry;
            if (stat_exit != stat_entry) tour[tour_len++] = stat_exit;
            current_loc_id = stat_exit;

            visit_station_ids[visit_station_count] = station_id;
            visit_station_segment[visit_station_count] = s;
            visit_station_direction[visit_station_count] = stat_dir;
            visit_station_count++;
            segment_catch += inst->nodes[station_idx].amount;
        }

        if (segment_catch > boat_capacity) goto fail;

        if (s < segment_count - 1) {
            double dock_dist = order_distance_or_zero(inst, current_loc_id, end_dock_loc_id);
            if (!grow_int_array(&tour, &tour_cap, tour_len + 1)) goto fail;
            segment_dist += dock_dist;
            tour[tour_len++] = end_dock_loc_id;
        }

        if (!grow_int_array(&segment_starts, &seg_starts_cap, s + 1) ||
            !grow_int_array(&segment_ends, &seg_ends_cap, s + 1) ||
            !grow_int_array(&segment_catches, &seg_catches_cap, s + 1) ||
            !grow_dist_array(&segment_dists, &seg_dists_cap, s + 1)) goto fail;

        segment_starts[s] = segment_start_idx;
        segment_ends[s] = (tour_len > segment_start_idx) ? (tour_len - 1) : (segment_start_idx - 1);
        segment_catches[s] = segment_catch;
        segment_dists[s] = segment_dist;
    }

    sol->tour = tour;
    sol->tour_length = tour_len;
    sol->visit_station_ids = visit_station_ids;
    sol->visit_station_count = visit_station_count;
    sol->visit_station_segment = visit_station_segment;
    sol->visit_station_direction = visit_station_direction;
    sol->segment_count = segment_count;
    sol->segment_starts = segment_starts;
    sol->segment_ends = segment_ends;
    sol->segment_catches = segment_catches;
    sol->segment_dists = segment_dists;
    sol->total_catch = 0;
    sol->total_distance = 0.0;
    for (int s = 0; s < segment_count; s++) {
        sol->total_catch += segment_catches[s];
        sol->total_distance += segment_dists[s];
    }
    if (segment_count > 0) {
        int last_loc_id = (tour_len > 0) ? tour[tour_len - 1] : boat_start_loc_id;
        sol->total_distance += order_distance_or_zero(inst, last_loc_id, boat_end_loc_id);
    } else {
        sol->total_distance += order_distance_or_zero(inst, boat_start_loc_id, boat_end_loc_id);
    }

    return 0;

fail:
    free(tour);
    free(segment_starts);
    free(segment_ends);
    free(segment_catches);
    free(segment_dists);
    free(visit_station_ids);
    free(visit_station_segment);
    free(visit_station_direction);
    memset(sol, 0, sizeof(*sol));
    return -1;
}

static void free_solution(nn_solution_t *sol) {
    init_free_solution(sol);
}

static void free_instance(nn_instance_t *inst) {
    free(inst->nodes);
    if (inst->distances) {
        for (int i = 0; i < inst->max_loc_id; i++) free(inst->distances[i]);
    }
    free(inst->distances);
    free(inst->loc_to_idx);
    memset(inst, 0, sizeof(*inst));
}

int main(int argc, char **argv) {
    const char *database = NULL;
    const char *config = NULL;
    const char *input = NULL;
    const char *output = NULL;
    const char *strategy = NULL;
    sqlite3 *db = NULL;
    nn_instance_t inst = {0};
    nn_solution_t sol = {0};
    sqlite3_stmt *stmt = NULL;
    double boat_capacity = 0.0;
    int boat_start_loc_id = 0;
    int boat_end_loc_id = 0;
    double boat_start_lat = 0.0;
    double boat_start_lon = 0.0;
    char boat_name[256] = "Unknown";
    int boat_id;
    int *station_order = NULL;
    int station_order_n = 0;
    int **station_segments = NULL;
    int *station_segment_counts = NULL;
    int station_segment_count = 0;
    int *dock_location_ids = NULL;
    int dock_location_count = 0;
    int is_feasible = 1;
    nn_solution_t pre_local_postopt_sol = {0};
    double mip_time_limit_seconds = 0.0;
    double init_haul_distance_scale = 0.0;
    double local_postopt_runtime_seconds = 0.0;
    int local_postopt_segment_solve_count = 0;
    gsp_mip_solve_detail_t *local_postopt_details = NULL;
    int local_postopt_detail_count = 0;
    struct timespec t0, t1, t_seg_end, t_postopt_end;

    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--database") == 0) database = argv[i + 1];
        else if (strcmp(argv[i], "--config") == 0) config = argv[i + 1];
        else if (strcmp(argv[i], "--input") == 0) input = argv[i + 1];
        else if (strcmp(argv[i], "--output") == 0) output = argv[i + 1];
        else if (strcmp(argv[i], "--strategy") == 0) strategy = argv[i + 1];
    }

    if (!database || !config || !input || !output) {
        fprintf(stderr, "Usage: %s --database <db> --config <yaml> --input <construction.json> --output <segment.json> [--strategy nn|ge|ci|noport|fixedport]\n", argv[0]);
        return 1;
    }
    if (!strategy) strategy = infer_strategy_from_path(input);
    init_haul_distance_scale = read_init_haul_distance_scale_from_yaml(config);

    clock_gettime(CLOCK_MONOTONIC, &t0);
    boat_id = read_boat_id_from_yaml(config);

    if (!parse_station_order_from_order_json(input, &station_order, &station_order_n)) {
        fprintf(stderr, "Failed to read station order from %s\n", input);
        return 1;
    }
    if (!parse_segmented_station_order_json(input,
                                            &station_segments,
                                            &station_segment_counts,
                                            &station_segment_count,
                                            &dock_location_ids,
                                            &dock_location_count)) {
        station_segments = NULL;
        station_segment_counts = NULL;
        station_segment_count = 0;
        dock_location_ids = NULL;
        dock_location_count = 0;
    }

    if (sqlite3_open(database, &db) != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        free(station_order);
        return 1;
    }

    if (load_nodes(db, &inst) != 0 || load_distance_matrix(db, &inst) != 0) {
        fprintf(stderr, "Failed to load init instance from database\n");
        sqlite3_close(db);
        free(station_order);
        free_instance(&inst);
        return 1;
    }

    {
        const char *boat_sql =
            "SELECT b.name, b.capacity, b.location_id, l.lat, l.lon "
            "FROM boats b JOIN locations l ON l.id = b.location_id WHERE b.id = ?;";
        if (sqlite3_prepare_v2(db, boat_sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        free(station_order);
        free_instance(&inst);
        return 1;
    }
    }
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

    clock_gettime(CLOCK_MONOTONIC, &t1);

    printf("Segment from construction input\n");
    printf("  input:    %s\n", input);
    printf("  output:   %s\n", output);
    printf("  strategy: %s\n", strategy);
    printf("  boat:     %s (id=%d)\n", boat_name, boat_id);
    printf("  stations: %d\n", station_order_n);
    printf("  capacity: %.0f\n", boat_capacity);
    printf("[INIT] Building capacity-feasible segmentation from construction input\n");
    fflush(stdout);

    if (station_segment_count > 1 &&
        segment_from_segmented_input(&inst,
                                     station_segments,
                                     station_segment_counts,
                                     station_segment_count,
                                     dock_location_ids,
                                     dock_location_count,
                                     &sol,
                                     boat_start_loc_id,
                                     boat_end_loc_id,
                                     (int)boat_capacity) == 0) {
        printf("[INIT] Preserved %d pre-segmented construction segments\n", station_segment_count);
    } else {
        if (segment_from_order(&inst, station_order, station_order_n, &sol,
                               boat_start_loc_id, boat_end_loc_id, (int)boat_capacity) != 0) {
            fprintf(stderr, "Failed to segment construction input\n");
            sqlite3_close(db);
            free(station_order);
            free_segmented_station_order(station_segments, station_segment_counts, station_segment_count);
            free(dock_location_ids);
            free_solution(&sol);
            free_instance(&inst);
            return 1;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &t_seg_end);

    if (!init_copy_solution(&sol, &pre_local_postopt_sol)) {
        fprintf(stderr, "Failed to copy pre-local-postopt init solution\n");
        sqlite3_close(db);
        free(station_order);
        free_segmented_station_order(station_segments, station_segment_counts, station_segment_count);
        free(dock_location_ids);
        free_solution(&sol);
        free_instance(&inst);
        return 1;
    }

    printf("[INIT] Pre-postopt baseline: %d segments, %.2f nm\n",
           pre_local_postopt_sol.segment_count, pre_local_postopt_sol.total_distance);
    fflush(stdout);

    mip_time_limit_seconds = read_init_mip_time_limit_from_yaml(config);
    if (!init_apply_local_postopt(&inst, &pre_local_postopt_sol,
                                  boat_start_loc_id, boat_end_loc_id,
                                  mip_time_limit_seconds,
                                  init_haul_distance_scale,
                                  &sol,
                                  &local_postopt_runtime_seconds,
                                  &local_postopt_segment_solve_count,
                                  &local_postopt_details,
                                  &local_postopt_detail_count)) {
        fprintf(stderr, "Failed to apply local post optimization to init solution\n");
        sqlite3_close(db);
        free(station_order);
        free_segmented_station_order(station_segments, station_segment_counts, station_segment_count);
        free(dock_location_ids);
        free_solution(&sol);
        free_solution(&pre_local_postopt_sol);
        free_instance(&inst);
        return 1;
    }

    clock_gettime(CLOCK_MONOTONIC, &t_postopt_end);

    if (!stations_have_no_duplicates(sol.visit_station_ids, sol.visit_station_count)) is_feasible = 0;
    if (!segments_within_capacity(sol.segment_catches, sol.segment_count, boat_capacity)) is_feasible = 0;

    write_json(db, output, &inst, &sol, boat_id,
               &pre_local_postopt_sol,
               local_postopt_details, local_postopt_detail_count,
               input,
               boat_name,
               strategy, "segment_from_order",
               boat_start_loc_id, boat_end_loc_id, boat_capacity,
               boat_start_lat, boat_start_lon, is_feasible,
               elapsed_seconds(t0, t1), elapsed_seconds(t1, t_seg_end),
               local_postopt_runtime_seconds,
               mip_time_limit_seconds);

    printf("[OK] Wrote %s\n", output);
    printf("  segments: %d\n", sol.segment_count);
    printf("  distance: %.2f -> %.2f nm\n",
           pre_local_postopt_sol.total_distance, sol.total_distance);
    printf("  local post-opt: solves=%d runtime=%.4f s time_limit=%s%.0f\n",
           local_postopt_segment_solve_count,
           local_postopt_runtime_seconds,
           (mip_time_limit_seconds > 0.0) ? "" : "uncapped ",
           (mip_time_limit_seconds > 0.0) ? mip_time_limit_seconds : 0.0);
    printf("  feasible: %s\n", is_feasible ? "true" : "false");

    sqlite3_close(db);
    free(station_order);
    free_segmented_station_order(station_segments, station_segment_counts, station_segment_count);
    free(dock_location_ids);
    free_solution(&sol);
    free_solution(&pre_local_postopt_sol);
    free(local_postopt_details);
    free_instance(&inst);
    return 0;
}
