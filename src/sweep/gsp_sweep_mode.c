#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <float.h>

#include <sqlite3.h>

#include "../include/init_types.h"
#include "../include/feasibility.h"
#include "../init/init_utils.h"

#ifdef HAVE_GUROBI
#include <gurobi_c.h>
#include "../mip/include/mip_endpaired_tsp.h"
#endif

#ifndef __stdcall
#define __stdcall
#endif

#define MAX_LINE 1024
#define SWEEP_EPS 1e-6

typedef struct {
    int *signed_station_ids;
    int count;
    int capacity;
    int catch_amount;
    int start_loc_id;
    int end_loc_id;
    double distance_nm;
} sweep_segment_t;

typedef struct {
    nn_solution_t solution;
    int pass_index;
    int changed;
    int boundary_changes;
    double pass_runtime_seconds;
    double total_runtime_seconds;
    int feasible;
} sweep_snapshot_t;

typedef struct {
    int boat_id;
    char boat_name[256];
    double boat_capacity;
    int boat_start_loc_id;
    int boat_end_loc_id;
    double boat_start_lat;
    double boat_start_lon;
} sweep_boat_t;

static double elapsed_seconds(struct timespec start, struct timespec end) {
    return (double)(end.tv_sec - start.tv_sec) +
           (double)(end.tv_nsec - start.tv_nsec) / 1e9;
}

static void free_solution(nn_solution_t *sol) {
    if (!sol) return;
    free(sol->tour);
    free(sol->visit_station_ids);
    free(sol->visit_station_segment);
    free(sol->visit_station_direction);
    free(sol->segment_starts);
    free(sol->segment_ends);
    free(sol->segment_catches);
    free(sol->segment_dists);
    memset(sol, 0, sizeof(*sol));
}

static void free_segments(sweep_segment_t *segments, int n_segments) {
    if (!segments) return;
    for (int i = 0; i < n_segments; i++) {
        free(segments[i].signed_station_ids);
    }
    free(segments);
}

static int append_int(int **arr, int *count, int *capacity, int value) {
    if (*count >= *capacity) {
        int new_capacity = (*capacity <= 0) ? 8 : (*capacity * 2);
        int *tmp = (int*)realloc(*arr, (size_t)new_capacity * sizeof(int));
        if (!tmp) return 0;
        *arr = tmp;
        *capacity = new_capacity;
    }
    (*arr)[(*count)++] = value;
    return 1;
}

static int append_snapshot(sweep_snapshot_t **arr, int *count, int *capacity,
                           const sweep_snapshot_t *snapshot) {
    if (*count >= *capacity) {
        int new_capacity = (*capacity <= 0) ? 8 : (*capacity * 2);
        sweep_snapshot_t *tmp = (sweep_snapshot_t*)realloc(
            *arr, (size_t)new_capacity * sizeof(sweep_snapshot_t));
        if (!tmp) return 0;
        *arr = tmp;
        *capacity = new_capacity;
    }
    (*arr)[*count] = *snapshot;
    (*count)++;
    return 1;
}

static int copy_solution(nn_solution_t *dst, const nn_solution_t *src) {
    if (!dst || !src) return 0;
    memset(dst, 0, sizeof(*dst));

    dst->tour_length = src->tour_length;
    dst->visit_station_count = src->visit_station_count;
    dst->segment_count = src->segment_count;
    dst->total_distance = src->total_distance;
    dst->total_catch = src->total_catch;

    if (src->tour_length > 0) {
        dst->tour = (int*)malloc((size_t)src->tour_length * sizeof(int));
        if (!dst->tour) goto fail;
        memcpy(dst->tour, src->tour, (size_t)src->tour_length * sizeof(int));
    }
    if (src->visit_station_count > 0) {
        dst->visit_station_ids = (int*)malloc((size_t)src->visit_station_count * sizeof(int));
        dst->visit_station_segment = (int*)malloc((size_t)src->visit_station_count * sizeof(int));
        dst->visit_station_direction = (int*)malloc((size_t)src->visit_station_count * sizeof(int));
        if (!dst->visit_station_ids || !dst->visit_station_segment || !dst->visit_station_direction) goto fail;
        memcpy(dst->visit_station_ids, src->visit_station_ids,
               (size_t)src->visit_station_count * sizeof(int));
        memcpy(dst->visit_station_segment, src->visit_station_segment,
               (size_t)src->visit_station_count * sizeof(int));
        memcpy(dst->visit_station_direction, src->visit_station_direction,
               (size_t)src->visit_station_count * sizeof(int));
    }
    if (src->segment_count > 0) {
        dst->segment_starts = (int*)malloc((size_t)src->segment_count * sizeof(int));
        dst->segment_ends = (int*)malloc((size_t)src->segment_count * sizeof(int));
        dst->segment_catches = (int*)malloc((size_t)src->segment_count * sizeof(int));
        dst->segment_dists = (double*)malloc((size_t)src->segment_count * sizeof(double));
        if (!dst->segment_starts || !dst->segment_ends || !dst->segment_catches || !dst->segment_dists) goto fail;
        memcpy(dst->segment_starts, src->segment_starts, (size_t)src->segment_count * sizeof(int));
        memcpy(dst->segment_ends, src->segment_ends, (size_t)src->segment_count * sizeof(int));
        memcpy(dst->segment_catches, src->segment_catches, (size_t)src->segment_count * sizeof(int));
        memcpy(dst->segment_dists, src->segment_dists, (size_t)src->segment_count * sizeof(double));
    }
    return 1;

fail:
    free_solution(dst);
    return 0;
}

