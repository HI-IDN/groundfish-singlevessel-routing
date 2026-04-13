#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#include <sqlite3.h>

#include "../include/init_types.h"
#include "../include/feasibility.h"
#include "../include/json_utils.h"
#include "../include/mip_report.h"
#include "../init/init_utils.h"
#include "../init/segment_postopt.h"

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
    nn_solution_t solution;
    int pass_index;
    int changed;
    int boundary_count;
    int *boundary_port_ids;
    double *boundary_improvement_gain_nm;
    int boundary_attempts;
    int boundary_changes;
    int mip_solve_count;
    int mip_detail_count;
    int mip_detail_capacity;
    gsp_mip_solve_detail_t *mip_solve_details;
    double pass_runtime_seconds;
    double total_runtime_seconds;
    int feasible;
} sweep_snapshot_t;

typedef struct {
    int mip_time_limit_seconds;
    int global_time_limit_seconds;
    int max_iterations;
    double haul_distance_scale;
} sweep_config_t;

static int count_segment_station_changes(const gsp_route_segment_t *before,
                                         const gsp_route_segment_t *after);

static double elapsed_seconds(struct timespec start, struct timespec end) {
    return (double)(end.tv_sec - start.tv_sec) +
           (double)(end.tv_nsec - start.tv_nsec) / 1e9;
}

static void log_progress(const char *message) {
    if (!message) return;
    printf("%s\n", message);
    fflush(stdout);
}

static int contains_int(const int *arr, int count, int value) {
    for (int i = 0; i < count; i++) {
        if (arr[i] == value) return 1;
    }
    return 0;
}

