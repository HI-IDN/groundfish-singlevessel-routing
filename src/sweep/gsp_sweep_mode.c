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
#include "../mip/include/mip_fixedport.h"
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
    int fallback_changes;
    int mip_solve_count;
    int mip_detail_count;
    int mip_detail_capacity;
    gsp_mip_solve_detail_t *mip_solve_details;
    double pass_runtime_seconds;
    double total_runtime_seconds;
    int feasible;
} sweep_snapshot_t;

typedef struct {
    int l1seg_time_limit_seconds;
    int l2seg_time_limit_seconds;
    int global_time_limit_seconds;
    int max_iterations;
    int enable_fallback;
} sweep_config_t;

typedef struct {
    int l1seg_time_limit_seconds;
    int l2seg_time_limit_seconds;
    int global_time_limit_seconds;
    int max_iterations;
    int enable_fallback;
} sweep_metadata_extra_t;

static int count_segment_station_changes(const gsp_route_segment_t *before,
                                         const gsp_route_segment_t *after);

static void write_sweep_metadata_extra(FILE *fp, const char *indent, const void *ctx) {
    const sweep_metadata_extra_t *extra = (const sweep_metadata_extra_t*)ctx;
    const char *base = indent ? indent : "";
    if (!fp || !extra) return;
    fprintf(fp, "%s  \"mip_time_limit_seconds\": %d,\n", base, extra->l2seg_time_limit_seconds);
    fprintf(fp, "%s  \"l1seg_time_limit_seconds\": %d,\n", base, extra->l1seg_time_limit_seconds);
    fprintf(fp, "%s  \"l2seg_time_limit_seconds\": %d,\n", base, extra->l2seg_time_limit_seconds);
    fprintf(fp, "%s  \"global_time_limit_seconds\": %d,\n", base, extra->global_time_limit_seconds);
    fprintf(fp, "%s  \"max_iterations\": %d,\n", base, extra->max_iterations);
    fprintf(fp, "%s  \"fallback_enabled\": %s", base, extra->enable_fallback ? "true" : "false");
}

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

static int read_bool_after_colon(const char *line, int default_value) {
    const char *p = strchr(line, ':');
    char value[32];
    int i = 0;
    if (!p) return default_value;
    p++;
    while (*p && isspace((unsigned char)*p)) p++;
    while (*p && !isspace((unsigned char)*p) && *p != '#' && i < (int)sizeof(value) - 1) {
        value[i++] = (char)tolower((unsigned char)*p++);
    }
    value[i] = '\0';
    if (strcmp(value, "1") == 0 || strcmp(value, "true") == 0 ||
        strcmp(value, "yes") == 0 || strcmp(value, "on") == 0) return 1;
    if (strcmp(value, "0") == 0 || strcmp(value, "false") == 0 ||
        strcmp(value, "no") == 0 || strcmp(value, "off") == 0) return 0;
    return default_value;
}

static void read_sweep_config_from_yaml(const char *yaml_path, sweep_config_t *cfg) {
    FILE *fp;
    char line[MAX_LINE];
    int section = 0;

    if (!cfg) return;
    cfg->l1seg_time_limit_seconds = 0;
    cfg->l2seg_time_limit_seconds = 0;
    cfg->global_time_limit_seconds = 0;
    cfg->max_iterations = 0;
    cfg->enable_fallback = 0;

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
        } else if (section == 1 && strncmp(trim, "fallback_enabled:", 17) == 0) {
            cfg->enable_fallback = read_bool_after_colon(trim, cfg->enable_fallback);
        }
    }

    fclose(fp);
    cfg->l1seg_time_limit_seconds = (int)read_segment_mip_time_limit_from_yaml(yaml_path);
    cfg->l2seg_time_limit_seconds = (int)read_sweep_mip_time_limit_from_yaml(yaml_path);
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
        if (*p == '{') {
            const char *total_value = find_key_in_object(p, "total");
            if (total_value) {
                p = total_value;
                return parse_json_double(&p, out_value);
            }
        } else {
            return parse_json_double(&p, out_value);
        }
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
            /* Skip intra-station haul arcs so distance_nm is transit-only,
               consistent with solve_segment_tsp which zeros haul in its objective. */
            int is_haul = 0;
            for (int k = 0; k < seg->count; k++) {
                int sidx = find_station_index(inst, abs(seg->signed_station_ids[k]));
                if (sidx >= 0) {
                    int s_entry = inst->nodes[sidx].start_loc_id;
                    int s_exit  = inst->nodes[sidx].end_loc_id;
                    if ((from_loc == s_entry && to_loc == s_exit) ||
                        (from_loc == s_exit  && to_loc == s_entry)) {
                        is_haul = 1;
                        break;
                    }
                }
            }
            if (is_haul) continue;
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
    /* haul arcs always excluded — transit-only objective */

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
                              double time_limit_seconds) {
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
                           NULL, NULL, NULL, NULL, NULL,
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
                                   double time_limit_seconds) {
    for (int i = 0; i < n_segments; i++) {
        if (!reoptimize_segment(env, inst, &segments[i], time_limit_seconds)) return 0;
    }
    return 1;
}