static void free_snapshot_array(sweep_snapshot_t *snapshots, int n_snapshots) {
    if (!snapshots) return;
    for (int i = 0; i < n_snapshots; i++) {
        free_solution(&snapshots[i].solution);
    }
    free(snapshots);
}

static int read_boat_id_from_yaml(const char *yaml_path) {
    FILE *fp = fopen(yaml_path, "r");
    char line[MAX_LINE];
    int boat_id = 2;

    if (!fp) {
        fprintf(stderr, "Warning: Cannot open %s, using default boat_id=2\n", yaml_path);
        return boat_id;
    }

    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "boat:")) {
            while (fgets(line, sizeof(line), fp)) {
                if (strstr(line, "id:")) {
                    boat_id = atoi(line + strcspn(line, "0123456789"));
                    fclose(fp);
                    return boat_id;
                }
                if (line[0] != ' ' && line[0] != '\t') break;
            }
        }
    }

    fclose(fp);
    return boat_id;
}

static char *read_text_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    long size;
    char *buf;

    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);

    buf = (char*)malloc((size_t)size + 1u);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    if (size > 0 && fread(buf, 1, (size_t)size, fp) != (size_t)size) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    buf[size] = '\0';
    fclose(fp);
    return buf;
}

static const char *skip_ws(const char *p) {
    while (p && *p && isspace((unsigned char)*p)) p++;
    return p;
}

static const char *find_json_key(const char *text, const char *key) {
    char pattern[128];
    if (!text || !key) return NULL;
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    return strstr(text, pattern);
}

static const char *find_key_in_object(const char *object_start, const char *key) {
    const char *key_pos = find_json_key(object_start, key);
    if (!key_pos) return NULL;
    key_pos = strchr(key_pos, ':');
    if (!key_pos) return NULL;
    return skip_ws(key_pos + 1);
}

static int parse_json_int(const char **pp, int *out_value) {
    char *endptr;
    long value;
    const char *p = skip_ws(*pp);
    if (!p || !*p) return 0;
    value = strtol(p, &endptr, 10);
    if (endptr == p) return 0;
    *out_value = (int)value;
    *pp = endptr;
    return 1;
}

static int parse_int_array(const char **pp, int **out_arr, int *out_count) {
    const char *p = skip_ws(*pp);
    int *arr = NULL;
    int count = 0;
    int capacity = 0;

    if (!p || *p != '[') return 0;
    p++;
    p = skip_ws(p);

    while (*p && *p != ']') {
        int value = 0;
        if (!parse_json_int(&p, &value)) {
            free(arr);
            return 0;
        }
        if (!append_int(&arr, &count, &capacity, value)) {
            free(arr);
            return 0;
        }
        p = skip_ws(p);
        if (*p == ',') {
            p++;
            p = skip_ws(p);
        } else if (*p != ']') {
            free(arr);
            return 0;
        }
    }

    if (*p != ']') {
        free(arr);
        return 0;
    }

    *pp = p + 1;
    *out_arr = arr;
    *out_count = count;
    return 1;
}

static int parse_nested_int_arrays(const char *text, const char *key,
                                   int ***out_arrays, int **out_sizes, int *out_count) {
    const char *p = find_json_key(text, key);
    int **arrays = NULL;
    int *sizes = NULL;
    int count = 0;
    int capacity = 0;

    if (!p) return 0;
    p = strchr(p, ':');
    if (!p) return 0;
    p = skip_ws(p + 1);
    if (*p != '[') return 0;
    p++;
    p = skip_ws(p);

    while (*p && *p != ']') {
        int *arr = NULL;
        int n_arr = 0;
        int new_capacity;
        void *tmp;

        if (!parse_int_array(&p, &arr, &n_arr)) goto fail;
        if (count >= capacity) {
            new_capacity = (capacity <= 0) ? 8 : capacity * 2;
            tmp = realloc(arrays, (size_t)new_capacity * sizeof(int*));
            if (!tmp) goto fail;
            arrays = (int**)tmp;
            tmp = realloc(sizes, (size_t)new_capacity * sizeof(int));
            if (!tmp) goto fail;
            sizes = (int*)tmp;
            capacity = new_capacity;
        }
        arrays[count] = arr;
        sizes[count] = n_arr;
        count++;

        p = skip_ws(p);
        if (*p == ',') {
            p++;
            p = skip_ws(p);
        } else if (*p != ']') {
            goto fail;
        }
    }

    if (*p != ']') goto fail;

    *out_arrays = arrays;
    *out_sizes = sizes;
    *out_count = count;
    return 1;

fail:
    if (arrays) {
        for (int i = 0; i < count; i++) free(arrays[i]);
    }
    free(arrays);
    free(sizes);
    return 0;
}