static int compare_int_asc(const void *a, const void *b) {
    const int ia = *(const int*)a;
    const int ib = *(const int*)b;
    return (ia > ib) - (ia < ib);
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

static void free_segments(gsp_route_segment_t *segments, int n_segments) {
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

static int lookup_port_info(sqlite3 *db, int location_id, int *port_id, char *port_name, size_t port_name_size) {
    sqlite3_stmt *stmt = NULL;
    const char *sql = "SELECT id, name FROM ports WHERE location_id = ?;";
    int found = 0;

    if (port_id) *port_id = 0;
    if (port_name && port_name_size > 0) port_name[0] = '\0';
    if (!db || location_id <= 0) return 0;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, location_id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        if (port_id) *port_id = sqlite3_column_int(stmt, 0);
        if (port_name && port_name_size > 0) {
            const unsigned char *txt = sqlite3_column_text(stmt, 1);
            snprintf(port_name, port_name_size, "%s", txt ? (const char*)txt : "");
        }
        found = 1;
    }
    sqlite3_finalize(stmt);
    return found;
}

static void print_segment_station_order(const gsp_route_segment_t *segments, int segment_count) {
    printf("Final segment station order:\n");
    for (int s = 0; s < segment_count; s++) {
        printf("  seg%d: [", s + 1);
        for (int i = 0; i < segments[s].count; i++) {
            if (i) printf(", ");
            printf("%d", abs(segments[s].signed_station_ids[i]));
        }
        printf("]\n");
    }
    fflush(stdout);
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
        free(snapshots[i].boundary_port_ids);
        free(snapshots[i].boundary_improvement_gain_nm);
        free(snapshots[i].mip_solve_details);
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

static int read_int_after_colon(const char *line, int default_value) {
    const char *p = strchr(line, ':');
    if (!p) return default_value;
    while (*p && !isdigit((unsigned char)*p) && *p != '-') p++;
    if (!*p) return default_value;
    return atoi(p);
}

static void read_sweep_config_from_yaml(const char *yaml_path, sweep_config_t *cfg) {
    FILE *fp;
    char line[MAX_LINE];
    int section = 0;

    if (!cfg) return;
    cfg->mip_time_limit_seconds = 0;
    cfg->global_time_limit_seconds = 0;
    cfg->max_iterations = 0;
    cfg->haul_distance_scale = read_sweep_haul_distance_scale_from_yaml(yaml_path);

    fp = fopen(yaml_path, "r");
    if (!fp) return;

    while (fgets(line, sizeof(line), fp)) {
        char *trim = line;
        while (*trim == ' ' || *trim == '\t') trim++;
        if (*trim == '#' || *trim == '\0' || *trim == '\n') continue;

        if (trim == line && strncmp(trim, "global_time_limit_seconds:", 26) == 0) {
            cfg->global_time_limit_seconds = read_int_after_colon(trim, cfg->global_time_limit_seconds);
            section = 0;
            continue;
        }

        if (trim == line && strchr(trim, ':')) {
            if (strncmp(trim, "sweep:", 6) == 0) section = 1;
            else if (strncmp(trim, "gurobi:", 7) == 0) section = 2;
            else section = 0;
            continue;
        }

        if (section == 1 && strncmp(trim, "max_iterations:", 15) == 0) {
            cfg->max_iterations = read_int_after_colon(trim, cfg->max_iterations);
        }
    }

    fclose(fp);
    cfg->mip_time_limit_seconds = (int)read_sweep_mip_time_limit_from_yaml(yaml_path);
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

static int parse_json_double(const char **pp, double *out_value);
static int parse_named_double_value(const char *text, const char *key, double *out_value);

static const char *find_key_in_object(const char *object_start, const char *key) {
    const char *key_pos = find_json_key(object_start, key);
    if (!key_pos) return NULL;
    key_pos = strchr(key_pos, ':');
    if (!key_pos) return NULL;
    return skip_ws(key_pos + 1);
}

static int extract_summary_final_variant_name(const char *summary_pos, char *variant_name, size_t variant_size) {
    const char *status_pos;
    const char *final_value;
    int len = 0;

    if (!summary_pos || !variant_name || variant_size == 0) return 0;
    status_pos = find_key_in_object(summary_pos, "status");
    final_value = status_pos ? find_key_in_object(status_pos, "final") : NULL;
    if ((!final_value || *final_value != '"')) {
        final_value = find_key_in_object(summary_pos, "final");
    }
    if (!final_value || *final_value != '"') return 0;
    final_value++;
    while (final_value[len] && final_value[len] != '"' && len < (int)variant_size - 1) {
        variant_name[len] = final_value[len];
        len++;
    }
    variant_name[len] = '\0';
    return len > 0;
}

static int extract_summary_final_total_distance(const char *summary_pos, double *out_value) {
    const char *distance_pos;
    const char *final_value;
    if (!summary_pos || !out_value) return 0;
    distance_pos = find_key_in_object(summary_pos, "distance_nm");
    final_value = distance_pos ? find_key_in_object(distance_pos, "final") : NULL;
    if (final_value) {
        const char *p = final_value;
        return parse_json_double(&p, out_value);
    }
    return parse_named_double_value(summary_pos, "final_total_distance_nm", out_value);
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

static int parse_json_double(const char **pp, double *out_value) {
    char *endptr;
    double value;
    const char *p = skip_ws(*pp);
    if (!p || !*p) return 0;
    value = strtod(p, &endptr);
    if (endptr == p) return 0;
    *out_value = value;
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

static int lookup_waypoint_path(sqlite3 *db, int from_loc_id, int to_loc_id,
                                int **out_ids, int *out_count, int *out_reversed) {
    sqlite3_stmt *stmt = NULL;
    const unsigned char *txt = NULL;
    int rc;

    if (out_ids) *out_ids = NULL;
    if (out_count) *out_count = 0;
    if (out_reversed) *out_reversed = 0;
    if (!db || !out_ids || !out_count) return 0;

    rc = sqlite3_prepare_v2(db,
        "SELECT waypoint_path FROM distances WHERE from_location_id = ? AND to_location_id = ?;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return 0;

    sqlite3_bind_int(stmt, 1, from_loc_id);
    sqlite3_bind_int(stmt, 2, to_loc_id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        txt = sqlite3_column_text(stmt, 0);
        *out_count = txt ? parse_waypoint_path_json_local((const char*)txt, out_ids) : 0;
        sqlite3_finalize(stmt);
        return 1;
    }
    sqlite3_finalize(stmt);

    rc = sqlite3_prepare_v2(db,
        "SELECT waypoint_path FROM distances WHERE from_location_id = ? AND to_location_id = ?;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, to_loc_id);
    sqlite3_bind_int(stmt, 2, from_loc_id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        txt = sqlite3_column_text(stmt, 0);
        *out_count = txt ? parse_waypoint_path_json_local((const char*)txt, out_ids) : 0;
        if (out_reversed) *out_reversed = 1;
        sqlite3_finalize(stmt);
        return 1;
    }
    sqlite3_finalize(stmt);
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

static int parse_named_double_value(const char *text, const char *key, double *out_value) {
    const char *p = find_json_key(text, key);
    if (!p || !out_value) return 0;
    p = strchr(p, ':');
    if (!p) return 0;
    p = skip_ws(p + 1);
    return parse_json_double(&p, out_value);
}

static int parse_named_int_array_value(const char *text, const char *key, int **out_arr, int *out_count) {
    const char *p = find_json_key(text, key);
    if (!p || !out_arr || !out_count) return 0;
    p = strchr(p, ':');
    if (!p) return 0;
    p = skip_ws(p + 1);
    return parse_int_array(&p, out_arr, out_count);
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

static int load_boat(sqlite3 *db, int boat_id, gsp_boat_t *boat) {
    sqlite3_stmt *stmt = NULL;
    const char *sql =
        "SELECT b.name, b.capacity, b.location_id, l.lat, l.lon "
        "FROM boats b JOIN locations l ON l.id = b.location_id WHERE b.id = ?";

    memset(boat, 0, sizeof(*boat));
    boat->boat_id = boat_id;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, boat_id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *name = sqlite3_column_text(stmt, 0);
        if (name) snprintf(boat->boat_name, sizeof(boat->boat_name), "%s", (const char*)name);
        boat->boat_capacity = sqlite3_column_double(stmt, 1);
        boat->boat_loc_id = sqlite3_column_int(stmt, 2);
        boat->boat_lat = sqlite3_column_double(stmt, 3);
        boat->boat_lon = sqlite3_column_double(stmt, 4);
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
                                             const char *key,
                                             int ***segment_arrays,
                                             int **segment_sizes,
                                             int *segment_count) {
    const char *summary_pos = find_json_key(json_text, "summary");
    if (summary_pos) {
        char variant_name[64];
        if (extract_summary_final_variant_name(summary_pos, variant_name, sizeof(variant_name))) {
            const char *solution_pos = find_json_key(json_text, "solution");
            if (solution_pos) {
                const char *variant_pos = find_json_key(solution_pos, variant_name);
                if (variant_pos) {
                    const char *candidate = find_json_key(variant_pos, key);
                    if (candidate) {
                        return parse_nested_int_arrays(candidate, key,
                                                       segment_arrays, segment_sizes, segment_count);
                    }
                }
            }
        }
    }
    return parse_nested_int_arrays(json_text, key,
                                   segment_arrays, segment_sizes, segment_count);
}

static int extract_int_array_from_input(const char *json_text,
                                        const char *key,
                                        int **out_arr,
    int *out_count) {
    const char *summary_pos = find_json_key(json_text, "summary");
    if (summary_pos) {
        char variant_name[64];
        if (extract_summary_final_variant_name(summary_pos, variant_name, sizeof(variant_name))) {
            const char *solution_pos = find_json_key(json_text, "solution");
            if (solution_pos) {
                const char *variant_pos = find_json_key(solution_pos, variant_name);
                if (variant_pos) {
                    const char *candidate = find_json_key(variant_pos, key);
                    if (candidate && parse_named_int_array_value(candidate, key, out_arr, out_count)) {
                        return 1;
                    }
                }
            }
        }
    }
    return parse_named_int_array_value(json_text, key, out_arr, out_count);
}

static int load_segments_from_json(const char *input_path, const nn_instance_t *inst,
                                   const gsp_boat_t *boat,
                                   gsp_route_segment_t **out_segments, int *out_count,
                                   nn_solution_t *initial_solution,
                                   double *input_total_distance_nm) {
    char *json_text = read_text_file(input_path);
    int **segment_arrays = NULL;
    int *segment_sizes = NULL;
    int **segment_location_arrays = NULL;
    int *segment_location_sizes = NULL;
    int segment_count = 0;
    int location_segment_count = 0;
    int *dock_location_ids = NULL;
    int dock_count = 0;
    gsp_route_segment_t *segments = NULL;
    int rc = 0;

    if (input_total_distance_nm) *input_total_distance_nm = 0.0;
    if (!json_text) return 0;
    if (!extract_segment_arrays_from_input(json_text, "tour_segments_station_ids",
                                           &segment_arrays, &segment_sizes, &segment_count)) goto cleanup;
    if (!extract_segment_arrays_from_input(json_text, "tour_segments_location_ids",
                                           &segment_location_arrays, &segment_location_sizes, &location_segment_count)) goto cleanup;
    if (segment_count <= 0) goto cleanup;
    if (location_segment_count != segment_count) goto cleanup;
    if (!extract_int_array_from_input(json_text, "dock_location_ids", &dock_location_ids, &dock_count)) goto cleanup;
    if (dock_count != segment_count + 1) goto cleanup;
    if (dock_location_ids[0] != boat->boat_loc_id ||
        dock_location_ids[dock_count - 1] != boat->boat_loc_id) goto cleanup;

    segments = (gsp_route_segment_t*)calloc((size_t)segment_count, sizeof(gsp_route_segment_t));
    if (!segments) goto cleanup;

    for (int i = 0; i < segment_count; i++) {
        gsp_route_segment_t *seg = &segments[i];
        seg->count = segment_sizes[i];
        seg->capacity = segment_sizes[i];
        seg->signed_station_ids = segment_arrays[i];
        segment_arrays[i] = NULL;
        seg->start_loc_id = dock_location_ids[i];
        seg->end_loc_id = dock_location_ids[i + 1];
        seg->catch_amount = compute_segment_catch(inst, seg->signed_station_ids, seg->count);
        seg->distance_nm = 0.0;
        if (!segment_location_arrays || !segment_location_sizes || segment_location_sizes[i] <= 1) goto cleanup;
        for (int j = 0; j < segment_location_sizes[i] - 1; j++) {
            int from_loc = segment_location_arrays[i][j];
            int to_loc = segment_location_arrays[i][j + 1];
            if (from_loc < 0 || from_loc >= inst->max_loc_id ||
                to_loc < 0 || to_loc >= inst->max_loc_id ||
                !inst->distances || !inst->distances[from_loc] ||
                inst->distances[from_loc][to_loc] < 0.0) {
                goto cleanup;
            }
            seg->distance_nm += inst->distances[from_loc][to_loc];
        }
    }

    memset(initial_solution, 0, sizeof(*initial_solution));
    if (input_total_distance_nm) {
        if (!parse_named_double_value(json_text, "final_total_distance_nm", input_total_distance_nm)) {
            const char *summary_pos = find_json_key(json_text, "summary");
            if (summary_pos) {
                if (!extract_summary_final_total_distance(summary_pos, input_total_distance_nm)) {
                    char variant_name[64];
                    if (extract_summary_final_variant_name(summary_pos, variant_name, sizeof(variant_name))) {
                        const char *solution_pos = find_json_key(json_text, "solution");
                        if (solution_pos) {
                            const char *variant_pos = find_json_key(solution_pos, variant_name);
                            if (variant_pos) {
                                (void)parse_named_double_value(variant_pos, "total_distance_nm",
                                                               input_total_distance_nm);
                            }
                        }
                    }
                }
            }
        }
    }
    rc = 1;

cleanup:
    if (!rc) {
        free_segments(segments, segment_count);
    }
    if (segment_arrays) {
        for (int i = 0; i < segment_count; i++) free(segment_arrays[i]);
    }
    if (segment_location_arrays) {
        for (int i = 0; i < segment_count; i++) free(segment_location_arrays[i]);
    }
    free(segment_arrays);
    free(segment_sizes);
    free(segment_location_arrays);
    free(segment_location_sizes);
    free(dock_location_ids);
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
                             int **out_signed_station_ids,
                             double haul_distance_scale,
                             int *solve_count,
                             double *out_gap_percent,
                             double *out_runtime_seconds,
                             int *out_model_num_vars,
                             int *out_model_num_constrs,
                             double time_limit_seconds) {
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
    mip_params.time_limit_seconds = time_limit_seconds;
    mip_params.exclude_haul_distance = !(haul_distance_scale > 0.0);
    mip_params.use_scaled_haul_distance = (haul_distance_scale > 0.0);
    mip_params.haul_distance_scale = (haul_distance_scale > 0.0) ? haul_distance_scale : 0.0;

    if (solve_mip_endpaired_tsp(&mip_instance, &mip_params,
                                start_loc_id, end_loc_id, &mip_solution) != 0) {
        goto fail;
    }

    if (solve_count) (*solve_count)++;
    if (out_gap_percent) *out_gap_percent = mip_solution.gap * 100.0;
    if (out_runtime_seconds) *out_runtime_seconds = mip_solution.runtime_seconds;
    if (out_model_num_vars) *out_model_num_vars = mip_solution.model_num_vars;
    if (out_model_num_constrs) *out_model_num_constrs = mip_solution.model_num_constrs;

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

static int segment_to_solution(const nn_instance_t *inst, const gsp_boat_t *boat,
                               const gsp_route_segment_t *segments, int n_segments,
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

static int solution_is_feasible(const nn_instance_t *inst, const gsp_boat_t *boat,
                                const nn_solution_t *sol) {
    return segments_within_capacity(sol->segment_catches, sol->segment_count, boat->boat_capacity) &&
           stations_are_unique_and_complete(sol->visit_station_ids, sol->visit_station_count, inst->num_stations);
}

static int station_in_segment(const nn_solution_t *sol, int segment_index, int station_id) {
    for (int i = 0; i < sol->visit_station_count; i++) {
        if (sol->visit_station_segment[i] == segment_index &&
            sol->visit_station_ids[i] == station_id) return 1;
    }
    return 0;
}

static int count_station_moves_between_solutions(const nn_solution_t *prev_sol,
                                                 const nn_solution_t *curr_sol) {
    int moved_count = 0;
    int max_candidates = 0;
    int *moved_ids = NULL;

    if (!prev_sol || !curr_sol) return 0;
    max_candidates = prev_sol->visit_station_count + curr_sol->visit_station_count;
    moved_ids = (int*)malloc((size_t)((max_candidates > 0) ? max_candidates : 1) * sizeof(int));
    if (!moved_ids) return 0;

    for (int s = 0; s < curr_sol->segment_count; s++) {
        for (int i = 0; i < prev_sol->visit_station_count; i++) {
            int station_id;
            if (prev_sol->visit_station_segment[i] != s) continue;
            station_id = prev_sol->visit_station_ids[i];
            if (!station_in_segment(curr_sol, s, station_id) &&
                !contains_int(moved_ids, moved_count, station_id)) {
                moved_ids[moved_count++] = station_id;
            }
        }
        for (int i = 0; i < curr_sol->visit_station_count; i++) {
            int station_id;
            if (curr_sol->visit_station_segment[i] != s) continue;
            station_id = curr_sol->visit_station_ids[i];
            if (!station_in_segment(prev_sol, s, station_id) &&
                !contains_int(moved_ids, moved_count, station_id)) {
                moved_ids[moved_count++] = station_id;
            }
        }
    }

    free(moved_ids);
    return moved_count;
}

static void compute_station_move_stats(const sweep_snapshot_t *snapshots,
                                       int snapshot_count,
                                       int *out_count,
                                       double *out_mean,
                                       double *out_median,
                                       int *out_max) {
    int pass_count = 0;
    int *moves = NULL;
    double sum = 0.0;

    if (out_count) *out_count = 0;
    if (out_mean) *out_mean = -1.0;
    if (out_median) *out_median = -1.0;
    if (out_max) *out_max = 0;
    if (!snapshots || snapshot_count <= 1) return;

    pass_count = snapshot_count - 1;
    moves = (int*)malloc((size_t)pass_count * sizeof(int));
    if (!moves) return;

    for (int i = 1; i < snapshot_count; i++) {
        moves[i - 1] = count_station_moves_between_solutions(&snapshots[i - 1].solution,
                                                             &snapshots[i].solution);
        sum += (double)moves[i - 1];
        if (out_max && moves[i - 1] > *out_max) *out_max = moves[i - 1];
    }

    qsort(moves, (size_t)pass_count, sizeof(int), compare_int_asc);

    if (out_count) *out_count = pass_count;
    if (out_mean) *out_mean = sum / (double)pass_count;
    if (out_median) {
        if (pass_count % 2 == 1) {
            *out_median = (double)moves[pass_count / 2];
        } else {
            *out_median = ((double)moves[(pass_count / 2) - 1] +
                           (double)moves[pass_count / 2]) / 2.0;
        }
    }

    free(moves);
}

static void write_station_mutation_ids(FILE *fp,
                                       const nn_solution_t *prev_sol,
                                       const nn_solution_t *curr_sol) {
    fprintf(fp, "      \"tour_segments_station_mutation_ids\": [\n");
    for (int s = 0; s < curr_sol->segment_count; s++) {
        int first = 1;
        fprintf(fp, "        [");
        if (prev_sol) {
            for (int i = 0; i < prev_sol->visit_station_count; i++) {
                int station_id;
                if (prev_sol->visit_station_segment[i] != s) continue;
                station_id = prev_sol->visit_station_ids[i];
                if (!station_in_segment(curr_sol, s, station_id)) {
                    if (!first) fprintf(fp, ", ");
                    fprintf(fp, "%d", -station_id);
                    first = 0;
                }
            }
            for (int i = 0; i < curr_sol->visit_station_count; i++) {
                int station_id;
                if (curr_sol->visit_station_segment[i] != s) continue;
                station_id = curr_sol->visit_station_ids[i];
                if (!station_in_segment(prev_sol, s, station_id)) {
                    if (!first) fprintf(fp, ", ");
                    fprintf(fp, "%d", station_id);
                    first = 0;
                }
            }
        }
        fprintf(fp, "]%s\n", (s + 1 < curr_sol->segment_count) ? "," : "");
    }
    fprintf(fp, "      ],\n");
}

#ifdef HAVE_GUROBI
static int reoptimize_segment(GRBenv *env, const nn_instance_t *inst, gsp_route_segment_t *segment,
                              double time_limit_seconds,
                              double haul_distance_scale) {
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
                           &catch_amount, &distance_nm, &signed_ids,
                           haul_distance_scale, NULL, NULL, NULL, NULL, NULL,
                           time_limit_seconds)) {
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
                                   gsp_route_segment_t *segments, int n_segments,
                                   double time_limit_seconds,
                                   double haul_distance_scale) {
    for (int i = 0; i < n_segments; i++) {
        if (!reoptimize_segment(env, inst, &segments[i], time_limit_seconds,
                                haul_distance_scale)) return 0;
    }
    return 1;
}

static int optimize_boundary(GRBenv *env, const nn_instance_t *inst, const gsp_boat_t *boat,
                             gsp_route_segment_t *left, gsp_route_segment_t *right,
                             double haul_distance_scale,
                             int *solve_count,
                             gsp_mip_solve_detail_t **solve_details,
                             int *solve_detail_count,
                             int *solve_detail_capacity,
                             int boundary_index,
                             int left_segment_index,
                             int right_segment_index,
                             double time_limit_seconds) {
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
        double left_gap_percent = -1.0;
        double right_gap_percent = -1.0;
        double left_runtime_seconds = -1.0;
        double right_runtime_seconds = -1.0;
        int left_num_vars = 0;
        int right_num_vars = 0;
        int left_num_constrs = 0;
        int right_num_constrs = 0;
        int *left_ids = NULL;
        int *right_ids = NULL;

        for (int i = 0; i < k; i++) left_catch += station_amount(inst, merged[i]);
        for (int i = k; i < total; i++) right_catch += station_amount(inst, merged[i]);
        if (left_catch > (int)boat->boat_capacity || right_catch > (int)boat->boat_capacity) continue;

        if (!solve_segment_tsp(env, inst, left->start_loc_id, left->end_loc_id,
                               merged, k, &left_catch, &left_dist, &left_ids,
                               haul_distance_scale, solve_count,
                               &left_gap_percent, &left_runtime_seconds,
                               &left_num_vars, &left_num_constrs,
                               time_limit_seconds)) {
            continue;
        }
        if (solve_details && solve_detail_count && solve_detail_capacity) {
            gsp_mip_solve_detail_t detail;
            gsp_mip_solve_detail_init(&detail);
            detail.boundary_index = boundary_index;
            detail.candidate_split_index = k;
            detail.segment_index = left_segment_index;
            detail.segment_role = 0;
            detail.station_count = k;
            detail.node_count = k + 2;
            detail.moved_stations = count_segment_station_changes(left, &(gsp_route_segment_t){
                .signed_station_ids = left_ids, .count = k
            });
            detail.model_num_vars = left_num_vars;
            detail.model_num_constrs = left_num_constrs;
            detail.runtime_seconds = left_runtime_seconds;
            detail.gap_percent = left_gap_percent;
            if (!gsp_append_mip_solve_detail(solve_details, solve_detail_count, solve_detail_capacity, &detail)) {
                free(left_ids);
                return 0;
            }
        }
        if (!solve_segment_tsp(env, inst, right->start_loc_id, right->end_loc_id,
                               merged + k, total - k, &right_catch, &right_dist, &right_ids,
                               haul_distance_scale, solve_count,
                               &right_gap_percent, &right_runtime_seconds,
                               &right_num_vars, &right_num_constrs,
                               time_limit_seconds)) {
            free(left_ids);
            continue;
        }
        if (solve_details && solve_detail_count && solve_detail_capacity) {
            gsp_mip_solve_detail_t detail;
            gsp_mip_solve_detail_init(&detail);
            detail.boundary_index = boundary_index;
            detail.candidate_split_index = k;
            detail.segment_index = right_segment_index;
            detail.segment_role = 1;
            detail.station_count = total - k;
            detail.node_count = (total - k) + 2;
            detail.moved_stations = count_segment_station_changes(right, &(gsp_route_segment_t){
                .signed_station_ids = right_ids, .count = total - k
            });
            detail.model_num_vars = right_num_vars;
            detail.model_num_constrs = right_num_constrs;
            detail.runtime_seconds = right_runtime_seconds;
            detail.gap_percent = right_gap_percent;
            if (!gsp_append_mip_solve_detail(solve_details, solve_detail_count, solve_detail_capacity, &detail)) {
                free(left_ids);
                free(right_ids);
                return 0;
            }
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

static int count_segment_station_changes(const gsp_route_segment_t *before,
                                         const gsp_route_segment_t *after) {
    int changes = 0;
    for (int i = 0; i < before->count; i++) {
        int found = 0;
        int station_id = abs(before->signed_station_ids[i]);
        for (int j = 0; j < after->count; j++) {
            if (abs(after->signed_station_ids[j]) == station_id) {
                found = 1;
                break;
            }
        }
        if (!found) changes++;
    }
    for (int i = 0; i < after->count; i++) {
        int found = 0;
        int station_id = abs(after->signed_station_ids[i]);
        for (int j = 0; j < before->count; j++) {
            if (abs(before->signed_station_ids[j]) == station_id) {
                found = 1;
                break;
            }
        }
        if (!found) changes++;
    }
    return changes;
}

static int sweep_leg_is_station_haul(const nn_instance_t *inst, int from_loc_id, int to_loc_id) {
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

static double sweep_distance_or_zero(const nn_instance_t *inst, int from_loc_id, int to_loc_id) {
    if (!inst || !inst->distances) return 0.0;
    if (from_loc_id == to_loc_id) return 0.0;
    if (from_loc_id < 0 || from_loc_id >= inst->max_loc_id) return 0.0;
    if (to_loc_id < 0 || to_loc_id >= inst->max_loc_id) return 0.0;
    return (inst->distances[from_loc_id][to_loc_id] > 0.0) ?
        inst->distances[from_loc_id][to_loc_id] : 0.0;
}

static void sweep_accumulate_leg_distance(const nn_instance_t *inst,
                                          int from_loc_id,
                                          int to_loc_id,
                                          gsp_distance_breakdown_t *breakdown) {
    double d;
    if (!breakdown) return;
    d = sweep_distance_or_zero(inst, from_loc_id, to_loc_id);
    if (sweep_leg_is_station_haul(inst, from_loc_id, to_loc_id)) {
        breakdown->haul_distance_nm += d;
    } else {
        breakdown->transit_distance_nm += d;
    }
    breakdown->total_distance_nm += d;
}

static void sweep_compute_segment_breakdowns(const nn_instance_t *inst,
                                             const nn_solution_t *sol,
                                             int boat_loc_id,
                                             gsp_distance_breakdown_t *segment_breakdowns,
                                             gsp_distance_breakdown_t *total_breakdown) {
    if (!inst || !sol || !segment_breakdowns || !total_breakdown) return;
    memset(total_breakdown, 0, sizeof(*total_breakdown));
    for (int s = 0; s < sol->segment_count; s++) {
        int prev_loc = (s == 0) ? boat_loc_id : sol->tour[sol->segment_ends[s - 1]];
        int end_loc = (s == sol->segment_count - 1) ? boat_loc_id : sol->tour[sol->segment_ends[s]];
        memset(&segment_breakdowns[s], 0, sizeof(segment_breakdowns[s]));
        for (int i = 0; i < sol->visit_station_count; i++) {
            int station_idx;
            int direction;
            int entry_loc;
            int exit_loc;

            if (sol->visit_station_segment[i] != s) continue;
            station_idx = find_station_index(inst, sol->visit_station_ids[i]);
            if (station_idx < 0) continue;

            direction = (sol->visit_station_direction && sol->visit_station_direction[i] < 0) ? -1 : 1;
            entry_loc = (direction > 0) ? inst->nodes[station_idx].start_loc_id
                                        : inst->nodes[station_idx].end_loc_id;
            exit_loc = (direction > 0) ? inst->nodes[station_idx].end_loc_id
                                       : inst->nodes[station_idx].start_loc_id;

            sweep_accumulate_leg_distance(inst, prev_loc, entry_loc, &segment_breakdowns[s]);
            sweep_accumulate_leg_distance(inst, entry_loc, exit_loc, &segment_breakdowns[s]);
            prev_loc = exit_loc;
        }
        if (prev_loc != end_loc) {
            sweep_accumulate_leg_distance(inst, prev_loc, end_loc, &segment_breakdowns[s]);
        }
        total_breakdown->transit_distance_nm += segment_breakdowns[s].transit_distance_nm;
        total_breakdown->haul_distance_nm += segment_breakdowns[s].haul_distance_nm;
        total_breakdown->total_distance_nm += segment_breakdowns[s].total_distance_nm;
    }
}

static void write_solution_json(FILE *fp, sqlite3 *db, const nn_instance_t *inst,
                                const nn_solution_t *sol,
                                const gsp_boat_t *boat, int feasible) {
    int *unique_waypoint_location_ids = NULL;
    int unique_waypoint_count = 0;
    int unique_waypoint_capacity = 0;
    unsigned char *seen_waypoint_location_ids = NULL;
    gsp_distance_breakdown_t *segment_breakdowns = NULL;
    gsp_distance_breakdown_t total_breakdown;

    if (!fp || !db || !inst || !sol || !boat) return;
    memset(&total_breakdown, 0, sizeof(total_breakdown));
    segment_breakdowns = (gsp_distance_breakdown_t*)calloc((size_t)sol->segment_count, sizeof(gsp_distance_breakdown_t));
    if (!segment_breakdowns) return;
    sweep_compute_segment_breakdowns(inst, sol, boat->boat_loc_id, segment_breakdowns, &total_breakdown);

    seen_waypoint_location_ids = (unsigned char*)calloc((size_t)inst->max_loc_id, sizeof(unsigned char));
    if (!seen_waypoint_location_ids) {
        free(segment_breakdowns);
        return;
    }

    fprintf(fp, "      \"tour_segments_location_ids\": [\n");
    for (int s = 0; s < sol->segment_count; s++) {
        int start = sol->segment_starts[s];
        int end = sol->segment_ends[s];
        int base_cap = (end - start + 1) + 2;
        int base_n = 0;
        int *base = (int*)malloc((size_t)base_cap * sizeof(int));
        fprintf(fp, "        [");
        if (!base) {
            fprintf(fp, "]%s\n", (s + 1 < sol->segment_count) ? "," : "");
            continue;
        }
        base[base_n++] = (s == 0) ? boat->boat_loc_id : sol->tour[sol->segment_ends[s - 1]];
        for (int i = start; i <= end; i++) base[base_n++] = sol->tour[i];
        if (s == sol->segment_count - 1 && (base_n == 0 || base[base_n - 1] != boat->boat_loc_id)) {
            base[base_n++] = boat->boat_loc_id;
        }
        if (base_n > 0) {
            fprintf(fp, "%d", base[0]);
            for (int i = 0; i < base_n - 1; i++) {
                int *waypoint_ids = NULL;
                int waypoint_count = 0;
                int reversed = 0;
                (void)lookup_waypoint_path(db, base[i], base[i + 1], &waypoint_ids, &waypoint_count, &reversed);
                if (waypoint_ids && waypoint_count > 0) {
                    if (reversed) {
                        for (int k = waypoint_count - 1; k >= 0; k--) {
                            int waypoint_id = waypoint_ids[k];
                            fprintf(fp, ", %d", waypoint_id);
                            if (waypoint_id >= 0 && waypoint_id < inst->max_loc_id &&
                                !seen_waypoint_location_ids[waypoint_id]) {
                                seen_waypoint_location_ids[waypoint_id] = 1;
                                (void)append_int(&unique_waypoint_location_ids, &unique_waypoint_count,
                                                 &unique_waypoint_capacity, waypoint_id);
                            }
                        }
                    } else {
                        for (int k = 0; k < waypoint_count; k++) {
                            int waypoint_id = waypoint_ids[k];
                            fprintf(fp, ", %d", waypoint_id);
                            if (waypoint_id >= 0 && waypoint_id < inst->max_loc_id &&
                                !seen_waypoint_location_ids[waypoint_id]) {
                                seen_waypoint_location_ids[waypoint_id] = 1;
                                (void)append_int(&unique_waypoint_location_ids, &unique_waypoint_count,
                                                 &unique_waypoint_capacity, waypoint_id);
                            }
                        }
                    }
                }
                free(waypoint_ids);
                fprintf(fp, ", %d", base[i + 1]);
            }
        }
        free(base);
        fprintf(fp, "]%s\n", (s + 1 < sol->segment_count) ? "," : "");
    }
    fprintf(fp, "      ],\n");

    {
        int *segment_end_location_ids = NULL;
        int *dock_location_ids = NULL;
        int dock_count = 0;
        segment_end_location_ids = (int*)malloc((size_t)sol->segment_count * sizeof(int));
        if (!segment_end_location_ids) return;
        for (int s = 0; s < sol->segment_count; s++) {
            segment_end_location_ids[s] =
                (s == sol->segment_count - 1) ? boat->boat_loc_id : sol->tour[sol->segment_ends[s]];
        }
        if (!gsp_build_dock_location_ids_from_segment_ends(boat->boat_loc_id,
                                                           segment_end_location_ids,
                                                           sol->segment_count,
                                                           &dock_location_ids,
                                                           &dock_count)) {
            free(segment_end_location_ids);
            return;
        }
        free(segment_end_location_ids);
        fprintf(fp, "      \"dock_location_ids\": [");
        for (int i = 0; i < dock_count; i++) {
            if (i) fprintf(fp, ", ");
            fprintf(fp, "%d", dock_location_ids[i]);
        }
        fprintf(fp, "],\n");
        free(dock_location_ids);
    }

    fprintf(fp, "      \"unique_waypoint_location_ids\": [");
    for (int i = 0; i < unique_waypoint_count; i++) {
        fprintf(fp, "%d", unique_waypoint_location_ids[i]);
        if (i + 1 < unique_waypoint_count) fprintf(fp, ", ");
    }
    fprintf(fp, "],\n");

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

    gsp_write_distance_nm_json(fp, "      ", segment_breakdowns, sol->segment_count, &total_breakdown, 1);
    fprintf(fp, "      \"feasible\": %s\n", feasible ? "true" : "false");
    free(seen_waypoint_location_ids);
    free(unique_waypoint_location_ids);
    free(segment_breakdowns);
}

static void write_pass_entry(FILE *fp,
                             sqlite3 *db,
                             const nn_instance_t *inst,
                             const char *pass_name,
                             const nn_solution_t *prev_solution,
                             const sweep_snapshot_t *snapshot,
                             const gsp_boat_t *boat) {
    int moved_stations = count_station_moves_between_solutions(prev_solution, &snapshot->solution);
    fprintf(fp, "    \"%s\": {\n", pass_name);
    fprintf(fp, "      \"pass\": %d,\n", snapshot->pass_index);
    fprintf(fp, "      \"changed\": %s,\n", snapshot->changed ? "true" : "false");
    fprintf(fp, "      \"stations_moved\": %d,\n", moved_stations);
    fprintf(fp, "      \"boundary_port_ids\": [");
    for (int i = 0; i < snapshot->boundary_count; i++) {
        fprintf(fp, "%d", snapshot->boundary_port_ids ? snapshot->boundary_port_ids[i] : 0);
        if (i + 1 < snapshot->boundary_count) fprintf(fp, ", ");
    }
    fprintf(fp, "],\n");
    fprintf(fp, "      \"boundary_improvement_gain_nm\": [");
    for (int i = 0; i < snapshot->boundary_count; i++) {
        fprintf(fp, "%.6f", snapshot->boundary_improvement_gain_nm ? snapshot->boundary_improvement_gain_nm[i] : 0.0);
        if (i + 1 < snapshot->boundary_count) fprintf(fp, ", ");
    }
    fprintf(fp, "],\n");
    fprintf(fp, "      \"boundary_attempts\": %d,\n", snapshot->boundary_attempts);
    fprintf(fp, "      \"boundary_changes\": %d,\n", snapshot->boundary_changes);
    fprintf(fp, "      \"accepted_capacity_solves\": %d,\n", snapshot->boundary_changes);
    fprintf(fp, "      \"total_capacity_solves\": %d,\n", snapshot->boundary_attempts);
    fprintf(fp, "      \"mip_solves\": %d,\n", snapshot->mip_solve_count);
    fprintf(fp, "      \"pass_runtime_seconds\": %.6f,\n", snapshot->pass_runtime_seconds);
    write_station_mutation_ids(fp, prev_solution, &snapshot->solution);
    write_solution_json(fp, db, inst, &snapshot->solution, boat, snapshot->feasible);
    fprintf(fp, "\n    }");
}

static gsp_mip_solve_detail_t *flatten_sweep_mip_details(const sweep_snapshot_t *snapshots,
                                                         int snapshot_count,
                                                         int *out_count) {
    gsp_mip_solve_detail_t *details = NULL;
    int count = 0;
    int capacity = 0;

    if (out_count) *out_count = 0;
    if (!snapshots || snapshot_count <= 0) return NULL;

    for (int s = 0; s < snapshot_count; s++) {
        for (int i = 0; i < snapshots[s].mip_detail_count; i++) {
            if (!gsp_append_mip_solve_detail(&details, &count, &capacity,
                                             &snapshots[s].mip_solve_details[i])) {
                free(details);
                return NULL;
            }
        }
    }

    if (out_count) *out_count = count;
    return details;
}

static int write_sweep_json(const char *output_path,
                            sqlite3 *db,
                            const gsp_boat_t *boat,
                            const nn_instance_t *inst,
                            const sweep_config_t *cfg,
                            const sweep_snapshot_t *snapshots,
                            int snapshot_count,
                            int total_boundary_attempts,
                            int total_boundary_changes,
                            int total_mip_solves,
                            double preprocessing_seconds,
                            double total_runtime_seconds,
                            const char *strategy_name,
                            int is_final_write) {
    FILE *fp = fopen(output_path, "w");
    char final_pass_name[32];
    int station_move_pass_count = 0;
    int station_move_max = 0;
    double station_move_mean = -1.0;
    double station_move_median = -1.0;
    double pass_runtime_total_seconds = 0.0;
    double postprocessing_seconds = 0.0;
    double mip_runtime_mean = -1.0;
    double mip_runtime_max = -1.0;
    double mip_gap_mean = -1.0;
    double mip_gap_max = -1.0;
    gsp_mip_solve_detail_t *mip_details = NULL;
    int mip_detail_count = 0;
    if (!fp) return 0;

    compute_station_move_stats(snapshots, snapshot_count,
                               &station_move_pass_count,
                               &station_move_mean,
                               &station_move_median,
                               &station_move_max);
    for (int i = 0; i < snapshot_count; i++) {
        pass_runtime_total_seconds += snapshots[i].pass_runtime_seconds;
    }
    postprocessing_seconds = total_runtime_seconds - preprocessing_seconds - pass_runtime_total_seconds;
    if (postprocessing_seconds < 0.0) postprocessing_seconds = 0.0;
    mip_details = flatten_sweep_mip_details(snapshots, snapshot_count, &mip_detail_count);
    gsp_compute_mip_summary(mip_details, mip_detail_count,
                            &mip_runtime_mean, &mip_runtime_max,
                            &mip_gap_mean, &mip_gap_max);

    fprintf(fp, "{\n");
    fprintf(fp, "  \"metadata\": {\n");
    fprintf(fp, "    \"solver_version\": \"refinement_1.0\",\n");
    fprintf(fp, "    \"timestamp\": \"%ld\",\n", (long)time(NULL));
    fprintf(fp, "    \"mode\": \"refinement\",\n");
    fprintf(fp, "    \"strategy\": \"%s\",\n", strategy_name ? strategy_name : "refinement");
    fprintf(fp, "    \"boat_id\": %d,\n", boat->boat_id);
    fprintf(fp, "    \"boat_name\": \"%s\",\n", boat->boat_name);
    fprintf(fp, "    \"boat_docked_location\": {\"lat\": %.6f, \"lon\": %.6f},\n",
            boat->boat_lat, boat->boat_lon);
    fprintf(fp, "    \"boat_location_id\": %d,\n", boat->boat_loc_id);
    fprintf(fp, "    \"mip_time_limit_seconds\": %d,\n", cfg ? cfg->mip_time_limit_seconds : 0);
    fprintf(fp, "    \"global_time_limit_seconds\": %d,\n", cfg ? cfg->global_time_limit_seconds : 0);
    fprintf(fp, "    \"max_iterations\": %d\n", cfg ? cfg->max_iterations : 0);
    fprintf(fp, "  },\n");

    fprintf(fp, "  \"problem\": {\n");
    fprintf(fp, "    \"num_stations\": %d,\n", inst->num_stations);
    fprintf(fp, "    \"capacity\": %.0f\n", boat->boat_capacity);
    fprintf(fp, "  },\n");

    fprintf(fp, "  \"solution\": {\n");
    for (int i = 0; i < snapshot_count; i++) {
        char pass_name[32];
        if (snapshots[i].pass_index == 0) snprintf(pass_name, sizeof(pass_name), "init");
        else snprintf(pass_name, sizeof(pass_name), "pass%d", snapshots[i].pass_index);
        write_pass_entry(fp, db, inst, pass_name,
                         (i > 0) ? &snapshots[i - 1].solution : NULL, &snapshots[i], boat);
        fprintf(fp, "%s\n", (i + 1 < snapshot_count) ? "," : "");
    }
    fprintf(fp, "  },\n");
    gsp_write_boundary_mip_section(fp, "l2seg", "endpaired_tsp",
                                   cfg ? (double)cfg->mip_time_limit_seconds : 0.0,
                                   mip_details, mip_detail_count);

    if (snapshots[snapshot_count - 1].pass_index == 0) snprintf(final_pass_name, sizeof(final_pass_name), "init");
    else snprintf(final_pass_name, sizeof(final_pass_name), "pass%d", snapshots[snapshot_count - 1].pass_index);
    fprintf(fp, "  \"summary\": {\n");
    {
        double *distance_trajectory = NULL;
        double *runtime_trajectory = NULL;
        distance_trajectory = (double*)calloc((size_t)snapshot_count, sizeof(double));
        runtime_trajectory = (double*)calloc((size_t)snapshot_count, sizeof(double));
        if (distance_trajectory && runtime_trajectory) {
            for (int i = 0; i < snapshot_count; i++) {
                distance_trajectory[i] = snapshots[i].solution.total_distance;
                runtime_trajectory[i] = snapshots[i].pass_runtime_seconds;
            }
        }

        gsp_write_summary_status_json(fp, "    ", final_pass_name,
                                      is_final_write ? "refinement_complete" : "refinement_running",
                                      snapshots[snapshot_count - 1].feasible,
                                      strategy_name ? strategy_name : "refinement", 1);
        gsp_write_summary_distance_json(fp, "    ", 0, 0.0,
                                        distance_trajectory, snapshot_count,
                                        snapshots[snapshot_count - 1].solution.total_distance, 1);
        gsp_write_summary_runtime_json(fp, "    ", preprocessing_seconds,
                                       runtime_trajectory, snapshot_count,
                                       postprocessing_seconds, total_runtime_seconds, 1);
        fprintf(fp, "    \"sweep\": {\n");
        fprintf(fp, "      \"pass_count\": %d,\n", snapshot_count);
        fprintf(fp, "      \"sweep_pass_count\": %d,\n", (snapshot_count > 0) ? (snapshot_count - 1) : 0);
        fprintf(fp, "      \"stations_moved_per_pass\": {\"count\": %d, \"mean\": ",
                station_move_pass_count);
        gsp_write_json_double_or_null(fp, station_move_mean);
        fprintf(fp, ", \"median\": ");
        gsp_write_json_double_or_null(fp, station_move_median);
        fprintf(fp, ", \"max\": %d},\n", station_move_max);
        fprintf(fp, "      \"accepted_capacity_solves\": %d,\n", total_boundary_changes);
        fprintf(fp, "      \"total_capacity_solves\": %d,\n", total_boundary_attempts);
        fprintf(fp, "      \"total_mip_solves\": %d,\n", total_mip_solves);
        fprintf(fp, "      \"total_boundary_changes\": %d\n", total_boundary_changes);
        fprintf(fp, "    },\n");
        gsp_write_summary_mip_json(fp, "    ", mip_detail_count,
                                   mip_runtime_mean, mip_runtime_max, mip_gap_mean, mip_gap_max, 0);
        free(distance_trajectory);
        free(runtime_trajectory);
    }
    fprintf(fp, "  }\n");
    fprintf(fp, "}\n");

    fclose(fp);
    free(mip_details);
    return 1;
}

static void parse_sweep_args(int argc, char **argv,
                             const char **strategy, const char **database,
                             const char **config, const char **input,
                             const char **output, int *time_limit, int *debug_mode) {
    *strategy = NULL;
    *database = NULL;
    *config = NULL;
    *input = NULL;
    *output = NULL;
    *time_limit = 0;
    *debug_mode = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0) {
            *debug_mode = 1;
            continue;
        }
        if (i >= argc - 1) break;
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
    int time_limit = 0, debug_mode = 0;
    sqlite3 *db = NULL;
    nn_instance_t inst = {0};
    gsp_boat_t boat;
    sweep_config_t sweep_cfg;
    gsp_route_segment_t *segments = NULL;
    int segment_count = 0;
    double input_total_distance_nm = 0.0;
    nn_solution_t current_solution = {0};
    sweep_snapshot_t *snapshots = NULL;
    int snapshot_count = 0, snapshot_capacity = 0;
    int total_boundary_attempts = 0;
    int total_boundary_changes = 0;
    int total_mip_solves = 0;
    int rc = 1;
    struct timespec t_start, t_pass_start, t_pass_end, t_now, t_preproc_end;
    double preprocessing_seconds = 0.0;

    printf("============================================================\n");
    printf("GSP Solver - Phase 1: Sweep\n");
    printf("============================================================\n\n");
    parse_sweep_args(argc, argv, &strategy, &database, &config, &input, &output, &time_limit, &debug_mode);
    if (!strategy || !database || !config || !input || !output) {
        fprintf(stderr, "ERROR: sweep requires --strategy, --database, --config, --input, and --output\n");
        goto cleanup;
    }
    read_sweep_config_from_yaml(config, &sweep_cfg);
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
    if (!load_segments_from_json(input, &inst, &boat, &segments, &segment_count,
                                 &current_solution, &input_total_distance_nm)) {
        fprintf(stderr, "ERROR: Failed to load segmented solution from %s\n", input);
        goto cleanup;
    }
    printf("Loaded segmented input: %d segments, %.2f nm total\n",
           segment_count, input_total_distance_nm);
    printf("Sweep parameters: mip_time_limit=%d max_iterations=%d global_time_limit=%d time_limit=%d\n",
           sweep_cfg.mip_time_limit_seconds,
           sweep_cfg.max_iterations, sweep_cfg.global_time_limit_seconds, time_limit);
    fflush(stdout);

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

        if (!segment_to_solution(&inst, &boat, segments, segment_count, &current_solution)) {
            GRBfreeenv(env);
            goto cleanup;
        }
        clock_gettime(CLOCK_MONOTONIC, &t_preproc_end);
        preprocessing_seconds = elapsed_seconds(t_start, t_preproc_end);

        {
            sweep_snapshot_t snapshot;
            memset(&snapshot, 0, sizeof(snapshot));
            clock_gettime(CLOCK_MONOTONIC, &t_now);
            snapshot.pass_index = 0;
            snapshot.changed = 0;
            snapshot.boundary_count = current_solution.segment_count;
            snapshot.boundary_port_ids = (int*)calloc((size_t)((snapshot.boundary_count > 0) ? snapshot.boundary_count : 1), sizeof(int));
            snapshot.boundary_improvement_gain_nm = (double*)calloc((size_t)((snapshot.boundary_count > 0) ? snapshot.boundary_count : 1), sizeof(double));
            snapshot.boundary_attempts = 0;
            snapshot.boundary_changes = 0;
            snapshot.mip_solve_count = 0;
            snapshot.mip_detail_count = 0;
            snapshot.mip_detail_capacity = 0;
            snapshot.mip_solve_details = NULL;
            snapshot.pass_runtime_seconds = 0.0;
            snapshot.total_runtime_seconds = elapsed_seconds(t_start, t_now);
            snapshot.feasible = solution_is_feasible(&inst, &boat, &current_solution);
            if (!snapshot.boundary_port_ids || !snapshot.boundary_improvement_gain_nm ||
                !copy_solution(&snapshot.solution, &current_solution)) {
                free(snapshot.boundary_port_ids);
                free(snapshot.boundary_improvement_gain_nm);
                GRBfreeenv(env);
                goto cleanup;
            }
            if (!append_snapshot(&snapshots, &snapshot_count, &snapshot_capacity, &snapshot)) {
                free(snapshot.boundary_port_ids);
                free(snapshot.boundary_improvement_gain_nm);
                free_solution(&snapshot.solution);
                GRBfreeenv(env);
                goto cleanup;
            }
            if (debug_mode) {
                if (!write_sweep_json(output, db, &boat, &inst,
                                      &sweep_cfg,
                                      snapshots, snapshot_count,
                                      total_boundary_attempts,
                                      total_boundary_changes,
                                      total_mip_solves,
                                      preprocessing_seconds,
                                      snapshot.total_runtime_seconds,
                                      strategy, 0)) {
                    fprintf(stderr, "ERROR: Failed to persist initial sweep snapshot JSON\n");
                    GRBfreeenv(env);
                    goto cleanup;
                }
            }
            printf("Stored initial sweep snapshot: init %.2f nm feasible=%s\n",
                   snapshot.solution.total_distance, snapshot.feasible ? "true" : "false");
            fflush(stdout);
        }

        active = (int*)malloc((size_t)((segment_count > 1) ? segment_count : 1) * sizeof(int));
        if (!active) {
            GRBfreeenv(env);
            goto cleanup;
        }
        for (int i = 0; i < segment_count; i++) active[i] = 1;

        while (keep_running) {
            int boundary_attempts = 0;
            int boundary_changes = 0;
            int changed = 0;
            int pass_mip_solve_count = 0;
            sweep_snapshot_t snapshot;
            int *boundary_port_ids = NULL;
            double *boundary_gain_nm = NULL;
            gsp_mip_solve_detail_t *pass_mip_solve_details = NULL;
            int pass_mip_detail_count = 0;
            int pass_mip_detail_capacity = 0;

            clock_gettime(CLOCK_MONOTONIC, &t_now);
            if (sweep_cfg.global_time_limit_seconds > 0 &&
                elapsed_seconds(t_start, t_now) >= (double)sweep_cfg.global_time_limit_seconds) {
                printf("Stopping sweep: global wall-clock limit reached (%.2fs >= %ds)\n",
                       elapsed_seconds(t_start, t_now),
                       sweep_cfg.global_time_limit_seconds);
                fflush(stdout);
                keep_running = 0;
                break;
            }
            if (sweep_cfg.max_iterations > 0 && pass_index > sweep_cfg.max_iterations) {
                keep_running = 0;
                break;
            }

            clock_gettime(CLOCK_MONOTONIC, &t_pass_start);
            memset(&snapshot, 0, sizeof(snapshot));
            boundary_port_ids = (int*)calloc((size_t)((segment_count > 0) ? segment_count : 1), sizeof(int));
            boundary_gain_nm = (double*)calloc((size_t)((segment_count > 0) ? segment_count : 1), sizeof(double));
            if (!boundary_port_ids || !boundary_gain_nm) {
                free(boundary_port_ids);
                free(boundary_gain_nm);
                free(pass_mip_solve_details);
                free(active);
                GRBfreeenv(env);
                goto cleanup;
            }
            printf("Starting sweep pass%d across %d boundaries\n",
                   pass_index, (segment_count > 1) ? segment_count : 0);
            fflush(stdout);

            for (int b = 0; b < segment_count; b++) {
                gsp_route_segment_t left_before = {0};
                gsp_route_segment_t right_before = {0};
                double before_total;
                int right_idx;
                int boundary_loc_id;
                int boundary_port_id = 0;
                char boundary_port_name[128];
                double boundary_time_limit_seconds = (double)sweep_cfg.mip_time_limit_seconds;
                clock_gettime(CLOCK_MONOTONIC, &t_now);
                if (sweep_cfg.global_time_limit_seconds > 0 &&
                    elapsed_seconds(t_start, t_now) >= (double)sweep_cfg.global_time_limit_seconds) {
                    printf("Stopping sweep pass%d: global wall-clock limit reached before boundary %d (%.2fs >= %ds)\n",
                           pass_index, b + 1,
                           elapsed_seconds(t_start, t_now),
                           sweep_cfg.global_time_limit_seconds);
                    fflush(stdout);
                    keep_running = 0;
                    break;
                }
                if (sweep_cfg.global_time_limit_seconds > 0) {
                    double remaining_seconds =
                        (double)sweep_cfg.global_time_limit_seconds - elapsed_seconds(t_start, t_now);
                    if (remaining_seconds <= 0.0) {
                        keep_running = 0;
                        break;
                    }
                    if (boundary_time_limit_seconds <= 0.0 || remaining_seconds < boundary_time_limit_seconds) {
                        boundary_time_limit_seconds = remaining_seconds;
                    }
                }
                if (!active[b]) continue;
                active[b] = 0;
                boundary_attempts++;
                total_boundary_attempts++;
                right_idx = (b + 1) % segment_count;
                before_total = segments[b].distance_nm + segments[right_idx].distance_nm;
                left_before.count = segments[b].count;
                right_before.count = segments[right_idx].count;
                left_before.signed_station_ids = (int*)malloc((size_t)left_before.count * sizeof(int));
                right_before.signed_station_ids = (int*)malloc((size_t)right_before.count * sizeof(int));
                if (!left_before.signed_station_ids || !right_before.signed_station_ids) {
                    free(left_before.signed_station_ids);
                    free(right_before.signed_station_ids);
                    free(boundary_port_ids);
                    free(boundary_gain_nm);
                    free(pass_mip_solve_details);
                    free(active);
                    GRBfreeenv(env);
                    goto cleanup;
                }
                memcpy(left_before.signed_station_ids, segments[b].signed_station_ids, (size_t)left_before.count * sizeof(int));
                memcpy(right_before.signed_station_ids, segments[right_idx].signed_station_ids, (size_t)right_before.count * sizeof(int));
                boundary_loc_id = segments[b].end_loc_id;
                if (boundary_loc_id <= 0) boundary_loc_id = segments[right_idx].start_loc_id;
                if (!lookup_port_info(db, boundary_loc_id, &boundary_port_id, boundary_port_name, sizeof(boundary_port_name))) {
                    snprintf(boundary_port_name, sizeof(boundary_port_name), "loc%d", boundary_loc_id);
                }
                boundary_port_ids[b] = boundary_port_id;
                printf("  pass%d boundary %d/%d: seg%d=%d stations -> seg%d=%d stations via port_id=%d name=%s loc_id=%d\n",
                       pass_index, b + 1, segment_count, b + 1, segments[b].count, right_idx + 1, segments[right_idx].count,
                       boundary_port_id, boundary_port_name, boundary_loc_id);
                fflush(stdout);
                if (optimize_boundary(env, &inst, &boat, &segments[b], &segments[right_idx],
                                      sweep_cfg.haul_distance_scale,
                                      &pass_mip_solve_count,
                                      &pass_mip_solve_details,
                                      &pass_mip_detail_count,
                                      &pass_mip_detail_capacity,
                                      b + 1,
                                      b + 1,
                                      right_idx + 1,
                                      boundary_time_limit_seconds)) {
                    int station_changes = count_segment_station_changes(&left_before, &segments[b]) +
                                          count_segment_station_changes(&right_before, &segments[right_idx]);
                    double after_total = segments[b].distance_nm + segments[right_idx].distance_nm;
                    double improvement_nm = before_total - after_total;
                    boundary_gain_nm[b] = improvement_nm;
                    changed = 1;
                    boundary_changes++;
                    total_boundary_changes++;
                    if (segment_count > 1) active[(b - 1 + segment_count) % segment_count] = 1;
                    active[b] = 1;
                    if (segment_count > 1) active[right_idx] = 1;
                    printf("    improved: %.2f -> %.2f nm, gain=%.2f nm, moved_station_marks=%d\n",
                           before_total, after_total, improvement_nm, station_changes);
                } else {
                    printf("    no change\n");
                }
                fflush(stdout);
                free(left_before.signed_station_ids);
                free(right_before.signed_station_ids);
            }

            free_solution(&current_solution);
            if (!segment_to_solution(&inst, &boat, segments, segment_count, &current_solution)) {
                free(boundary_port_ids);
                free(boundary_gain_nm);
                free(pass_mip_solve_details);
                free(active);
                GRBfreeenv(env);
                goto cleanup;
            }

            clock_gettime(CLOCK_MONOTONIC, &t_pass_end);
            clock_gettime(CLOCK_MONOTONIC, &t_now);
            snapshot.pass_index = pass_index;
            snapshot.changed = changed;
            snapshot.boundary_count = segment_count;
            snapshot.boundary_port_ids = boundary_port_ids;
            boundary_port_ids = NULL;
            snapshot.boundary_improvement_gain_nm = boundary_gain_nm;
            boundary_gain_nm = NULL;
            snapshot.boundary_attempts = boundary_attempts;
            snapshot.boundary_changes = boundary_changes;
            snapshot.mip_solve_count = pass_mip_solve_count;
            snapshot.mip_detail_count = pass_mip_detail_count;
            snapshot.mip_detail_capacity = pass_mip_detail_capacity;
            snapshot.mip_solve_details = pass_mip_solve_details;
            pass_mip_solve_details = NULL;
            snapshot.pass_runtime_seconds = elapsed_seconds(t_pass_start, t_pass_end);
            snapshot.total_runtime_seconds = elapsed_seconds(t_start, t_now);
            snapshot.feasible = solution_is_feasible(&inst, &boat, &current_solution);
            if (!copy_solution(&snapshot.solution, &current_solution)) {
                free(snapshot.mip_solve_details);
                free(snapshot.boundary_port_ids);
                free(snapshot.boundary_improvement_gain_nm);
                free(active);
                GRBfreeenv(env);
                goto cleanup;
            }
            if (!append_snapshot(&snapshots, &snapshot_count, &snapshot_capacity, &snapshot)) {
                free(snapshot.mip_solve_details);
                free(snapshot.boundary_port_ids);
                free(snapshot.boundary_improvement_gain_nm);
                free_solution(&snapshot.solution);
                free(active);
                GRBfreeenv(env);
                goto cleanup;
            }
            total_mip_solves += pass_mip_solve_count;
            if (debug_mode) {
                if (!write_sweep_json(output, db, &boat, &inst,
                                      &sweep_cfg,
                                      snapshots, snapshot_count,
                                      total_boundary_attempts,
                                      total_boundary_changes,
                                      total_mip_solves,
                                      preprocessing_seconds,
                                      snapshot.total_runtime_seconds,
                                      strategy, 0)) {
                    fprintf(stderr, "ERROR: Failed to persist sweep snapshot JSON\n");
                    free(boundary_port_ids);
                    free(boundary_gain_nm);
                    free(pass_mip_solve_details);
                    free(active);
                    GRBfreeenv(env);
                    goto cleanup;
                }
            }
            printf("Completed pass%d: changed=%d boundary_changes=%d total_distance=%.2f runtime=%.2fs total=%.2fs\n",
                   pass_index, changed, boundary_changes, snapshot.solution.total_distance,
                   snapshot.pass_runtime_seconds, snapshot.total_runtime_seconds);
            fflush(stdout);

            pass_index++;
            if (!changed) keep_running = 0;
            if (time_limit > 0 && snapshot.total_runtime_seconds >= (double)time_limit) keep_running = 0;
            if (sweep_cfg.global_time_limit_seconds > 0 &&
                snapshot.total_runtime_seconds >= (double)sweep_cfg.global_time_limit_seconds) {
                keep_running = 0;
            }
        }

        free(active);
        GRBfreeenv(env);
    }

    if (snapshot_count > 0) {
        clock_gettime(CLOCK_MONOTONIC, &t_now);
        if (!write_sweep_json(output, db, &boat, &inst,
                              &sweep_cfg,
                              snapshots, snapshot_count,
                              total_boundary_attempts,
                              total_boundary_changes,
                              total_mip_solves,
                              preprocessing_seconds,
                              elapsed_seconds(t_start, t_now),
                              strategy, 1)) {
            fprintf(stderr, "ERROR: Failed to persist final sweep snapshot JSON\n");
            goto cleanup;
        }
    }

    rc = 0;
    if (segments && segment_count > 0) {
        print_segment_station_order(segments, segment_count);
    }
#endif

cleanup:
    if (db) sqlite3_close(db);
    free_instance(&inst);
    free_segments(segments, segment_count);
    free_solution(&current_solution);
    free_snapshot_array(snapshots, snapshot_count);
    return rc;
}