static int solve_boundary_capacity_mip(GRBenv *env,
                                       const nn_instance_t *inst,
                                       const gsp_boat_t *boat,
                                       const gsp_route_segment_t *left,
                                       const gsp_route_segment_t *right,
                                       int boundary_loc_id,
                                       double time_limit_seconds,
                                       int **out_left_ids,
                                       int *out_left_count,
                                       int **out_right_ids,
                                       int *out_right_count,
                                       double *out_gap_percent,
                                       double *out_runtime_seconds,
                                       int *out_model_num_vars,
                                       int *out_model_num_constrs) {
    int total = left->count + right->count;
    Station *stations = NULL;
    int *candidate_ports = NULL;
    Boat local_boat;
    mip_fixedport_instance_t mip_instance;
    mip_fixedport_solution_t mip_solution;
    mip_params_t mip_params;
    int seen_port = 0;
    int left_count = 0;
    int right_count = 0;
    int *left_ids = NULL;
    int *right_ids = NULL;
    int error = 1;

    if (!inst || !boat || !left || !right || boundary_loc_id <= 0 || total <= 0) return 0;
    if (out_left_ids) *out_left_ids = NULL;
    if (out_right_ids) *out_right_ids = NULL;
    if (out_left_count) *out_left_count = 0;
    if (out_right_count) *out_right_count = 0;

    stations = (Station*)calloc((size_t)total, sizeof(Station));
    candidate_ports = (int*)malloc(sizeof(int));
    left_ids = (int*)malloc((size_t)total * sizeof(int));
    right_ids = (int*)malloc((size_t)total * sizeof(int));
    if (!stations || !candidate_ports || !left_ids || !right_ids) goto quit;

    for (int i = 0; i < total; i++) {
        int signed_station_id = (i < left->count)
            ? left->signed_station_ids[i]
            : right->signed_station_ids[i - left->count];
        int station_idx = find_station_index(inst, abs(signed_station_id));
        if (station_idx < 0) goto quit;
        stations[i].station_id = inst->nodes[station_idx].table_id;
        stations[i].amount = inst->nodes[station_idx].amount;
        stations[i].start_location_id = inst->nodes[station_idx].start_loc_id;
        stations[i].end_location_id = inst->nodes[station_idx].end_loc_id;
    }

    local_boat.boat_id = boat->boat_id;
    local_boat.name = (char*)boat->boat_name;
    local_boat.capacity = (int)boat->boat_capacity;
    local_boat.location_id = left->start_loc_id;
    candidate_ports[0] = boundary_loc_id;

    memset(&mip_instance, 0, sizeof(mip_instance));
    mip_instance.boat = &local_boat;
    mip_instance.end_location_id = right->end_loc_id;
    mip_instance.stations = stations;
    mip_instance.n_stations = total;
    mip_instance.candidate_port_location_ids = candidate_ports;
    mip_instance.candidate_port_count = 1;
    mip_instance.distances = inst->distances;
    mip_instance.max_location_id = inst->max_loc_id;

    memset(&mip_params, 0, sizeof(mip_params));
    mip_params.verbose = 0;
    mip_params.shared_env = env;
    mip_params.time_limit_seconds = time_limit_seconds;

    memset(&mip_solution, 0, sizeof(mip_solution));
    if (solve_mip_fixedport(&mip_instance, &mip_params, &mip_solution) != 0) goto quit;

    for (int i = 0; i < mip_solution.visit_count; i++) {
        int signed_visit_id = mip_solution.signed_visit_ids[i];
        int visit_id = abs(signed_visit_id);
        int sign = (signed_visit_id < 0) ? -1 : 1;
        if (visit_id <= total) {
            int station_id = stations[visit_id - 1].station_id;
            if (!seen_port) left_ids[left_count++] = sign * station_id;
            else right_ids[right_count++] = sign * station_id;
        } else if (visit_id == total + 1) {
            if (seen_port) goto quit;
            seen_port = 1;
        } else {
            goto quit;
        }
    }
    if (!seen_port || left_count <= 0 || right_count <= 0) goto quit;

    if (out_gap_percent) *out_gap_percent = mip_solution.gap * 100.0;
    if (out_runtime_seconds) *out_runtime_seconds = mip_solution.runtime_seconds;
    if (out_model_num_vars) {
        int size = 1 + total + 1;
        int n = 2 * size;
        *out_model_num_vars = 3 * n * n;
    }
    if (out_model_num_constrs) {
        int size = 1 + total + 1;
        *out_model_num_constrs = 5 * size;
    }

    *out_left_ids = left_ids;
    *out_left_count = left_count;
    *out_right_ids = right_ids;
    *out_right_count = right_count;
    left_ids = NULL;
    right_ids = NULL;
    error = 0;

quit:
    free_mip_fixedport_solution(&mip_solution);
    free(stations);
    free(candidate_ports);
    free(left_ids);
    free(right_ids);
    return error == 0;
}

static int append_mip_detail_checked(gsp_mip_solve_detail_t **solve_details,
                                     int *solve_detail_count,
                                     int *solve_detail_capacity,
                                     const gsp_mip_solve_detail_t *detail) {
    if (!solve_details || !solve_detail_count || !solve_detail_capacity) return 1;
    return gsp_append_mip_solve_detail(solve_details, solve_detail_count,
                                       solve_detail_capacity, detail);
}

static int copy_route_segment_local(const gsp_route_segment_t *src, gsp_route_segment_t *dst) {
    if (!src || !dst) return 0;
    memset(dst, 0, sizeof(*dst));
    *dst = *src;
    dst->signed_station_ids = NULL;
    if (src->count > 0) {
        dst->signed_station_ids = (int*)malloc((size_t)src->count * sizeof(int));
        if (!dst->signed_station_ids) return 0;
        memcpy(dst->signed_station_ids, src->signed_station_ids, (size_t)src->count * sizeof(int));
    }
    return 1;
}

static int copy_segment_with_extra_station(const gsp_route_segment_t *src,
                                           int signed_station_id,
                                           gsp_route_segment_t *dst) {
    if (!copy_route_segment_local(src, dst)) return 0;
    if (signed_station_id != 0) {
        int *tmp = (int*)realloc(dst->signed_station_ids,
                                (size_t)(dst->count + 1) * sizeof(int));
        if (!tmp) {
            free(dst->signed_station_ids);
            memset(dst, 0, sizeof(*dst));
            return 0;
        }
        dst->signed_station_ids = tmp;
        dst->signed_station_ids[dst->count++] = signed_station_id;
        dst->capacity = dst->count;
    }
    return 1;
}

static int copy_segment_without_station(const gsp_route_segment_t *src,
                                        int remove_pos,
                                        gsp_route_segment_t *dst) {
    int k = 0;
    if (!src || !dst || remove_pos < 0 || remove_pos >= src->count || src->count <= 1) return 0;
    memset(dst, 0, sizeof(*dst));
    *dst = *src;
    dst->count = src->count - 1;
    dst->capacity = dst->count;
    dst->signed_station_ids = (int*)malloc((size_t)dst->count * sizeof(int));
    if (!dst->signed_station_ids) return 0;
    for (int i = 0; i < src->count; i++) {
        if (i == remove_pos) continue;
        dst->signed_station_ids[k++] = src->signed_station_ids[i];
    }
    return 1;
}