static int load_nodes(sqlite3 *db, nn_instance_t *inst) {
    sqlite3_stmt *stmt = NULL;
    const char *count_sql =
        "SELECT (SELECT COUNT(*) FROM stations), (SELECT COUNT(*) FROM ports)";
    int num_stations = 0;
    int num_ports = 0;
    int total_nodes;

    if (sqlite3_prepare_v2(db, count_sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        num_stations = sqlite3_column_int(stmt, 0);
        num_ports = sqlite3_column_int(stmt, 1);
    }
    sqlite3_finalize(stmt);

    total_nodes = num_stations + num_ports;
    if (num_stations <= 0 || total_nodes <= 0) return 0;

    inst->num_stations = num_stations;
    inst->num_ports = num_ports;
    inst->nodes = (nn_node_t*)calloc((size_t)total_nodes, sizeof(nn_node_t));
    if (!inst->nodes) return 0;

    if (sqlite3_prepare_v2(db,
            "SELECT id, start_location_id, end_location_id, amount FROM stations ORDER BY id",
            -1, &stmt, NULL) != SQLITE_OK) return 0;
    for (int i = 0; i < num_stations && sqlite3_step(stmt) == SQLITE_ROW; i++) {
        inst->nodes[i].table_id = sqlite3_column_int(stmt, 0);
        inst->nodes[i].start_loc_id = sqlite3_column_int(stmt, 1);
        inst->nodes[i].end_loc_id = sqlite3_column_int(stmt, 2);
        inst->nodes[i].amount = sqlite3_column_int(stmt, 3);
        inst->nodes[i].is_port = 0;
    }
    sqlite3_finalize(stmt);

    if (sqlite3_prepare_v2(db,
            "SELECT id, location_id FROM ports ORDER BY id",
            -1, &stmt, NULL) != SQLITE_OK) return 0;
    for (int i = num_stations; i < total_nodes && sqlite3_step(stmt) == SQLITE_ROW; i++) {
        int loc_id = sqlite3_column_int(stmt, 1);
        inst->nodes[i].table_id = sqlite3_column_int(stmt, 0);
        inst->nodes[i].start_loc_id = loc_id;
        inst->nodes[i].end_loc_id = loc_id;
        inst->nodes[i].amount = 0;
        inst->nodes[i].is_port = 1;
    }
    sqlite3_finalize(stmt);
    return 1;
}

static int load_distance_matrix(sqlite3 *db, nn_instance_t *inst) {
    sqlite3_stmt *stmt = NULL;
    int max_loc_id = 0;

    if (sqlite3_prepare_v2(db, "SELECT MAX(id) FROM locations", -1, &stmt, NULL) != SQLITE_OK) return 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) max_loc_id = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    inst->max_loc_id = max_loc_id + 1;
    inst->distances = (double**)malloc((size_t)inst->max_loc_id * sizeof(double*));
    inst->loc_to_idx = (int*)malloc((size_t)inst->max_loc_id * sizeof(int));
    if (!inst->distances || !inst->loc_to_idx) return 0;

    for (int i = 0; i < inst->max_loc_id; i++) {
        inst->distances[i] = (double*)malloc((size_t)inst->max_loc_id * sizeof(double));
        if (!inst->distances[i]) return 0;
        inst->loc_to_idx[i] = i;
        for (int j = 0; j < inst->max_loc_id; j++) inst->distances[i][j] = -1.0;
    }

    if (sqlite3_prepare_v2(db,
            "SELECT from_location_id, to_location_id, distance_nm FROM distances",
            -1, &stmt, NULL) != SQLITE_OK) return 0;
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
    return 1;
}

static void free_instance(nn_instance_t *inst) {
    if (!inst) return;
    free(inst->nodes);
    if (inst->distances) {
        for (int i = 0; i < inst->max_loc_id; i++) free(inst->distances[i]);
    }
    free(inst->distances);
    free(inst->loc_to_idx);
    memset(inst, 0, sizeof(*inst));
}

static int load_boat(sqlite3 *db, int boat_id, sweep_boat_t *boat) {
    sqlite3_stmt *stmt = NULL;
    const char *sql =
        "SELECT b.name, b.capacity, b.start_location_id, b.end_location_id, l.lat, l.lon "
        "FROM boats b JOIN locations l ON l.id = b.start_location_id WHERE b.id = ?";

    memset(boat, 0, sizeof(*boat));
    boat->boat_id = boat_id;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, boat_id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *name = sqlite3_column_text(stmt, 0);
        if (name) snprintf(boat->boat_name, sizeof(boat->boat_name), "%s", (const char*)name);
        boat->boat_capacity = sqlite3_column_double(stmt, 1);
        boat->boat_start_loc_id = sqlite3_column_int(stmt, 2);
        boat->boat_end_loc_id = sqlite3_column_int(stmt, 3);
        boat->boat_start_lat = sqlite3_column_double(stmt, 4);
        boat->boat_start_lon = sqlite3_column_double(stmt, 5);
        sqlite3_finalize(stmt);
        return 1;
    }

    sqlite3_finalize(stmt);
    return 0;
}

static int find_station_index(const nn_instance_t *inst, int station_id) {
    for (int i = 0; i < inst->num_stations; i++) {
        if (inst->nodes[i].table_id == station_id) return i;
    }
    return -1;
}

static int station_amount(const nn_instance_t *inst, int station_id) {
    int idx = find_station_index(inst, station_id);
    return (idx >= 0) ? inst->nodes[idx].amount : 0;
}

static int compute_segment_catch(const nn_instance_t *inst, const int *signed_station_ids, int count) {
    int total = 0;
    for (int i = 0; i < count; i++) total += station_amount(inst, abs(signed_station_ids[i]));
    return total;
}

static int extract_segment_arrays_from_input(const char *json_text,
                                             int ***segment_arrays,
                                             int **segment_sizes,
                                             int *segment_count) {
    const char *summary_pos = find_json_key(json_text, "summary");
    if (summary_pos) {
        const char *final_value = find_key_in_object(summary_pos, "final");
        if (final_value && *final_value == '"') {
            char variant_name[64];
            int len = 0;
            final_value++;
            while (final_value[len] && final_value[len] != '"' && len < (int)sizeof(variant_name) - 1) {
                variant_name[len] = final_value[len];
                len++;
            }
            variant_name[len] = '\0';
            if (len > 0) {
                const char *solution_pos = find_json_key(json_text, "solution");
                if (solution_pos) {
                    const char *variant_pos = find_json_key(solution_pos, variant_name);
                    if (variant_pos) {
                        const char *candidate = find_json_key(variant_pos, "tour_segments_station_ids");
                        if (candidate) {
                            return parse_nested_int_arrays(candidate, "tour_segments_station_ids",
                                                           segment_arrays, segment_sizes, segment_count);
                        }
                    }
                }
            }
        }
    }
    return parse_nested_int_arrays(json_text, "tour_segments_station_ids",
                                   segment_arrays, segment_sizes, segment_count);
}

static int load_segments_from_json(const char *input_path, const nn_instance_t *inst,
                                   const sweep_boat_t *boat,
                                   sweep_segment_t **out_segments, int *out_count,
                                   nn_solution_t *initial_solution) {
    char *json_text = read_text_file(input_path);
    int **segment_arrays = NULL;
    int *segment_sizes = NULL;
    int segment_count = 0;
    sweep_segment_t *segments = NULL;
    int rc = 0;

    if (!json_text) return 0;
    if (!extract_segment_arrays_from_input(json_text, &segment_arrays, &segment_sizes, &segment_count)) goto cleanup;
    if (segment_count <= 0) goto cleanup;

    segments = (sweep_segment_t*)calloc((size_t)segment_count, sizeof(sweep_segment_t));
    if (!segments) goto cleanup;

    for (int i = 0; i < segment_count; i++) {
        sweep_segment_t *seg = &segments[i];
        seg->count = segment_sizes[i];
        seg->capacity = segment_sizes[i];
        seg->signed_station_ids = segment_arrays[i];
        segment_arrays[i] = NULL;
        seg->start_loc_id = (i == 0) ? boat->boat_start_loc_id : 0;
        seg->end_loc_id = (i == segment_count - 1) ? boat->boat_end_loc_id : 0;
        seg->catch_amount = compute_segment_catch(inst, seg->signed_station_ids, seg->count);
    }

    for (int i = 0; i < segment_count - 1; i++) {
        int last_station_id = abs(segments[i].signed_station_ids[segments[i].count - 1]);
        int last_station_idx = find_station_index(inst, last_station_id);
        int nearest_port_idx = find_nearest_port_from_node_pair(inst, last_station_idx);
        if (nearest_port_idx < 0) goto cleanup;
        segments[i].end_loc_id = inst->nodes[nearest_port_idx].start_loc_id;
        segments[i + 1].start_loc_id = inst->nodes[nearest_port_idx].start_loc_id;
    }

    memset(initial_solution, 0, sizeof(*initial_solution));
    rc = 1;

cleanup:
    if (!rc) {
        free_segments(segments, segment_count);
    }
    if (segment_arrays) {
        for (int i = 0; i < segment_count; i++) free(segment_arrays[i]);
    }
    free(segment_arrays);
    free(segment_sizes);
    free(json_text);
    if (rc) {
        *out_segments = segments;
        *out_count = segment_count;
    }
    return rc;
}

#ifdef HAVE_GUROBI
static int solve_segment_tsp(GRBenv *env, const nn_instance_t *inst,
                             int start_loc_id, int end_loc_id,
                             const int *station_ids, int n_stations,
                             int *out_catch, double *out_distance,
                             int **out_signed_station_ids) {
    mip_endpaired_instance_t mip_instance;
    mip_endpaired_solution_t mip_solution;
    mip_params_t mip_params;
    int *instance_station_ids = NULL;
    int *start_loc_ids = NULL;
    int *end_loc_ids = NULL;
    int *amounts = NULL;

    if (n_stations <= 0) return 0;
    memset(&mip_instance, 0, sizeof(mip_instance));
    memset(&mip_solution, 0, sizeof(mip_solution));
    memset(&mip_params, 0, sizeof(mip_params));

    instance_station_ids = (int*)malloc((size_t)n_stations * sizeof(int));
    start_loc_ids = (int*)malloc((size_t)n_stations * sizeof(int));
    end_loc_ids = (int*)malloc((size_t)n_stations * sizeof(int));
    amounts = (int*)malloc((size_t)n_stations * sizeof(int));
    if (!instance_station_ids || !start_loc_ids || !end_loc_ids || !amounts) goto fail;

    for (int i = 0; i < n_stations; i++) {
        int station_idx = find_station_index(inst, station_ids[i]);
        if (station_idx < 0) goto fail;
        instance_station_ids[i] = station_ids[i];
        start_loc_ids[i] = inst->nodes[station_idx].start_loc_id;
        end_loc_ids[i] = inst->nodes[station_idx].end_loc_id;
        amounts[i] = inst->nodes[station_idx].amount;
    }

    mip_instance.num_stations = n_stations;
    mip_instance.station_ids = instance_station_ids;
    mip_instance.station_start_loc_ids = start_loc_ids;
    mip_instance.station_end_loc_ids = end_loc_ids;
    mip_instance.station_amounts = amounts;
    mip_instance.distances = inst->distances;
    mip_instance.max_location_id = inst->max_loc_id;

    mip_params.verbose = 0;
    mip_params.shared_env = env;

    if (solve_mip_endpaired_tsp(&mip_instance, &mip_params,
                                start_loc_id, end_loc_id, &mip_solution) != 0) {
        goto fail;
    }

    *out_catch = mip_solution.catch_amount;
    *out_distance = mip_solution.total_distance_nm;
    *out_signed_station_ids = mip_solution.signed_station_ids;
    mip_solution.signed_station_ids = NULL;

    free_mip_endpaired_solution(&mip_solution);
    free(instance_station_ids);
    free(start_loc_ids);
    free(end_loc_ids);
    free(amounts);
    return 1;

fail:
    free_mip_endpaired_solution(&mip_solution);
    free(instance_station_ids);
    free(start_loc_ids);
    free(end_loc_ids);
    free(amounts);
    return 0;
}
#endif