static double station_to_loc_distance(const nn_instance_t *inst, int signed_station_id, int loc_id) {
    int station_idx = find_station_index(inst, abs(signed_station_id));
    double d1, d2;
    if (station_idx < 0) return 1e100;
    if (!inst->distances || loc_id < 0 || loc_id >= inst->max_loc_id) return 1e100;
    d1 = (inst->nodes[station_idx].start_loc_id >= 0 &&
          inst->nodes[station_idx].start_loc_id < inst->max_loc_id &&
          inst->distances[inst->nodes[station_idx].start_loc_id])
        ? inst->distances[inst->nodes[station_idx].start_loc_id][loc_id] : 0.0;
    d2 = (inst->nodes[station_idx].end_loc_id >= 0 &&
          inst->nodes[station_idx].end_loc_id < inst->max_loc_id &&
          inst->distances[inst->nodes[station_idx].end_loc_id])
        ? inst->distances[inst->nodes[station_idx].end_loc_id][loc_id] : 0.0;
    if (d1 <= 0.0) return d2;
    if (d2 <= 0.0) return d1;
    return (d1 < d2) ? d1 : d2;
}

static double station_to_segment_pointset_distance(const nn_instance_t *inst,
                                                   int signed_station_id,
                                                   const gsp_route_segment_t *segment) {
    double best = 1e100;
    if (!inst || !segment) return best;
    {
        double d = station_to_loc_distance(inst, signed_station_id, segment->start_loc_id);
        if (d < best) best = d;
        d = station_to_loc_distance(inst, signed_station_id, segment->end_loc_id);
        if (d < best) best = d;
    }
    for (int i = 0; i < segment->count; i++) {
        int station_idx = find_station_index(inst, abs(segment->signed_station_ids[i]));
        double d;
        if (station_idx < 0) continue;
        d = station_to_loc_distance(inst, signed_station_id, inst->nodes[station_idx].start_loc_id);
        if (d < best) best = d;
        d = station_to_loc_distance(inst, signed_station_id, inst->nodes[station_idx].end_loc_id);
        if (d < best) best = d;
    }
    return best;
}

static int select_fallback_donor(const nn_instance_t *inst,
                                 const gsp_boat_t *boat,
                                 const gsp_route_segment_t *segments,
                                 int segment_count,
                                 int left_idx,
                                 int right_idx,
                                 int boundary_loc_id,
                                 int *out_donor_idx,
                                 int *out_station_pos,
                                 int *out_nearer_left) {
    double best_score = 1e100;
    int best_donor = -1;
    int best_pos = -1;
    int best_left = 1;

    if (!inst || !boat || !segments || segment_count <= 2) return 0;
    for (int s = 0; s < segment_count; s++) {
        int left_prev = (left_idx - 1 + segment_count) % segment_count;
        int right_next = (right_idx + 1) % segment_count;
        if (s == left_idx || s == right_idx) continue;
        if (s == left_prev || s == right_next) continue;
        if (segments[s].count <= 1) continue;
        for (int i = 0; i < segments[s].count; i++) {
            int signed_id = segments[s].signed_station_ids[i];
            int amount = station_amount(inst, abs(signed_id));
            int left_feasible = ((double)(segments[left_idx].catch_amount + amount) <= boat->boat_capacity);
            int right_feasible = ((double)(segments[right_idx].catch_amount + amount) <= boat->boat_capacity);
            double to_left;
            double to_right;
            double to_boundary;
            int nearer_left;
            double score;
            if (!left_feasible && !right_feasible) continue;
            to_left = station_to_segment_pointset_distance(inst, signed_id, &segments[left_idx]);
            to_right = station_to_segment_pointset_distance(inst, signed_id, &segments[right_idx]);
            if (left_feasible && right_feasible) nearer_left = (to_left <= to_right);
            else nearer_left = left_feasible;
            to_boundary = station_to_loc_distance(inst, signed_id, boundary_loc_id);
            score = to_boundary;
            if (to_left < score) score = to_left;
            if (to_right < score) score = to_right;
            if (score < best_score) {
                best_score = score;
                best_donor = s;
                best_pos = i;
                best_left = nearer_left;
            }
        }
    }

    if (best_donor < 0) return 0;
    if (out_donor_idx) *out_donor_idx = best_donor;
    if (out_station_pos) *out_station_pos = best_pos;
    if (out_nearer_left) *out_nearer_left = best_left;
    return 1;
}