static int segment_to_solution(const nn_instance_t *inst, const sweep_boat_t *boat,
                               const sweep_segment_t *segments, int n_segments,
                               nn_solution_t *sol) {
    int visit_station_count = 0;
    int tour_length = 0;

    memset(sol, 0, sizeof(*sol));
    for (int s = 0; s < n_segments; s++) {
        visit_station_count += segments[s].count;
        tour_length += segments[s].count + ((s < n_segments - 1) ? 1 : 0);
    }

    sol->tour = (int*)malloc((size_t)tour_length * sizeof(int));
    sol->visit_station_ids = (int*)malloc((size_t)visit_station_count * sizeof(int));
    sol->visit_station_segment = (int*)malloc((size_t)visit_station_count * sizeof(int));
    sol->visit_station_direction = (int*)malloc((size_t)visit_station_count * sizeof(int));
    sol->segment_starts = (int*)malloc((size_t)n_segments * sizeof(int));
    sol->segment_ends = (int*)malloc((size_t)n_segments * sizeof(int));
    sol->segment_catches = (int*)malloc((size_t)n_segments * sizeof(int));
    sol->segment_dists = (double*)malloc((size_t)n_segments * sizeof(double));
    if (!sol->tour || !sol->visit_station_ids || !sol->visit_station_segment ||
        !sol->visit_station_direction || !sol->segment_starts || !sol->segment_ends ||
        !sol->segment_catches || !sol->segment_dists) {
        free_solution(sol);
        return 0;
    }

    (void)boat;
    for (int s = 0; s < n_segments; s++) {
        sol->segment_starts[s] = sol->tour_length;
        for (int i = 0; i < segments[s].count; i++) {
            int signed_id = segments[s].signed_station_ids[i];
            int station_idx = find_station_index(inst, abs(signed_id));
            int direction = (signed_id < 0) ? -1 : 1;
            if (station_idx < 0) {
                free_solution(sol);
                return 0;
            }
            int entry_loc = (direction > 0) ? inst->nodes[station_idx].start_loc_id : inst->nodes[station_idx].end_loc_id;
            sol->tour[sol->tour_length++] = entry_loc;
            sol->visit_station_ids[sol->visit_station_count] = abs(signed_id);
            sol->visit_station_segment[sol->visit_station_count] = s;
            sol->visit_station_direction[sol->visit_station_count] = direction;
            sol->visit_station_count++;
        }
        if (s < n_segments - 1) {
            sol->tour[sol->tour_length++] = segments[s].end_loc_id;
        }
        sol->segment_ends[s] = sol->tour_length - 1;
        sol->segment_catches[s] = segments[s].catch_amount;
        sol->segment_dists[s] = segments[s].distance_nm;
        sol->segment_count++;
        sol->total_distance += segments[s].distance_nm;
        sol->total_catch += segments[s].catch_amount;
    }

    return 1;
}

static int solution_is_feasible(const nn_instance_t *inst, const sweep_boat_t *boat,
                                const nn_solution_t *sol) {
    return segments_within_capacity(sol->segment_catches, sol->segment_count, boat->boat_capacity) &&
           stations_are_unique_and_complete(sol->visit_station_ids, sol->visit_station_count, inst->num_stations);
}

#ifdef HAVE_GUROBI
static int reoptimize_segment(GRBenv *env, const nn_instance_t *inst, sweep_segment_t *segment) {
    int *station_ids = NULL;
    int catch_amount = 0;
    double distance_nm = 0.0;
    int *signed_ids = NULL;

    if (segment->count <= 0) return 1;
    station_ids = (int*)malloc((size_t)segment->count * sizeof(int));
    if (!station_ids) return 0;
    for (int i = 0; i < segment->count; i++) station_ids[i] = abs(segment->signed_station_ids[i]);

    if (!solve_segment_tsp(env, inst, segment->start_loc_id, segment->end_loc_id,
                           station_ids, segment->count,
                           &catch_amount, &distance_nm, &signed_ids)) {
        free(station_ids);
        return 0;
    }

    free(station_ids);
    free(segment->signed_station_ids);
    segment->signed_station_ids = signed_ids;
    segment->catch_amount = catch_amount;
    segment->distance_nm = distance_nm;
    return 1;
}

static int reoptimize_all_segments(GRBenv *env, const nn_instance_t *inst,
                                   sweep_segment_t *segments, int n_segments) {
    for (int i = 0; i < n_segments; i++) {
        if (!reoptimize_segment(env, inst, &segments[i])) return 0;
    }
    return 1;
}

static int optimize_boundary(GRBenv *env, const nn_instance_t *inst, const sweep_boat_t *boat,
                             sweep_segment_t *left, sweep_segment_t *right) {
    int total = left->count + right->count;
    int *merged = (int*)malloc((size_t)total * sizeof(int));
    double current_total = left->distance_nm + right->distance_nm;
    double best_total = current_total - SWEEP_EPS;
    int best_k = -1;
    int *best_left_ids = NULL;
    int *best_right_ids = NULL;
    int best_left_catch = 0, best_right_catch = 0;
    double best_left_dist = 0.0, best_right_dist = 0.0;

    if (!merged) return 0;
    for (int i = 0; i < left->count; i++) merged[i] = abs(left->signed_station_ids[i]);
    for (int i = 0; i < right->count; i++) merged[left->count + i] = abs(right->signed_station_ids[i]);

    for (int k = 1; k < total; k++) {
        int left_catch = 0;
        int right_catch = 0;
        double left_dist = 0.0;
        double right_dist = 0.0;
        int *left_ids = NULL;
        int *right_ids = NULL;

        for (int i = 0; i < k; i++) left_catch += station_amount(inst, merged[i]);
        for (int i = k; i < total; i++) right_catch += station_amount(inst, merged[i]);
        if (left_catch > (int)boat->boat_capacity || right_catch > (int)boat->boat_capacity) continue;

        if (!solve_segment_tsp(env, inst, left->start_loc_id, left->end_loc_id,
                               merged, k, &left_catch, &left_dist, &left_ids)) {
            continue;
        }
        if (!solve_segment_tsp(env, inst, right->start_loc_id, right->end_loc_id,
                               merged + k, total - k, &right_catch, &right_dist, &right_ids)) {
            free(left_ids);
            continue;
        }

        if (left_dist + right_dist + SWEEP_EPS < best_total) {
            free(best_left_ids);
            free(best_right_ids);
            best_left_ids = left_ids;
            best_right_ids = right_ids;
            best_left_catch = left_catch;
            best_right_catch = right_catch;
            best_left_dist = left_dist;
            best_right_dist = right_dist;
            best_total = left_dist + right_dist;
            best_k = k;
        } else {
            free(left_ids);
            free(right_ids);
        }
    }

    free(merged);
    if (best_k < 0) {
        free(best_left_ids);
        free(best_right_ids);
        return 0;
    }

    free(left->signed_station_ids);
    free(right->signed_station_ids);
    left->signed_station_ids = best_left_ids;
    right->signed_station_ids = best_right_ids;
    left->count = best_k;
    right->count = total - best_k;
    left->catch_amount = best_left_catch;
    right->catch_amount = best_right_catch;
    left->distance_nm = best_left_dist;
    right->distance_nm = best_right_dist;
    return 1;
}
#endif

static void write_solution_json(FILE *fp, const nn_solution_t *sol,
                                const sweep_boat_t *boat, int feasible) {
    fprintf(fp, "      \"dock_location_ids\": [");
    fprintf(fp, "%d", boat->boat_start_loc_id);
    for (int s = 0; s < sol->segment_count - 1; s++) {
        fprintf(fp, ", %d", sol->tour[sol->segment_ends[s]]);
    }
    fprintf(fp, ", %d],\n", boat->boat_end_loc_id);

    fprintf(fp, "      \"tour_segments_station_ids\": [\n");
    for (int s = 0; s < sol->segment_count; s++) {
        int first = 1;
        fprintf(fp, "        [");
        for (int i = 0; i < sol->visit_station_count; i++) {
            if (sol->visit_station_segment[i] != s) continue;
            if (!first) fprintf(fp, ", ");
            fprintf(fp, "%d", sol->visit_station_ids[i] *
                              ((sol->visit_station_direction[i] < 0) ? -1 : 1));
            first = 0;
        }
        fprintf(fp, "]%s\n", (s + 1 < sol->segment_count) ? "," : "");
    }
    fprintf(fp, "      ],\n");

    fprintf(fp, "      \"tour_length\": [");
    for (int s = 0; s < sol->segment_count; s++) {
        fprintf(fp, "%d", sol->segment_ends[s] - sol->segment_starts[s] + 1);
        if (s + 1 < sol->segment_count) fprintf(fp, ", ");
    }
    fprintf(fp, "],\n");

    fprintf(fp, "      \"segment_count\": %d,\n", sol->segment_count);

    fprintf(fp, "      \"segment_catch_amount\": [");
    for (int s = 0; s < sol->segment_count; s++) {
        fprintf(fp, "%d", sol->segment_catches[s]);
        if (s + 1 < sol->segment_count) fprintf(fp, ", ");
    }
    fprintf(fp, "],\n");

    fprintf(fp, "      \"segment_distance_nm\": [");
    for (int s = 0; s < sol->segment_count; s++) {
        fprintf(fp, "%.2f", sol->segment_dists[s]);
        if (s + 1 < sol->segment_count) fprintf(fp, ", ");
    }
    fprintf(fp, "],\n");

    fprintf(fp, "      \"total_distance_nm\": %.2f,\n", sol->total_distance);
    fprintf(fp, "      \"feasible\": %s\n", feasible ? "true" : "false");
}

static void write_pass_entry(FILE *fp,
                             const char *pass_name,
                             const sweep_snapshot_t *snapshot,
                             const sweep_boat_t *boat) {
    fprintf(fp, "    \"%s\": {\n", pass_name);
    fprintf(fp, "      \"pass\": %d,\n", snapshot->pass_index);
    fprintf(fp, "      \"changed\": %d,\n", snapshot->changed);
    fprintf(fp, "      \"boundary_changes\": %d,\n", snapshot->boundary_changes);
    write_solution_json(fp, &snapshot->solution, boat, snapshot->feasible);
    fprintf(fp, "\n    }");
}