static int optimize_boundary_candidate(GRBenv *env,
                                       const nn_instance_t *inst,
                                       const gsp_boat_t *boat,
                                       const gsp_route_segment_t *left,
                                       const gsp_route_segment_t *right,
                                       int boundary_index,
                                       int left_segment_index,
                                       int right_segment_index,
                                       int boundary_loc_id,
                                       int fallback_used,
                                       int pass_index,
                                       double l1seg_time_limit_seconds,
                                       double l2seg_time_limit_seconds,
                                       int *solve_count,
                                       gsp_mip_solve_detail_t **solve_details,
                                       int *solve_detail_count,
                                       int *solve_detail_capacity,
                                       int **best_left_ids,
                                       int *best_left_count,
                                       int *best_left_catch,
                                       double *best_left_dist,
                                       int **best_right_ids,
                                       int *best_right_count,
                                       int *best_right_catch,
                                       double *best_right_dist) {
    int *left_station_ids = NULL;
    int *right_station_ids = NULL;
    int *left_abs_ids = NULL;
    int *right_abs_ids = NULL;
    int left_count = 0, right_count = 0;
    double capacity_gap_percent = -1.0, capacity_runtime_seconds = -1.0;
    int capacity_num_vars = 0, capacity_num_constrs = 0;
    int total = left->count + right->count;
    int ok = 0;

    if (best_left_ids) *best_left_ids = NULL;
    if (best_right_ids) *best_right_ids = NULL;

    if (!solve_boundary_capacity_mip(env, inst, boat, left, right,
                                     boundary_loc_id,
                                     l2seg_time_limit_seconds,
                                     &left_station_ids, &left_count,
                                     &right_station_ids, &right_count,
                                     &capacity_gap_percent,
                                     &capacity_runtime_seconds,
                                     &capacity_num_vars,
                                     &capacity_num_constrs)) {
        return 0;
    }
    if (solve_count) (*solve_count)++;

    {
        gsp_mip_solve_detail_t detail;
        gsp_mip_solve_detail_init(&detail);
        detail.pass_index = pass_index;
        detail.boundary_index = boundary_index;
        detail.candidate_split_index = fallback_used ? 1 : 0;
        detail.segment_index = 0;
        detail.segment_role = 2;
        detail.station_count = total;
        detail.node_count = total + 3;
        detail.moved_stations =
            count_segment_station_changes(left, &(gsp_route_segment_t){.signed_station_ids = left_station_ids, .count = left_count}) +
            count_segment_station_changes(right, &(gsp_route_segment_t){.signed_station_ids = right_station_ids, .count = right_count});
        detail.model_num_vars = capacity_num_vars;
        detail.model_num_constrs = capacity_num_constrs;
        detail.runtime_seconds = capacity_runtime_seconds;
        detail.gap_percent = capacity_gap_percent;
        if (!append_mip_detail_checked(solve_details, solve_detail_count, solve_detail_capacity, &detail)) goto cleanup;
    }

    left_abs_ids = (int*)malloc((size_t)left_count * sizeof(int));
    right_abs_ids = (int*)malloc((size_t)right_count * sizeof(int));
    if (!left_abs_ids || !right_abs_ids) goto cleanup;
    for (int i = 0; i < left_count; i++) left_abs_ids[i] = abs(left_station_ids[i]);
    for (int i = 0; i < right_count; i++) right_abs_ids[i] = abs(right_station_ids[i]);

    {
        double runtime = -1.0, gap = -1.0;
        int vars = 0, constrs = 0;
        if (!solve_segment_tsp(env, inst, left->start_loc_id, boundary_loc_id,
                               left_abs_ids, left_count,
                               best_left_catch, best_left_dist, best_left_ids,
                               NULL, &gap, &runtime, &vars, &constrs,
                               l1seg_time_limit_seconds)) goto cleanup;
        if (solve_count) (*solve_count)++;
        {
            gsp_mip_solve_detail_t detail;
            gsp_mip_solve_detail_init(&detail);
            detail.pass_index = pass_index;
            detail.boundary_index = boundary_index;
            detail.candidate_split_index = fallback_used ? 1 : 0;
            detail.segment_index = left_segment_index;
            detail.segment_role = 1;
            detail.station_count = left_count;
            detail.node_count = left_count + 2;
            detail.moved_stations =
                count_segment_station_changes(left, &(gsp_route_segment_t){.signed_station_ids = *best_left_ids, .count = left_count});
            detail.model_num_vars = vars;
            detail.model_num_constrs = constrs;
            detail.runtime_seconds = runtime;
            detail.gap_percent = gap;
            if (!append_mip_detail_checked(solve_details, solve_detail_count, solve_detail_capacity, &detail)) goto cleanup;
        }
    }

    {
        double runtime = -1.0, gap = -1.0;
        int vars = 0, constrs = 0;
        if (!solve_segment_tsp(env, inst, boundary_loc_id, right->end_loc_id,
                               right_abs_ids, right_count,
                               best_right_catch, best_right_dist, best_right_ids,
                               NULL, &gap, &runtime, &vars, &constrs,
                               l1seg_time_limit_seconds)) goto cleanup;
        if (solve_count) (*solve_count)++;
        {
            gsp_mip_solve_detail_t detail;
            gsp_mip_solve_detail_init(&detail);
            detail.pass_index = pass_index;
            detail.boundary_index = boundary_index;
            detail.candidate_split_index = fallback_used ? 1 : 0;
            detail.segment_index = right_segment_index;
            detail.segment_role = 1;
            detail.station_count = right_count;
            detail.node_count = right_count + 2;
            detail.moved_stations =
                count_segment_station_changes(right, &(gsp_route_segment_t){.signed_station_ids = *best_right_ids, .count = right_count});
            detail.model_num_vars = vars;
            detail.model_num_constrs = constrs;
            detail.runtime_seconds = runtime;
            detail.gap_percent = gap;
            if (!append_mip_detail_checked(solve_details, solve_detail_count, solve_detail_capacity, &detail)) goto cleanup;
        }
    }

    *best_left_count = left_count;
    *best_right_count = right_count;
    ok = 1;

cleanup:
    free(left_station_ids);
    free(right_station_ids);
    free(left_abs_ids);
    free(right_abs_ids);
    if (!ok) {
        free(*best_left_ids);
        free(*best_right_ids);
        if (best_left_ids) *best_left_ids = NULL;
        if (best_right_ids) *best_right_ids = NULL;
    }
    return ok;
}

static int reoptimize_candidate_segment(GRBenv *env,
                                        const nn_instance_t *inst,
                                        const gsp_route_segment_t *baseline,
                                        gsp_route_segment_t *candidate,
                                        int pass_index,
                                        int boundary_index,
                                        int segment_index,
                                        double l1seg_time_limit_seconds,
                                        int *solve_count,
                                        gsp_mip_solve_detail_t **solve_details,
                                        int *solve_detail_count,
                                        int *solve_detail_capacity) {
    int *station_ids = NULL;
    int *signed_ids = NULL;
    int catch_amount = 0;
    double distance_nm = 0.0;
    double runtime = -1.0, gap = -1.0;
    int vars = 0, constrs = 0;

    if (!candidate || candidate->count <= 0) return 0;
    station_ids = (int*)malloc((size_t)candidate->count * sizeof(int));
    if (!station_ids) return 0;
    for (int i = 0; i < candidate->count; i++) station_ids[i] = abs(candidate->signed_station_ids[i]);

    if (!solve_segment_tsp(env, inst, candidate->start_loc_id, candidate->end_loc_id,
                           station_ids, candidate->count,
                           &catch_amount, &distance_nm, &signed_ids,
                           NULL, &gap, &runtime, &vars, &constrs,
                           l1seg_time_limit_seconds)) {
        free(station_ids);
        return 0;
    }
    free(station_ids);
    if (solve_count) (*solve_count)++;

    {
        gsp_mip_solve_detail_t detail;
        gsp_mip_solve_detail_init(&detail);
        detail.pass_index = pass_index;
        detail.boundary_index = boundary_index;
        detail.candidate_split_index = 1;
        detail.segment_index = segment_index;
        detail.segment_role = 1;
        detail.station_count = candidate->count;
        detail.node_count = candidate->count + 2;
        detail.moved_stations = baseline ? count_segment_station_changes(baseline, &(gsp_route_segment_t){.signed_station_ids = signed_ids, .count = candidate->count}) : 0;
        detail.model_num_vars = vars;
        detail.model_num_constrs = constrs;
        detail.runtime_seconds = runtime;
        detail.gap_percent = gap;
        if (!append_mip_detail_checked(solve_details, solve_detail_count, solve_detail_capacity, &detail)) {
            free(signed_ids);
            return 0;
        }
    }

    free(candidate->signed_station_ids);
    candidate->signed_station_ids = signed_ids;
    candidate->catch_amount = catch_amount;
    candidate->distance_nm = distance_nm;
    return 1;
}