static int write_sweep_json(const char *output_path,
                            const sweep_boat_t *boat,
                            const nn_instance_t *inst,
                            const sweep_snapshot_t *snapshots,
                            int snapshot_count,
                            int total_boundary_changes,
                            double total_runtime_seconds,
                            const char *strategy_name) {
    FILE *fp = fopen(output_path, "w");
    char final_pass_name[32];
    if (!fp) return 0;

    fprintf(fp, "{\n");
    fprintf(fp, "  \"metadata\": {\n");
    fprintf(fp, "    \"solver_version\": \"sweep_1.0\",\n");
    fprintf(fp, "    \"timestamp\": \"%ld\",\n", (long)time(NULL));
    fprintf(fp, "    \"mode\": \"sweep\",\n");
    fprintf(fp, "    \"strategy\": \"%s\",\n", strategy_name ? strategy_name : "sweep");
    fprintf(fp, "    \"boat_id\": %d,\n", boat->boat_id);
    fprintf(fp, "    \"boat_name\": \"%s\",\n", boat->boat_name);
    fprintf(fp, "    \"home_port\": {\"lat\": %.6f, \"lon\": %.6f},\n",
            boat->boat_start_lat, boat->boat_start_lon);
    fprintf(fp, "    \"boat_location_ids\": [%d, %d]\n",
            boat->boat_start_loc_id, boat->boat_end_loc_id);
    fprintf(fp, "  },\n");

    fprintf(fp, "  \"problem\": {\n");
    fprintf(fp, "    \"num_stations\": %d,\n", inst->num_stations);
    fprintf(fp, "    \"capacity\": %.0f\n", boat->boat_capacity);
    fprintf(fp, "  },\n");

    fprintf(fp, "  \"solution\": {\n");
    for (int i = 0; i < snapshot_count; i++) {
        char pass_name[32];
        snprintf(pass_name, sizeof(pass_name), "pass%d", snapshots[i].pass_index);
        write_pass_entry(fp, pass_name, &snapshots[i], boat);
        fprintf(fp, "%s\n", (i + 1 < snapshot_count) ? "," : "");
    }
    fprintf(fp, "  },\n");

    snprintf(final_pass_name, sizeof(final_pass_name), "pass%d", snapshots[snapshot_count - 1].pass_index);
    fprintf(fp, "  \"summary\": {\n");
    fprintf(fp, "    \"final\": \"%s\",\n", final_pass_name);
    fprintf(fp, "    \"status\": \"sweep_complete\",\n");
    fprintf(fp, "    \"feasible\": %s,\n", snapshots[snapshot_count - 1].feasible ? "true" : "false");
    fprintf(fp, "    \"total_distance_nm\": [");
    for (int i = 0; i < snapshot_count; i++) {
        fprintf(fp, "%.2f", snapshots[i].solution.total_distance);
        if (i + 1 < snapshot_count) fprintf(fp, ", ");
    }
    fprintf(fp, "],\n");
    fprintf(fp, "    \"final_total_distance_nm\": %.2f,\n", snapshots[snapshot_count - 1].solution.total_distance);
    fprintf(fp, "    \"preprocessing_seconds\": 0.0,\n");
    fprintf(fp, "    \"solution_runtime_seconds\": [");
    for (int i = 0; i < snapshot_count; i++) {
        fprintf(fp, "%.6f", snapshots[i].pass_runtime_seconds);
        if (i + 1 < snapshot_count) fprintf(fp, ", ");
    }
    fprintf(fp, "],\n");
    fprintf(fp, "    \"postprocessing_seconds\": 0.0,\n");
    fprintf(fp, "    \"total_runtime_seconds\": %.6f,\n", total_runtime_seconds);
    fprintf(fp, "    \"pass_count\": %d,\n", snapshot_count);
    fprintf(fp, "    \"total_boundary_changes\": %d,\n", total_boundary_changes);
    fprintf(fp, "    \"method\": \"%s\"\n", strategy_name ? strategy_name : "sweep");
    fprintf(fp, "  }\n");
    fprintf(fp, "}\n");

    fclose(fp);
    return 1;
}

static void parse_sweep_args(int argc, char **argv,
                             const char **strategy, const char **database,
                             const char **config, const char **input,
                             const char **output, int *time_limit) {
    *strategy = NULL;
    *database = NULL;
    *config = NULL;
    *input = NULL;
    *output = NULL;
    *time_limit = 0;

    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--strategy") == 0) *strategy = argv[i + 1];
        else if (strcmp(argv[i], "--database") == 0) *database = argv[i + 1];
        else if (strcmp(argv[i], "--config") == 0) *config = argv[i + 1];
        else if (strcmp(argv[i], "--input") == 0) *input = argv[i + 1];
        else if (strcmp(argv[i], "--output") == 0) *output = argv[i + 1];
        else if (strcmp(argv[i], "--time-limit") == 0) *time_limit = atoi(argv[i + 1]);
    }
}