static int optimize_boundary_fallback(GRBenv *env,
                                      const nn_instance_t *inst,
                                      const gsp_boat_t *boat,
                                      gsp_route_segment_t *segments,
                                      int segment_count,
                                      int left_idx,
                                      int right_idx,
                                      int boundary_index,
                                      int boundary_loc_id,
                                      int pass_index,
                                      double l1seg_time_limit_seconds,
                                      double l2seg_time_limit_seconds,
                                      int *solve_count,
                                      gsp_mip_solve_detail_t **solve_details,
                                      int *solve_detail_count,
                                      int *solve_detail_capacity,
                                      int *changed_donor_index,
                                      double *changed_donor_before_distance) {
    gsp_route_segment_t *left = &segments[left_idx];
    gsp_route_segment_t *right = &segments[right_idx];
    int donor_idx = -1, donor_pos = -1, nearer_left = 1;
    int donor_signed_id = 0;
    gsp_route_segment_t fallback_left = {0};
    gsp_route_segment_t fallback_right = {0};
    gsp_route_segment_t fallback_donor = {0};
    int *fallback_left_ids = NULL, *fallback_right_ids = NULL;
    int fallback_left_count = 0, fallback_right_count = 0;
    int fallback_left_catch = 0, fallback_right_catch = 0;
    double fallback_left_dist = 0.0, fallback_right_dist = 0.0;
    double fallback_current_total;
    int accepted = 0;

    if (!select_fallback_donor(inst, boat, segments, segment_count, left_idx, right_idx,
                               boundary_loc_id, &donor_idx, &donor_pos, &nearer_left)) {
        return 0;
    }
    donor_signed_id = segments[donor_idx].signed_station_ids[donor_pos];

    if (!copy_segment_with_extra_station(left, nearer_left ? donor_signed_id : 0, &fallback_left)) goto cleanup;
    if (!copy_segment_with_extra_station(right, nearer_left ? 0 : donor_signed_id, &fallback_right)) goto cleanup;
    if (!copy_segment_without_station(&segments[donor_idx], donor_pos, &fallback_donor)) goto cleanup;

    if (!optimize_boundary_candidate(env, inst, boat, &fallback_left, &fallback_right,
                                     boundary_index, left_idx + 1, right_idx + 1,
                                     boundary_loc_id, 1, pass_index,
                                     l1seg_time_limit_seconds, l2seg_time_limit_seconds,
                                     solve_count, solve_details, solve_detail_count,
                                     solve_detail_capacity,
                                     &fallback_left_ids, &fallback_left_count,
                                     &fallback_left_catch, &fallback_left_dist,
                                     &fallback_right_ids, &fallback_right_count,
                                     &fallback_right_catch, &fallback_right_dist)) {
        goto cleanup;
    }
    if (!reoptimize_candidate_segment(env, inst, &segments[donor_idx], &fallback_donor,
                                      pass_index, boundary_index, donor_idx + 1,
                                      l1seg_time_limit_seconds, solve_count,
                                      solve_details, solve_detail_count,
                                      solve_detail_capacity)) {
        goto cleanup;
    }

    fallback_current_total = left->distance_nm + right->distance_nm + segments[donor_idx].distance_nm;
    if (fallback_left_dist + fallback_right_dist + fallback_donor.distance_nm + SWEEP_EPS < fallback_current_total) {
        double donor_before_distance = segments[donor_idx].distance_nm;
        free(left->signed_station_ids);
        free(right->signed_station_ids);
        free(segments[donor_idx].signed_station_ids);
        left->signed_station_ids = fallback_left_ids;
        right->signed_station_ids = fallback_right_ids;
        segments[donor_idx].signed_station_ids = fallback_donor.signed_station_ids;
        fallback_left_ids = NULL;
        fallback_right_ids = NULL;
        fallback_donor.signed_station_ids = NULL;
        left->count = fallback_left_count;
        right->count = fallback_right_count;
        left->capacity = fallback_left_count;
        right->capacity = fallback_right_count;
        segments[donor_idx].count = fallback_donor.count;
        segments[donor_idx].capacity = fallback_donor.count;
        left->catch_amount = fallback_left_catch;
        right->catch_amount = fallback_right_catch;
        segments[donor_idx].catch_amount = fallback_donor.catch_amount;
        left->distance_nm = fallback_left_dist;
        right->distance_nm = fallback_right_dist;
        segments[donor_idx].distance_nm = fallback_donor.distance_nm;
        if (changed_donor_index) *changed_donor_index = donor_idx;
        if (changed_donor_before_distance) *changed_donor_before_distance = donor_before_distance;
        accepted = 1;
    }

cleanup:
    free(fallback_left.signed_station_ids);
    free(fallback_right.signed_station_ids);
    free(fallback_donor.signed_station_ids);
    free(fallback_left_ids);
    free(fallback_right_ids);
    return accepted;
}