int mode_sweep(int argc, char **argv) {
    const char *strategy = NULL, *database = NULL, *config = NULL, *input = NULL, *output = NULL;
    int time_limit = 0;
    sqlite3 *db = NULL;
    nn_instance_t inst = {0};
    sweep_boat_t boat;
    sweep_segment_t *segments = NULL;
    int segment_count = 0;
    nn_solution_t current_solution = {0};
    sweep_snapshot_t *snapshots = NULL;
    int snapshot_count = 0, snapshot_capacity = 0;
    int total_boundary_changes = 0;
    int rc = 1;
    struct timespec t_start, t_pass_start, t_pass_end, t_now;

    printf("============================================================\n");
    printf("GSP Solver - Phase 1: Sweep\n");
    printf("============================================================\n\n");

    parse_sweep_args(argc, argv, &strategy, &database, &config, &input, &output, &time_limit);
    if (!strategy || !database || !config || !input || !output) {
        fprintf(stderr, "ERROR: sweep requires --strategy, --database, --config, --input, and --output\n");
        goto cleanup;
    }

#ifndef HAVE_GUROBI
    fprintf(stderr, "ERROR: sweep mode requires Gurobi support at build time\n");
    goto cleanup;
#else
    clock_gettime(CLOCK_MONOTONIC, &t_start);
    if (sqlite3_open(database, &db) != SQLITE_OK) {
        fprintf(stderr, "ERROR: Cannot open database: %s\n", sqlite3_errmsg(db));
        goto cleanup;
    }
    if (!load_nodes(db, &inst) || !load_distance_matrix(db, &inst)) {
        fprintf(stderr, "ERROR: Failed to load nodes or distances\n");
        goto cleanup;
    }
    if (!load_boat(db, read_boat_id_from_yaml(config), &boat)) {
        fprintf(stderr, "ERROR: Failed to load boat from config/database\n");
        goto cleanup;
    }
    if (!load_segments_from_json(input, &inst, &boat, &segments, &segment_count, &current_solution)) {
        fprintf(stderr, "ERROR: Failed to load segmented solution from %s\n", input);
        goto cleanup;
    }

    {
        GRBenv *env = NULL;
        int *active = NULL;
        int pass_index = 1;
        int keep_running = 1;

        if (GRBloadenv(&env, NULL) != 0) {
            fprintf(stderr, "ERROR: Failed to create Gurobi environment for sweep\n");
            goto cleanup;
        }
        GRBsetintparam(env, "OutputFlag", 0);
        GRBsetintparam(env, "LogToConsole", 0);

        if (!reoptimize_all_segments(env, &inst, segments, segment_count)) {
            fprintf(stderr, "ERROR: Failed to reoptimize sweep segments\n");
            GRBfreeenv(env);
            goto cleanup;
        }
        if (!segment_to_solution(&inst, &boat, segments, segment_count, &current_solution)) {
            GRBfreeenv(env);
            goto cleanup;
        }

        {
            sweep_snapshot_t snapshot;
            memset(&snapshot, 0, sizeof(snapshot));
            clock_gettime(CLOCK_MONOTONIC, &t_now);
            snapshot.pass_index = 0;
            snapshot.changed = 1;
            snapshot.boundary_changes = 0;
            snapshot.pass_runtime_seconds = 0.0;
            snapshot.total_runtime_seconds = elapsed_seconds(t_start, t_now);
            snapshot.feasible = solution_is_feasible(&inst, &boat, &current_solution);
            if (!copy_solution(&snapshot.solution, &current_solution)) {
                GRBfreeenv(env);
                goto cleanup;
            }
            if (!append_snapshot(&snapshots, &snapshot_count, &snapshot_capacity, &snapshot)) {
                free_solution(&snapshot.solution);
                GRBfreeenv(env);
                goto cleanup;
            }
            if (!write_sweep_json(output, &boat, &inst,
                                  snapshots, snapshot_count,
                                  total_boundary_changes, snapshot.total_runtime_seconds,
                                  strategy)) {
                fprintf(stderr, "ERROR: Failed to persist initial sweep snapshot JSON\n");
                GRBfreeenv(env);
                goto cleanup;
            }
        }

        active = (int*)malloc((size_t)((segment_count > 1) ? segment_count - 1 : 1) * sizeof(int));
        if (!active) {
            GRBfreeenv(env);
            goto cleanup;
        }
        for (int i = 0; i < segment_count - 1; i++) active[i] = 1;

        while (keep_running) {
            int boundary_changes = 0;
            int changed = 0;
            sweep_snapshot_t snapshot;

            clock_gettime(CLOCK_MONOTONIC, &t_pass_start);
            memset(&snapshot, 0, sizeof(snapshot));

            for (int b = 0; b < segment_count - 1; b++) {
                if (!active[b]) continue;
                active[b] = 0;
                if (optimize_boundary(env, &inst, &boat, &segments[b], &segments[b + 1])) {
                    changed = 1;
                    boundary_changes++;
                    total_boundary_changes++;
                    if (b > 0) active[b - 1] = 1;
                    active[b] = 1;
                    if (b + 1 < segment_count - 1) active[b + 1] = 1;
                }
            }

            free_solution(&current_solution);
            if (!segment_to_solution(&inst, &boat, segments, segment_count, &current_solution)) {
                free(active);
                GRBfreeenv(env);
                goto cleanup;
            }

            clock_gettime(CLOCK_MONOTONIC, &t_pass_end);
            clock_gettime(CLOCK_MONOTONIC, &t_now);
            snapshot.pass_index = pass_index;
            snapshot.changed = changed;
            snapshot.boundary_changes = boundary_changes;
            snapshot.pass_runtime_seconds = elapsed_seconds(t_pass_start, t_pass_end);
            snapshot.total_runtime_seconds = elapsed_seconds(t_start, t_now);
            snapshot.feasible = solution_is_feasible(&inst, &boat, &current_solution);
            if (!copy_solution(&snapshot.solution, &current_solution)) {
                free(active);
                GRBfreeenv(env);
                goto cleanup;
            }
            if (!append_snapshot(&snapshots, &snapshot_count, &snapshot_capacity, &snapshot)) {
                free_solution(&snapshot.solution);
                free(active);
                GRBfreeenv(env);
                goto cleanup;
            }
            if (!write_sweep_json(output, &boat, &inst,
                                  snapshots, snapshot_count,
                                  total_boundary_changes, snapshot.total_runtime_seconds,
                                  strategy)) {
                fprintf(stderr, "ERROR: Failed to persist sweep snapshot JSON\n");
                free(active);
                GRBfreeenv(env);
                goto cleanup;
            }

            pass_index++;
            if (!changed) keep_running = 0;
            if (time_limit > 0 && snapshot.total_runtime_seconds >= (double)time_limit) keep_running = 0;
        }

        free(active);
        GRBfreeenv(env);
    }

    rc = 0;
#endif

cleanup:
    if (db) sqlite3_close(db);
    free_instance(&inst);
    free_segments(segments, segment_count);
    free_solution(&current_solution);
    free_snapshot_array(snapshots, snapshot_count);
    return rc;
}