static int optimize_boundary(GRBenv *env, const nn_instance_t *inst, const gsp_boat_t *boat,
                             gsp_route_segment_t *segments, int segment_count,
                             int left_idx, int right_idx,
                             int *solve_count,
                             gsp_mip_solve_detail_t **solve_details,
                             int *solve_detail_count,
                             int *solve_detail_capacity,
                             int boundary_index,
                             int boundary_loc_id,
                             int pass_index,
                             double l1seg_time_limit_seconds,
                             double l2seg_time_limit_seconds,
                             int enable_fallback,
                             int *changed_donor_index,
                             double *changed_donor_before_distance) {
    gsp_route_segment_t *left = &segments[left_idx];
    gsp_route_segment_t *right = &segments[right_idx];
    double current_total = left->distance_nm + right->distance_nm;
    int *best_left_ids = NULL, *best_right_ids = NULL;
    int best_left_count = 0, best_right_count = 0;
    int best_left_catch = 0, best_right_catch = 0;
    double best_left_dist = 0.0, best_right_dist = 0.0;

    if (changed_donor_index) *changed_donor_index = -1;
    if (changed_donor_before_distance) *changed_donor_before_distance = 0.0;
    if (optimize_boundary_candidate(env, inst, boat, left, right,
                                    boundary_index, left_idx + 1, right_idx + 1,
                                    boundary_loc_id, 0, pass_index,
                                    l1seg_time_limit_seconds, l2seg_time_limit_seconds,
                                    solve_count, solve_details, solve_detail_count,
                                    solve_detail_capacity,
                                    &best_left_ids, &best_left_count, &best_left_catch, &best_left_dist,
                                    &best_right_ids, &best_right_count, &best_right_catch, &best_right_dist)) {
        if (best_left_dist + best_right_dist + SWEEP_EPS < current_total) {
            free(left->signed_station_ids);
            free(right->signed_station_ids);
            left->signed_station_ids = best_left_ids;
            right->signed_station_ids = best_right_ids;
            left->count = best_left_count;
            right->count = best_right_count;
            left->capacity = best_left_count;
            right->capacity = best_right_count;
            left->catch_amount = best_left_catch;
            right->catch_amount = best_right_catch;
            left->distance_nm = best_left_dist;
            right->distance_nm = best_right_dist;
            return 1;
        }
        free(best_left_ids);
        free(best_right_ids);
    }

    if (!enable_fallback) return 0;

    return optimize_boundary_fallback(env, inst, boat, segments, segment_count,
                                      left_idx, right_idx,
                                      boundary_index, boundary_loc_id, pass_index,
                                      l1seg_time_limit_seconds, l2seg_time_limit_seconds,
                                      solve_count, solve_details, solve_detail_count,
                                      solve_detail_capacity,
                                      changed_donor_index, changed_donor_before_distance);
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
    int *station_counts = NULL;
    gsp_distance_breakdown_t *segment_breakdowns = NULL;
    gsp_distance_breakdown_t total_breakdown;

    if (!fp || !db || !inst || !sol || !boat) return;
    memset(&total_breakdown, 0, sizeof(total_breakdown));
    segment_breakdowns = (gsp_distance_breakdown_t*)calloc((size_t)sol->segment_count, sizeof(gsp_distance_breakdown_t));
    if (!segment_breakdowns) return;
    sweep_compute_segment_breakdowns(inst, sol, boat->boat_loc_id, segment_breakdowns, &total_breakdown);

    station_counts = (int*)calloc((size_t)sol->segment_count, sizeof(int));
    if (!station_counts) {
        free(segment_breakdowns);
        return;
    }

    seen_waypoint_location_ids = (unsigned char*)calloc((size_t)inst->max_loc_id, sizeof(unsigned char));
    if (!seen_waypoint_location_ids) {
        free(station_counts);
        free(segment_breakdowns);
        return;
    }

    fprintf(fp, "      \"tour_segments_location_ids\": [\n");
    for (int s = 0; s < sol->segment_count; s++) {
        int segment_end_loc = (s == sol->segment_count - 1) ? boat->boat_loc_id : sol->tour[sol->segment_ends[s]];
        int base_cap = 2 * sol->visit_station_count + 2;
        int base_n = 0;
        int base_ok = 1;
        int *base = (int*)malloc((size_t)base_cap * sizeof(int));
        fprintf(fp, "        [");
        if (!base) {
            fprintf(fp, "]%s\n", (s + 1 < sol->segment_count) ? "," : "");
            continue;
        }
        base[base_n++] = (s == 0) ? boat->boat_loc_id : sol->tour[sol->segment_ends[s - 1]];
        for (int i = 0; i < sol->visit_station_count; i++) {
            if (sol->visit_station_segment[i] == s) {
                int station_idx = find_station_index(inst, sol->visit_station_ids[i]);
                int direction = (sol->visit_station_direction && sol->visit_station_direction[i] < 0) ? -1 : 1;
                int entry_loc;
                int exit_loc;
                if (station_idx < 0) continue;
                station_counts[s]++;
                entry_loc = (direction > 0) ? inst->nodes[station_idx].start_loc_id
                                            : inst->nodes[station_idx].end_loc_id;
                exit_loc = (direction > 0) ? inst->nodes[station_idx].end_loc_id
                                           : inst->nodes[station_idx].start_loc_id;
                if (!append_int(&base, &base_n, &base_cap, entry_loc) ||
                    !append_int(&base, &base_n, &base_cap, exit_loc)) {
                    base_ok = 0;
                    break;
                }
            }
        }
        if (base_ok && (base_n == 0 || base[base_n - 1] != segment_end_loc)) {
            if (!append_int(&base, &base_n, &base_cap, segment_end_loc)) {
                base_ok = 0;
            }
        }
        if (!base_ok) {
            free(base);
            fprintf(fp, "]%s\n", (s + 1 < sol->segment_count) ? "," : "");
            continue;
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

    fprintf(fp, "      \"station_count\": [");
    for (int s = 0; s < sol->segment_count; s++) {
        fprintf(fp, "%d", station_counts[s]);
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
    free(station_counts);
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
    fprintf(fp, "      \"fallback_changes\": %d,\n", snapshot->fallback_changes);
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

static void write_mip_role_entry(FILE *fp,
                                 const char *key,
                                 const char *model_name,
                                 double limit_seconds,
                                 const gsp_mip_solve_detail_t *details,
                                 int detail_count,
                                 int segment_role,
                                 int trailing_comma) {
    int count = 0;
    if (!fp) return;
    for (int i = 0; i < detail_count; i++) {
        if (details[i].segment_role == segment_role) count++;
    }
    fprintf(fp, "    \"%s\": {\n", key);
    fprintf(fp, "      \"L\": %.6f,\n", limit_seconds);
    fprintf(fp, "      \"model\": \"%s\",\n", model_name ? model_name : "unknown");
    fprintf(fp, "      \"count\": %d,\n", count);
    fprintf(fp, "      \"solve_detail_tuple\": [\"pass_index\", \"boundary_index\", \"candidate_split_index\", \"segment_index\", \"station_count\", \"node_count\", \"moved_stations\", \"mip_size\", \"runtime_seconds\", \"gap_percent\"],\n");
    fprintf(fp, "      \"values\": [");
    count = 0;
    for (int i = 0; i < detail_count; i++) {
        const gsp_mip_solve_detail_t *detail = &details[i];
        if (detail->segment_role != segment_role) continue;
        if (count) fprintf(fp, ", ");
        fprintf(fp, "[%d, %d, %d, %d, %d, %d, %d, [%d, %d], %.6f, %.6f]",
                detail->pass_index,
                detail->boundary_index,
                detail->candidate_split_index,
                detail->segment_index,
                detail->station_count,
                detail->node_count,
                detail->moved_stations,
                detail->model_num_vars,
                detail->model_num_constrs,
                detail->runtime_seconds,
                detail->gap_percent);
        count++;
    }
    fprintf(fp, "]\n");
    fprintf(fp, "    }%s\n", trailing_comma ? "," : "");
}

static void write_refinement_mip_json(FILE *fp,
                                      const sweep_config_t *cfg,
                                      const gsp_mip_solve_detail_t *details,
                                      int detail_count) {
    fprintf(fp, "  \"mip\": {\n");
    write_mip_role_entry(fp, "1seg", "endpaired_tsp",
                         cfg ? (double)cfg->l1seg_time_limit_seconds : 0.0,
                         details, detail_count, 1, 1);
    write_mip_role_entry(fp, "2seg", "fixedport_capacity",
                         cfg ? (double)cfg->l2seg_time_limit_seconds : 0.0,
                         details, detail_count, 2, 0);
    fprintf(fp, "  },\n");
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
                            int total_fallback_changes,
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
    {
        gsp_metadata_json_t metadata = {0};
        gsp_problem_json_t problem = {0};
        sweep_metadata_extra_t metadata_extra = {
            cfg ? cfg->l1seg_time_limit_seconds : 0,
            cfg ? cfg->l2seg_time_limit_seconds : 0,
            cfg ? cfg->global_time_limit_seconds : 0,
            cfg ? cfg->max_iterations : 0,
            cfg ? cfg->enable_fallback : 0
        };
        metadata.solver_version = "refinement_1.0";
        metadata.mode_name = "refinement";
        metadata.strategy_name = strategy_name ? strategy_name : "refinement";
        metadata.boat_id = boat->boat_id;
        metadata.boat_name = boat->boat_name;
        metadata.boat_lat = boat->boat_lat;
        metadata.boat_lon = boat->boat_lon;
        metadata.boat_location_id = boat->boat_loc_id;
        metadata.extra_writer = write_sweep_metadata_extra;
        metadata.extra_ctx = &metadata_extra;
        gsp_write_metadata_json(fp, "  ", &metadata, 1);

        problem.has_num_stations = 1;
        problem.num_stations = inst->num_stations;
        problem.has_capacity = 1;
        problem.capacity = boat->boat_capacity;
        gsp_write_problem_json(fp, "  ", &problem, 1);
    }

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
    write_refinement_mip_json(fp, cfg, mip_details, mip_detail_count);

    if (snapshots[snapshot_count - 1].pass_index == 0) snprintf(final_pass_name, sizeof(final_pass_name), "init");
    else snprintf(final_pass_name, sizeof(final_pass_name), "pass%d", snapshots[snapshot_count - 1].pass_index);
    fprintf(fp, "  \"summary\": {\n");
    {
        double *distance_trajectory = NULL;
        double *runtime_trajectory = NULL;
        gsp_distance_breakdown_t *final_segment_breakdowns = NULL;
        gsp_distance_breakdown_t final_total_breakdown;
        memset(&final_total_breakdown, 0, sizeof(final_total_breakdown));
        distance_trajectory = (double*)calloc((size_t)snapshot_count, sizeof(double));
        runtime_trajectory = (double*)calloc((size_t)snapshot_count, sizeof(double));
        if (distance_trajectory && runtime_trajectory) {
            for (int i = 0; i < snapshot_count; i++) {
                distance_trajectory[i] = snapshots[i].solution.total_distance;
                runtime_trajectory[i] = snapshots[i].pass_runtime_seconds;
            }
        }
        final_segment_breakdowns = (gsp_distance_breakdown_t*)calloc(
            (size_t)snapshots[snapshot_count - 1].solution.segment_count,
            sizeof(gsp_distance_breakdown_t));
        if (final_segment_breakdowns) {
            sweep_compute_segment_breakdowns(inst,
                                             &snapshots[snapshot_count - 1].solution,
                                             boat->boat_loc_id,
                                             final_segment_breakdowns,
                                             &final_total_breakdown);
        }

        gsp_write_summary_status_json(fp, "    ", final_pass_name,
                                      is_final_write ? "refinement_complete" : "refinement_running",
                                      snapshots[snapshot_count - 1].feasible,
                                      strategy_name ? strategy_name : "refinement", 1);
        gsp_write_summary_distance_json(fp, "    ", 0, 0.0,
                                        distance_trajectory, snapshot_count,
                                        snapshots[snapshot_count - 1].solution.total_distance,
                                        final_segment_breakdowns ? &final_total_breakdown : NULL,
                                        1);
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
        fprintf(fp, "      \"total_boundary_changes\": %d,\n", total_boundary_changes);
        fprintf(fp, "      \"total_fallback_changes\": %d\n", total_fallback_changes);
        fprintf(fp, "    },\n");
        gsp_write_summary_mip_json(fp, "    ", mip_detail_count,
                                   mip_runtime_mean, mip_runtime_max, mip_gap_mean, mip_gap_max, 0);
        free(final_segment_breakdowns);
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
                             const char **output, int *time_limit,
                             int *l1seg_time_limit,
                             int *l2seg_time_limit,
                             int *enable_fallback_override,
                             int *debug_mode) {
    *strategy = NULL;
    *database = NULL;
    *config = NULL;
    *input = NULL;
    *output = NULL;
    *time_limit = 0;
    *l1seg_time_limit = -1;
    *l2seg_time_limit = -1;
    *enable_fallback_override = -1;
    *debug_mode = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0) {
            *debug_mode = 1;
            continue;
        }
        if (strcmp(argv[i], "--enable-fallback") == 0) {
            *enable_fallback_override = 1;
            continue;
        }
        if (strcmp(argv[i], "--disable-fallback") == 0) {
            *enable_fallback_override = 0;
            continue;
        }
        if (i >= argc - 1) break;
        if (strcmp(argv[i], "--strategy") == 0) *strategy = argv[i + 1];
        else if (strcmp(argv[i], "--database") == 0) *database = argv[i + 1];
        else if (strcmp(argv[i], "--config") == 0) *config = argv[i + 1];
        else if (strcmp(argv[i], "--input") == 0) *input = argv[i + 1];
        else if (strcmp(argv[i], "--output") == 0) *output = argv[i + 1];
        else if (strcmp(argv[i], "--time-limit") == 0) *time_limit = atoi(argv[i + 1]);
        else if (strcmp(argv[i], "--l1seg-limit") == 0) *l1seg_time_limit = atoi(argv[i + 1]);
        else if (strcmp(argv[i], "--l2seg-limit") == 0) *l2seg_time_limit = atoi(argv[i + 1]);
    }
}

int mode_refinement(int argc, char **argv) {
    const char *strategy = NULL, *database = NULL, *config = NULL, *input = NULL, *output = NULL;
    int time_limit = 0, l1seg_time_limit = -1, l2seg_time_limit = -1;
    int enable_fallback_override = -1, debug_mode = 0;
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
    int total_fallback_changes = 0;
    int total_mip_solves = 0;
    int rc = 1;
    struct timespec t_start, t_pass_start, t_pass_end, t_now, t_preproc_end;
    double preprocessing_seconds = 0.0;

    printf("============================================================\n");
    printf("GSP Solver - Refinement\n");
    printf("============================================================\n\n");
    parse_sweep_args(argc, argv, &strategy, &database, &config, &input, &output,
                     &time_limit, &l1seg_time_limit, &l2seg_time_limit,
                     &enable_fallback_override, &debug_mode);
    if (!strategy || !database || !config || !input || !output) {
        fprintf(stderr, "ERROR: sweep requires --strategy, --database, --config, --input, and --output\n");
        goto cleanup;
    }
    read_sweep_config_from_yaml(config, &sweep_cfg);
    if (l1seg_time_limit >= 0) {
        sweep_cfg.l1seg_time_limit_seconds = l1seg_time_limit;
    }
    if (l2seg_time_limit >= 0) {
        sweep_cfg.l2seg_time_limit_seconds = l2seg_time_limit;
    }
    if (enable_fallback_override >= 0) {
        sweep_cfg.enable_fallback = enable_fallback_override;
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
    if (!load_segments_from_json(input, &inst, &boat, &segments, &segment_count,
                                 &current_solution, &input_total_distance_nm)) {
        fprintf(stderr, "ERROR: Failed to load segmented solution from %s\n", input);
        goto cleanup;
    }
    printf("Loaded segmented input: %d segments, %.2f nm total\n",
           segment_count, input_total_distance_nm);
    printf("Sweep parameters: l1seg_time_limit=%d l2seg_time_limit=%d max_iterations=%d global_time_limit=%d time_limit=%d fallback_enabled=%s\n",
           sweep_cfg.l1seg_time_limit_seconds,
           sweep_cfg.l2seg_time_limit_seconds,
           sweep_cfg.max_iterations, sweep_cfg.global_time_limit_seconds, time_limit,
           sweep_cfg.enable_fallback ? "true" : "false");
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
                                      total_fallback_changes,
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
            int fallback_changes = 0;
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
                double boundary_l1_time_limit_seconds = (double)sweep_cfg.l1seg_time_limit_seconds;
                double boundary_l2_time_limit_seconds = (double)sweep_cfg.l2seg_time_limit_seconds;
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
                    if (boundary_l1_time_limit_seconds <= 0.0 || remaining_seconds < boundary_l1_time_limit_seconds) {
                        boundary_l1_time_limit_seconds = remaining_seconds;
                    }
                    if (boundary_l2_time_limit_seconds <= 0.0 || remaining_seconds < boundary_l2_time_limit_seconds) {
                        boundary_l2_time_limit_seconds = remaining_seconds;
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
                {
                    int changed_donor_idx = -1;
                    double changed_donor_before_distance = 0.0;
                    if (optimize_boundary(env, &inst, &boat, segments, segment_count,
                                      b, right_idx,
                                      &pass_mip_solve_count,
                                      &pass_mip_solve_details,
                                      &pass_mip_detail_count,
                                      &pass_mip_detail_capacity,
                                      b + 1,
                                      boundary_loc_id,
                                      pass_index,
                                      boundary_l1_time_limit_seconds,
                                      boundary_l2_time_limit_seconds,
                                      sweep_cfg.enable_fallback,
                                      &changed_donor_idx,
                                      &changed_donor_before_distance)) {
                    int station_changes = count_segment_station_changes(&left_before, &segments[b]) +
                                          count_segment_station_changes(&right_before, &segments[right_idx]);
                    double after_total = segments[b].distance_nm + segments[right_idx].distance_nm;
                    double improvement_nm = before_total - after_total;
                    if (changed_donor_idx >= 0) {
                        improvement_nm += changed_donor_before_distance - segments[changed_donor_idx].distance_nm;
                    }
                    boundary_gain_nm[b] = improvement_nm;
                    changed = 1;
                    boundary_changes++;
                    total_boundary_changes++;
                    if (changed_donor_idx >= 0) {
                        fallback_changes++;
                        total_fallback_changes++;
                    }
                    if (segment_count > 1) active[(b - 1 + segment_count) % segment_count] = 1;
                    active[b] = 1;
                    if (segment_count > 1) active[right_idx] = 1;
                    if (changed_donor_idx >= 0 && segment_count > 1) {
                        active[(changed_donor_idx - 1 + segment_count) % segment_count] = 1;
                        active[changed_donor_idx] = 1;
                    }
                    printf("    improved: %.2f -> %.2f nm, gain=%.2f nm, moved_station_marks=%d\n",
                           before_total, after_total, improvement_nm, station_changes);
                    } else {
                    printf("    no change\n");
                    }
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
            snapshot.fallback_changes = fallback_changes;
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
                                      total_fallback_changes,
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
                              total_fallback_changes,
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
