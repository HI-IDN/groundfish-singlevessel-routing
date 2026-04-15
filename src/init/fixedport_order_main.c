#include "../include/dat_parser.h"
#include "../include/json_utils.h"
#include "../mip/include/mip_fixedport.h"
#include "segment_postopt.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    Boat boat;
    Port *ports;
    int n_ports;
    Station *stations;
    int n_stations;
    double **distances;
    int max_location_id;
    double boat_start_lat;
    double boat_start_lon;
} app_instance_t;

typedef struct {
    int *data;
    int count;
    int capacity;
} int_vec_t;

typedef struct {
    int *values;
    int count;
    int capacity;
} segment_int_vec_t;

typedef struct {
    segment_int_vec_t locations;
    segment_int_vec_t station_ids;
    int catch_amount;
    gsp_distance_breakdown_t distance;
    int end_dock_location_id;
} route_segment_t;

typedef struct {
    int location_id;
    int port_id;
    char *name;
} port_info_t;

typedef struct {
    const char *station_selection;
} fixedport_metadata_extra_t;

typedef struct {
    int num_fixed_port_visits;
    const int *candidate_port_location_ids;
} fixedport_problem_extra_t;

static void write_fixedport_metadata_extra(FILE *fp, const char *indent, const void *ctx) {
    const fixedport_metadata_extra_t *extra = (const fixedport_metadata_extra_t*)ctx;
    const char *base = indent ? indent : "";
    if (!fp || !extra) return;
    fprintf(fp, "%s  \"station_selection\": \"%s\"", base,
            extra->station_selection ? extra->station_selection : "all");
}

static void write_fixedport_problem_extra(FILE *fp, const char *indent, const void *ctx) {
    const fixedport_problem_extra_t *extra = (const fixedport_problem_extra_t*)ctx;
    const char *base = indent ? indent : "";
    if (!fp || !extra) return;
    fprintf(fp, "%s  \"num_fixed_port_visits\": %d,\n", base, extra->num_fixed_port_visits);
    fprintf(fp, "%s  \"candidate_port_location_ids\": [", base);
    for (int i = 0; i < extra->num_fixed_port_visits; i++) {
        if (i) fprintf(fp, ", ");
        fprintf(fp, "%d", extra->candidate_port_location_ids[i]);
    }
    fprintf(fp, "]");
}

static char *dupstr_local(const char *src) {
    size_t len;
    char *copy;
    if (!src) return NULL;
    len = strlen(src);
    copy = (char*)malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, src, len + 1);
    return copy;
}

static double elapsed_seconds(clock_t start_clock, clock_t end_clock) {
    return (double)(end_clock - start_clock) / (double)CLOCKS_PER_SEC;
}

static void int_vec_init(int_vec_t *vec) {
    vec->data = NULL;
    vec->count = 0;
    vec->capacity = 0;
}

static void int_vec_free(int_vec_t *vec) {
    free(vec->data);
    memset(vec, 0, sizeof(*vec));
}

static int int_vec_push(int_vec_t *vec, int value) {
    int *tmp;
    int new_capacity;
    if (vec->count >= vec->capacity) {
        new_capacity = vec->capacity > 0 ? vec->capacity * 2 : 16;
        tmp = (int*)realloc(vec->data, (size_t)new_capacity * sizeof(int));
        if (!tmp) return 0;
        vec->data = tmp;
        vec->capacity = new_capacity;
    }
    vec->data[vec->count++] = value;
    return 1;
}

static int int_vec_push_unique(int_vec_t *vec, int value) {
    for (int i = 0; i < vec->count; i++) if (vec->data[i] == value) return 1;
    return int_vec_push(vec, value);
}

static void segment_int_vec_init(segment_int_vec_t *vec) {
    vec->values = NULL;
    vec->count = 0;
    vec->capacity = 0;
}

static void segment_int_vec_free(segment_int_vec_t *vec) {
    free(vec->values);
    memset(vec, 0, sizeof(*vec));
}

static int segment_int_vec_push(segment_int_vec_t *vec, int value) {
    int *tmp;
    int new_capacity;
    if (vec->count >= vec->capacity) {
        new_capacity = vec->capacity > 0 ? vec->capacity * 2 : 16;
        tmp = (int*)realloc(vec->values, (size_t)new_capacity * sizeof(int));
        if (!tmp) return 0;
        vec->values = tmp;
        vec->capacity = new_capacity;
    }
    vec->values[vec->count++] = value;
    return 1;
}

static int segment_int_vec_push_if_changed(segment_int_vec_t *vec, int value) {
    if (vec->count > 0 && vec->values[vec->count - 1] == value) return 1;
    return segment_int_vec_push(vec, value);
}

static void route_segment_init(route_segment_t *segment) {
    memset(segment, 0, sizeof(*segment));
    segment_int_vec_init(&segment->locations);
    segment_int_vec_init(&segment->station_ids);
}

static void route_segment_free(route_segment_t *segment) {
    segment_int_vec_free(&segment->locations);
    segment_int_vec_free(&segment->station_ids);
    memset(segment, 0, sizeof(*segment));
}

static void free_port_info_array(port_info_t *ports, int count) {
    if (!ports) return;
    for (int i = 0; i < count; i++) free(ports[i].name);
    free(ports);
}

static void free_app_instance(app_instance_t *app) {
    if (!app) return;
    free(app->boat.name);
    for (int i = 0; i < app->n_ports; i++) free(app->ports[i].name);
    for (int i = 0; i < app->n_stations; i++) free(app->stations[i].comment);
    free(app->ports);
    free(app->stations);
    if (app->distances) {
        for (int i = 0; i < app->max_location_id; i++) free(app->distances[i]);
    }
    free(app->distances);
    memset(app, 0, sizeof(*app));
}

static char *trim_left(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static void trim_right(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' || s[len - 1] == ' ' || s[len - 1] == '\t')) {
        s[--len] = '\0';
    }
}

static int read_fixedport_config(const char *yaml_path,
                                 int *boat_id_out,
                                 double *xseg_out,
                                 double *global_time_limit_out,
                                 int *thread_count_out,
                                 double *haul_distance_scale_out) {
    FILE *fp = NULL;
    char line[1024];
    int section = 0;

    if (boat_id_out) *boat_id_out = 2;
    if (xseg_out) *xseg_out = 0.0;
    if (global_time_limit_out) *global_time_limit_out = 0.0;
    if (thread_count_out) *thread_count_out = 0;
    if (haul_distance_scale_out) *haul_distance_scale_out = 0.0;

    fp = fopen(yaml_path, "r");
    if (!fp) return 0;

    while (fgets(line, sizeof(line), fp)) {
        char *hash = strchr(line, '#');
        char *trimmed;
        if (hash) *hash = '\0';
        trim_right(line);
        trimmed = trim_left(line);
        if (*trimmed == '\0') continue;

        if (trimmed == line && strncmp(trimmed, "global_time_limit_seconds:", 26) == 0 && global_time_limit_out) {
            *global_time_limit_out = atof(trimmed + 26);
            section = 0;
            continue;
        }

        if (trimmed == line) {
            if (strncmp(trimmed, "boat:", 5) == 0) section = 1;
            else if (strncmp(trimmed, "gurobi:", 7) == 0) section = 2;
            else section = 0;
            continue;
        }

        if (section == 1 && strncmp(trimmed, "id:", 3) == 0 && boat_id_out) {
            *boat_id_out = atoi(trimmed + 3);
        } else if (section == 2 && strncmp(trimmed, "threads:", 8) == 0 && thread_count_out) {
            *thread_count_out = atoi(trimmed + 8);
        }
    }

    fclose(fp);
    if (xseg_out) *xseg_out = read_fixedport_mip_time_limit_from_yaml(yaml_path);
    if (haul_distance_scale_out) *haul_distance_scale_out = read_fixedport_haul_distance_scale_from_yaml(yaml_path);
    return 0;
}

static int load_boat(sqlite3 *db, int boat_id, app_instance_t *app) {
    sqlite3_stmt *stmt = NULL;
    const char *sql =
        "SELECT b.id, b.name, b.capacity, b.location_id, l.lat, l.lon "
        "FROM boats b "
        "JOIN locations l ON l.id = b.location_id "
        "WHERE b.id = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 1;
    sqlite3_bind_int(stmt, 1, boat_id);
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return 1;
    }
    app->boat.boat_id = sqlite3_column_int(stmt, 0);
    app->boat.name = dupstr_local((const char*)sqlite3_column_text(stmt, 1));
    app->boat.capacity = sqlite3_column_int(stmt, 2);
    app->boat.location_id = sqlite3_column_int(stmt, 3);
    app->boat_start_lat = sqlite3_column_double(stmt, 4);
    app->boat_start_lon = sqlite3_column_double(stmt, 5);
    sqlite3_finalize(stmt);
    return 0;
}

static int load_ports(sqlite3 *db, app_instance_t *app) {
    sqlite3_stmt *stmt = NULL;
    int count = 0;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM ports;", -1, &stmt, NULL) != SQLITE_OK) return 1;
    if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    app->ports = (Port*)calloc((size_t)count, sizeof(Port));
    if (count > 0 && !app->ports) return 1;
    app->n_ports = count;
    if (sqlite3_prepare_v2(db, "SELECT id, name, location_id FROM ports ORDER BY id;", -1, &stmt, NULL) != SQLITE_OK) return 1;
    for (int i = 0; i < count; i++) {
        if (sqlite3_step(stmt) != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            return 1;
        }
        app->ports[i].port_id = sqlite3_column_int(stmt, 0);
        app->ports[i].name = dupstr_local((const char*)sqlite3_column_text(stmt, 1));
        app->ports[i].location_id = sqlite3_column_int(stmt, 2);
    }
    sqlite3_finalize(stmt);
    return 0;
}

static int load_stations(sqlite3 *db, app_instance_t *app) {
    sqlite3_stmt *stmt = NULL;
    int count = 0;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM stations;", -1, &stmt, NULL) != SQLITE_OK) return 1;
    if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    app->stations = (Station*)calloc((size_t)count, sizeof(Station));
    if (count > 0 && !app->stations) return 1;
    app->n_stations = count;
    if (sqlite3_prepare_v2(db,
            "SELECT id, amount, comment, ext_id, start_location_id, end_location_id FROM stations ORDER BY id;",
            -1, &stmt, NULL) != SQLITE_OK) return 1;
    for (int i = 0; i < count; i++) {
        if (sqlite3_step(stmt) != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            return 1;
        }
        app->stations[i].station_id = sqlite3_column_int(stmt, 0);
        app->stations[i].amount = sqlite3_column_int(stmt, 1);
        app->stations[i].comment = dupstr_local((const char*)sqlite3_column_text(stmt, 2));
        app->stations[i].external_id = sqlite3_column_int(stmt, 3);
        app->stations[i].start_location_id = sqlite3_column_int(stmt, 4);
        app->stations[i].end_location_id = sqlite3_column_int(stmt, 5);
    }
    sqlite3_finalize(stmt);
    return 0;
}

static int load_distances(sqlite3 *db, app_instance_t *app) {
    sqlite3_stmt *stmt = NULL;
    int max_id = 0;
    if (sqlite3_prepare_v2(db, "SELECT MAX(id) FROM locations;", -1, &stmt, NULL) != SQLITE_OK) return 1;
    if (sqlite3_step(stmt) == SQLITE_ROW) max_id = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    app->max_location_id = max_id + 1;
    app->distances = (double**)calloc((size_t)app->max_location_id, sizeof(double*));
    if (!app->distances) return 1;
    for (int i = 0; i < app->max_location_id; i++) {
        app->distances[i] = (double*)malloc((size_t)app->max_location_id * sizeof(double));
        if (!app->distances[i]) return 1;
        for (int j = 0; j < app->max_location_id; j++) app->distances[i][j] = -1.0;
    }
    if (sqlite3_prepare_v2(db, "SELECT from_location_id, to_location_id, distance_nm FROM distances;", -1, &stmt, NULL) != SQLITE_OK) return 1;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int from = sqlite3_column_int(stmt, 0);
        int to = sqlite3_column_int(stmt, 1);
        double value = sqlite3_column_double(stmt, 2);
        if (from >= 0 && from < app->max_location_id && to >= 0 && to < app->max_location_id) {
            app->distances[from][to] = value;
        }
    }
    sqlite3_finalize(stmt);
    return 0;
}

static char *read_text_file(const char *path) {
    FILE *fp;
    long size;
    char *buf;
    if (!path) return NULL;
    fp = fopen(path, "rb");
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
    while (p && *p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    return p;
}

static const char *find_json_key(const char *text, const char *key) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    return strstr(text, pattern);
}

static const char *find_matching_bracket(const char *p) {
    int depth = 0;
    if (!p || *p != '[') return NULL;
    while (*p) {
        if (*p == '[') depth++;
        else if (*p == ']') {
            depth--;
            if (depth == 0) return p;
        }
        p++;
    }
    return NULL;
}

static int parse_int_array_after_key(const char *json, const char *key, int **out_values, int *out_count) {
    const char *p;
    const char *start;
    const char *end;
    int *values = NULL;
    int count = 0;
    int capacity = 0;

    *out_values = NULL;
    *out_count = 0;
    p = find_json_key(json, key);
    if (!p) return 0;
    p = strchr(p, ':');
    if (!p) return 0;
    p = skip_ws(p + 1);
    if (!p || *p != '[') return 0;
    start = p;
    end = find_matching_bracket(start);
    if (!end) return 0;
    p = start + 1;
    while (p < end) {
        char *next;
        long value;
        p = skip_ws(p);
        if (!p || p >= end) break;
        if (*p == ',') {
            p++;
            continue;
        }
        value = strtol(p, &next, 10);
        if (next == p) {
            p++;
            continue;
        }
        if (count >= capacity) {
            int new_capacity = capacity > 0 ? capacity * 2 : 16;
            int *tmp = (int*)realloc(values, (size_t)new_capacity * sizeof(int));
            if (!tmp) {
                free(values);
                return 0;
            }
            values = tmp;
            capacity = new_capacity;
        }
        values[count++] = (int)value;
        p = next;
    }
    *out_values = values;
    *out_count = count;
    return 1;
}

static int load_candidate_port_location_ids(const char *path, int **out_values, int *out_count) {
    char *json = read_text_file(path);
    int ok = 0;
    if (!json) return 0;
    ok = parse_int_array_after_key(json, "ordered_port_location_ids", out_values, out_count);
    free(json);
    return ok;
}

static void remove_one_candidate_port(int *values, int *count, int location_id) {
    if (!values || !count || *count <= 0) return;
    for (int i = *count - 1; i >= 0; i--) {
        if (values[i] != location_id) continue;
        for (int j = i; j + 1 < *count; j++) values[j] = values[j + 1];
        (*count)--;
        return;
    }
}

static int load_port_lookup(sqlite3 *db, port_info_t **out_ports, int *out_count) {
    sqlite3_stmt *stmt = NULL;
    port_info_t *ports = NULL;
    int count = 0;
    int capacity = 0;
    *out_ports = NULL;
    *out_count = 0;
    if (sqlite3_prepare_v2(db, "SELECT location_id, id, name FROM ports ORDER BY location_id;", -1, &stmt, NULL) != SQLITE_OK) return 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        port_info_t *tmp;
        int new_capacity;
        if (count >= capacity) {
            new_capacity = capacity > 0 ? capacity * 2 : 16;
            tmp = (port_info_t*)realloc(ports, (size_t)new_capacity * sizeof(port_info_t));
            if (!tmp) {
                sqlite3_finalize(stmt);
                free_port_info_array(ports, count);
                return 0;
            }
            ports = tmp;
            capacity = new_capacity;
        }
        ports[count].location_id = sqlite3_column_int(stmt, 0);
        ports[count].port_id = sqlite3_column_int(stmt, 1);
        ports[count].name = dupstr_local((const char*)sqlite3_column_text(stmt, 2));
        if (!ports[count].name) {
            sqlite3_finalize(stmt);
            free_port_info_array(ports, count);
            return 0;
        }
        count++;
    }
    sqlite3_finalize(stmt);
    *out_ports = ports;
    *out_count = count;
    return 1;
}

static const port_info_t *find_port_info(const port_info_t *ports, int count, int location_id) {
    for (int i = 0; i < count; i++) if (ports[i].location_id == location_id) return &ports[i];
    return NULL;
}

static int use_all_stations(const app_instance_t *app,
                            const Station **out_stations,
                            int *out_count) {
    if (!app || !out_stations || !out_count) return 0;
    *out_stations = app->stations;
    *out_count = app->n_stations;
    return app->n_stations > 0;
}

static int parse_waypoint_path_json(const char *json_text, int **out_ids) {
    int *ids = NULL;
    int count = 0;
    const char *cursor = json_text;
    *out_ids = NULL;
    if (!json_text) return 0;
    while (*cursor) {
        char *endptr = NULL;
        long value;
        while (*cursor && !((*cursor >= '0' && *cursor <= '9') || *cursor == '-')) cursor++;
        if (!*cursor) break;
        value = strtol(cursor, &endptr, 10);
        if (endptr == cursor) break;
        {
            int *tmp = (int*)realloc(ids, (size_t)(count + 1) * sizeof(int));
            if (!tmp) {
                free(ids);
                return 0;
            }
            ids = tmp;
            ids[count++] = (int)value;
        }
        cursor = endptr;
    }
    *out_ids = ids;
    return count;
}

static int lookup_waypoint_path(sqlite3 *db, int from_loc_id, int to_loc_id, int **out_ids) {
    sqlite3_stmt *stmt = NULL;
    static const char *sql = "SELECT waypoint_path FROM distances WHERE from_location_id = ? AND to_location_id = ?;";
    int count = 0;
    *out_ids = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, from_loc_id);
        sqlite3_bind_int(stmt, 2, to_loc_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *txt = sqlite3_column_text(stmt, 0);
            if (txt) count = parse_waypoint_path_json((const char*)txt, out_ids);
        }
        sqlite3_finalize(stmt);
    }
    return count;
}

static double distance_nm(const app_instance_t *app, int from_loc_id, int to_loc_id) {
    if (from_loc_id < 0 || from_loc_id >= app->max_location_id) return -1.0;
    if (to_loc_id < 0 || to_loc_id >= app->max_location_id) return -1.0;
    if (from_loc_id == to_loc_id) return 0.0;
    return app->distances[from_loc_id][to_loc_id];
}

static int append_leg_to_segment(sqlite3 *db,
                                 route_segment_t *segment,
                                 int from_loc_id,
                                 int to_loc_id,
                                 int_vec_t *unique_waypoints) {
    int *waypoints = NULL;
    int waypoint_count = 0;
    if (!segment_int_vec_push_if_changed(&segment->locations, from_loc_id)) return 0;
    waypoint_count = lookup_waypoint_path(db, from_loc_id, to_loc_id, &waypoints);
    for (int i = 0; i < waypoint_count; i++) {
        if (!segment_int_vec_push_if_changed(&segment->locations, waypoints[i]) ||
            !int_vec_push_unique(unique_waypoints, waypoints[i])) {
            free(waypoints);
            return 0;
        }
    }
    free(waypoints);
    return segment_int_vec_push_if_changed(&segment->locations, to_loc_id);
}

static int write_fixedport_json(sqlite3 *db,
                                const char *output_path,
                                const app_instance_t *app,
                                const Station *stations,
                                int station_count,
                                const int *candidate_ports,
                                int candidate_port_count,
                                const port_info_t *port_lookup,
                                int port_lookup_count,
                                const mip_fixedport_solution_t *solution,
                                double preprocessing_seconds,
                                double total_runtime_seconds) {
    route_segment_t *segments = NULL;
    gsp_int_list_view_t *location_views = NULL;
    gsp_int_list_view_t *station_views = NULL;
    gsp_distance_breakdown_t *segment_breakdowns = NULL;
    int *segment_catches = NULL;
    int *segment_end_docks = NULL;
    int *tour_lengths = NULL;
    int *dock_location_ids = NULL;
    int *port_visit_counts = NULL;
    int dock_location_count = 0;
    int_vec_t unique_waypoints;
    gsp_distance_breakdown_t grand_total = {0.0, 0.0, 0.0};
    gsp_solution_json_view_t solution_view;
    gsp_summary_json_t summary;
    FILE *fp = NULL;
    int feasible = 1;
    int segment_count = candidate_port_count + 1;
    int current_segment = 0;
    int prev_loc_id = app->boat.location_id;

    memset(&solution_view, 0, sizeof(solution_view));
    memset(&summary, 0, sizeof(summary));
    int_vec_init(&unique_waypoints);

    segments = (route_segment_t*)calloc((size_t)segment_count, sizeof(route_segment_t));
    location_views = (gsp_int_list_view_t*)calloc((size_t)segment_count, sizeof(gsp_int_list_view_t));
    station_views = (gsp_int_list_view_t*)calloc((size_t)segment_count, sizeof(gsp_int_list_view_t));
    segment_breakdowns = (gsp_distance_breakdown_t*)calloc((size_t)segment_count, sizeof(gsp_distance_breakdown_t));
    segment_catches = (int*)calloc((size_t)segment_count, sizeof(int));
    segment_end_docks = (int*)calloc((size_t)segment_count, sizeof(int));
    tour_lengths = (int*)calloc((size_t)segment_count, sizeof(int));
    port_visit_counts = (int*)calloc((size_t)port_lookup_count, sizeof(int));
    if (!segments || !location_views || !station_views || !segment_breakdowns || !segment_catches || !segment_end_docks || !tour_lengths || !port_visit_counts) goto fail;

    for (int s = 0; s < segment_count; s++) route_segment_init(&segments[s]);
    if (!segment_int_vec_push_if_changed(&segments[0].locations, app->boat.location_id)) goto fail;

    for (int i = 0; i < solution->visit_count; i++) {
        int signed_visit_id = solution->signed_visit_ids[i];
        int visit_id = abs(signed_visit_id);
        if (visit_id <= station_count) {
            const Station *station = &stations[visit_id - 1];
            int entry_loc_id = signed_visit_id < 0 ? station->end_location_id : station->start_location_id;
            int exit_loc_id = signed_visit_id < 0 ? station->start_location_id : station->end_location_id;
            double transit = distance_nm(app, prev_loc_id, entry_loc_id);
            double haul = distance_nm(app, entry_loc_id, exit_loc_id);
            if (transit < 0.0 || haul < 0.0) goto fail;
            if (!append_leg_to_segment(db, &segments[current_segment], prev_loc_id, entry_loc_id, &unique_waypoints) ||
                !append_leg_to_segment(db, &segments[current_segment], entry_loc_id, exit_loc_id, &unique_waypoints) ||
                !segment_int_vec_push(&segments[current_segment].station_ids, station->station_id)) goto fail;
            segments[current_segment].distance.transit_distance_nm += transit;
            segments[current_segment].distance.haul_distance_nm += haul;
            segments[current_segment].catch_amount += station->amount;
            prev_loc_id = exit_loc_id;
        } else {
            int port_index = visit_id - station_count - 1;
            int port_loc_id;
            double transit;
            if (port_index < 0 || port_index >= candidate_port_count) goto fail;
            port_loc_id = candidate_ports[port_index];
            transit = distance_nm(app, prev_loc_id, port_loc_id);
            if (transit < 0.0) goto fail;
            if (!append_leg_to_segment(db, &segments[current_segment], prev_loc_id, port_loc_id, &unique_waypoints)) goto fail;
            segments[current_segment].distance.transit_distance_nm += transit;
            segments[current_segment].end_dock_location_id = port_loc_id;
            prev_loc_id = port_loc_id;
            current_segment++;
            if (current_segment >= segment_count) goto fail;
            if (!segment_int_vec_push_if_changed(&segments[current_segment].locations, port_loc_id)) goto fail;
        }
    }

    {
        double transit = distance_nm(app, prev_loc_id, app->boat.location_id);
        if (transit < 0.0) goto fail;
        if (!append_leg_to_segment(db, &segments[current_segment], prev_loc_id, app->boat.location_id, &unique_waypoints)) goto fail;
        segments[current_segment].distance.transit_distance_nm += transit;
        segments[current_segment].end_dock_location_id = app->boat.location_id;
    }

    for (int s = 0; s < segment_count; s++) {
        segments[s].distance.total_distance_nm =
            segments[s].distance.transit_distance_nm + segments[s].distance.haul_distance_nm;
        segment_breakdowns[s] = segments[s].distance;
        segment_catches[s] = segments[s].catch_amount;
        segment_end_docks[s] = segments[s].end_dock_location_id;
        tour_lengths[s] = segments[s].station_ids.count + 2;
        location_views[s].values = segments[s].locations.values;
        location_views[s].count = segments[s].locations.count;
        station_views[s].values = segments[s].station_ids.values;
        station_views[s].count = segments[s].station_ids.count;
        if (segments[s].catch_amount > app->boat.capacity) feasible = 0;
        grand_total.transit_distance_nm += segments[s].distance.transit_distance_nm;
        grand_total.haul_distance_nm += segments[s].distance.haul_distance_nm;
        grand_total.total_distance_nm += segments[s].distance.total_distance_nm;
    }

    if (!gsp_build_dock_location_ids_from_segment_ends(app->boat.location_id,
                                                       segment_end_docks,
                                                       segment_count,
                                                       &dock_location_ids,
                                                       &dock_location_count)) goto fail;

    fp = fopen(output_path, "w");
    if (!fp) goto fail;
    fprintf(fp, "{\n");
    {
        gsp_metadata_json_t metadata = {0};
        gsp_problem_json_t problem = {0};
        fixedport_metadata_extra_t metadata_extra = {"all"};
        fixedport_problem_extra_t problem_extra = {
            candidate_port_count,
            candidate_ports
        };
        metadata.solver_version = "construction_fixedport_0.1";
        metadata.mode_name = "construction";
        metadata.strategy_name = "fixedport";
        metadata.boat_id = app->boat.boat_id;
        metadata.boat_name = app->boat.name;
        metadata.boat_lat = app->boat_start_lat;
        metadata.boat_lon = app->boat_start_lon;
        metadata.boat_location_id = app->boat.location_id;
        metadata.extra_writer = write_fixedport_metadata_extra;
        metadata.extra_ctx = &metadata_extra;
        gsp_write_metadata_json(fp, "  ", &metadata, 1);

        problem.has_num_stations = 1;
        problem.num_stations = station_count;
        problem.has_capacity = 1;
        problem.capacity = app->boat.capacity;
        problem.extra_writer = write_fixedport_problem_extra;
        problem.extra_ctx = &problem_extra;
        gsp_write_problem_json(fp, "  ", &problem, 1);
    }
    for (int i = 0; i < candidate_port_count; i++) {
        for (int j = 0; j < port_lookup_count; j++) {
            if (port_lookup[j].location_id == candidate_ports[i]) {
                port_visit_counts[j]++;
                break;
            }
        }
    }
    fprintf(fp, "  \"port_visit_summary\": [\n");
    {
        int emitted = 0;
        for (int i = 0; i < port_lookup_count; i++) {
            if (port_visit_counts[i] <= 0) continue;
            if (emitted > 0) fprintf(fp, ",\n");
            fprintf(fp, "    {\"location_id\": %d, \"port_id\": %d, \"name\": \"%s\", \"visit_count\": %d}",
                    port_lookup[i].location_id,
                    port_lookup[i].port_id,
                    port_lookup[i].name ? port_lookup[i].name : "",
                    port_visit_counts[i]);
            emitted++;
        }
        if (emitted > 0) fprintf(fp, "\n");
    }
    fprintf(fp, "  ],\n");
    fprintf(fp, "  \"solution\": {\n");
    fprintf(fp, "    \"fixedport-capacity-feasible\": ");
    solution_view.variant_name = "fixedport-capacity-feasible";
    solution_view.tour_segments_location_ids = location_views;
    solution_view.tour_segments_location_count = segment_count;
    solution_view.dock_location_ids = dock_location_ids;
    solution_view.dock_location_count = dock_location_count;
    solution_view.unique_waypoint_location_ids = unique_waypoints.data;
    solution_view.unique_waypoint_location_count = unique_waypoints.count;
    solution_view.tour_segments_station_ids = station_views;
    solution_view.tour_segments_station_count = segment_count;
    solution_view.tour_length = tour_lengths;
    solution_view.tour_length_count = segment_count;
    solution_view.segment_count = segment_count;
    solution_view.segment_catch_amount = segment_catches;
    solution_view.segment_catch_count = segment_count;
    solution_view.segment_breakdowns = segment_breakdowns;
    solution_view.grand_total = &grand_total;
    solution_view.feasible = feasible;
    gsp_write_solution_json(fp, "", &solution_view, 0);
    fprintf(fp, "  },\n");
    gsp_summary_reset(&summary);
    gsp_summary_set_status_and_distance(&summary,
                                        "fixedport-capacity-feasible",
                                        "construction_complete",
                                        feasible,
                                        "fixedport",
                                        0,
                                        0.0,
                                        &grand_total.total_distance_nm,
                                        1,
                                        grand_total.total_distance_nm);
    gsp_summary_set_runtime(&summary,
                            preprocessing_seconds,
                            &solution->runtime_seconds,
                            1,
                            0.0,
                            total_runtime_seconds);
    gsp_summary_set_mip(&summary,
                        1,
                        solution->runtime_seconds,
                        solution->runtime_seconds,
                        solution->gap * 100.0,
                        solution->gap * 100.0);
    gsp_write_summary_json(fp, "  ", &summary, 0);
    fprintf(fp, ",\n");
    {
        gsp_solver_stats_json_t solver_stats = {0};
        solver_stats.status_name =
            (solution->status == MIP_STATUS_OPTIMAL) ? "optimal" :
            (solution->status == MIP_STATUS_TIME_LIMIT) ? "time_limit" :
            (solution->status == MIP_STATUS_SUBOPTIMAL) ? "suboptimal" : "failed";
        solver_stats.gurobi_status_name = mip_gurobi_status_name(solution->status);
        solver_stats.gurobi_status_code = solution->status;
        solver_stats.include_preprocessing_seconds = 0;
        solver_stats.runtime_seconds = solution->runtime_seconds;
        solver_stats.total_runtime_seconds = total_runtime_seconds;
        solver_stats.method_name = "fixedport";
        solver_stats.mip_gap = solution->gap;
        gsp_write_solver_stats_json(fp, "  ", &solver_stats, 0);
    }
    fprintf(fp, "}\n");
    fclose(fp);

    for (int s = 0; s < segment_count; s++) route_segment_free(&segments[s]);
    free(segments);
    free(location_views);
    free(station_views);
    free(segment_breakdowns);
    free(segment_catches);
    free(segment_end_docks);
    free(tour_lengths);
    free(dock_location_ids);
    free(port_visit_counts);
    int_vec_free(&unique_waypoints);
    return 1;

fail:
    if (fp) fclose(fp);
    if (segments) for (int s = 0; s < segment_count; s++) route_segment_free(&segments[s]);
    free(segments);
    free(location_views);
    free(station_views);
    free(segment_breakdowns);
    free(segment_catches);
    free(segment_end_docks);
    free(tour_lengths);
    free(dock_location_ids);
    free(port_visit_counts);
    int_vec_free(&unique_waypoints);
    return 0;
}

int main(int argc, char **argv) {
    const char *db_path = NULL;
    const char *yaml_path = NULL;
    const char *candidate_path = NULL;
    const char *output_path = NULL;
    sqlite3 *db = NULL;
    app_instance_t app;
    port_info_t *port_lookup = NULL;
    int port_lookup_count = 0;
    int *candidate_ports = NULL;
    int candidate_port_count = 0;
    const Station *fixedport_stations = NULL;
    int fixedport_station_count = 0;
    mip_fixedport_instance_t mip_instance;
    mip_fixedport_solution_t mip_solution;
    mip_params_t mip_params;
    int boat_id = 2;
    double time_limit_seconds = 0.0;
    double global_time_limit_seconds = 0.0;
    int thread_count = 0;
    double haul_distance_scale = 0.0;
    clock_t t_start;
    clock_t t_after_load;
    clock_t t_done;
    int ok = 0;

    memset(&app, 0, sizeof(app));
    memset(&mip_instance, 0, sizeof(mip_instance));
    memset(&mip_solution, 0, sizeof(mip_solution));
    memset(&mip_params, 0, sizeof(mip_params));

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--database") == 0 && i + 1 < argc) db_path = argv[++i];
        else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) yaml_path = argv[++i];
        else if (strcmp(argv[i], "--candidates") == 0 && i + 1 < argc) candidate_path = argv[++i];
        else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) output_path = argv[++i];
    }
    if (!db_path || !yaml_path || !candidate_path || !output_path) {
        fprintf(stderr, "Usage: %s --database <db> --config <yaml> --candidates <candidate_ports.json> --output <fixedport.json>\n", argv[0]);
        return 1;
    }

    t_start = clock();
    read_fixedport_config(yaml_path, &boat_id, &time_limit_seconds, &global_time_limit_seconds,
                          &thread_count, &haul_distance_scale);
    (void)global_time_limit_seconds;

    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }
    if (load_boat(db, boat_id, &app) != 0 ||
        load_ports(db, &app) != 0 ||
        load_stations(db, &app) != 0 ||
        load_distances(db, &app) != 0 ||
        !load_port_lookup(db, &port_lookup, &port_lookup_count) ||
        !load_candidate_port_location_ids(candidate_path, &candidate_ports, &candidate_port_count) ||
        !use_all_stations(&app, &fixedport_stations, &fixedport_station_count)) {
        fprintf(stderr, "Failed to initialize fixedport debug instance\n");
        goto cleanup;
    }

    remove_one_candidate_port(candidate_ports, &candidate_port_count, app.boat.location_id);

    t_after_load = clock();
    mip_instance.boat = &app.boat;
    mip_instance.stations = fixedport_stations;
    mip_instance.n_stations = fixedport_station_count;
    mip_instance.candidate_port_location_ids = candidate_ports;
    mip_instance.candidate_port_count = candidate_port_count;
    mip_instance.distances = app.distances;
    mip_instance.max_location_id = app.max_location_id;
    mip_params.time_limit_seconds = time_limit_seconds;
    mip_params.thread_count = thread_count;
    mip_params.verbose = 1;
    mip_params.exclude_haul_distance = !(haul_distance_scale > 0.0);
    mip_params.use_scaled_haul_distance = (haul_distance_scale > 0.0);
    mip_params.haul_distance_scale = (haul_distance_scale > 0.0) ? haul_distance_scale : 0.0;

    printf("Fixedport MIP instance\n");
    printf("  boat: %s (id=%d)\n", app.boat.name ? app.boat.name : "Unknown", app.boat.boat_id);
    printf("  stations: %d\n", fixedport_station_count);
    printf("  candidate ports: %d\n", candidate_port_count);
    printf("  time limit: %s%.0f s\n",
           (time_limit_seconds > 0.0) ? "" : "none ",
           (time_limit_seconds > 0.0) ? time_limit_seconds : 0.0);
    printf("  threads: %d\n", thread_count);
    printf("  objective: %s\n", (haul_distance_scale > 0.0) ? "scaled_haul" : "transit");
    if (haul_distance_scale > 0.0)
        printf("  haul distance scale: %.8f\n", haul_distance_scale);
    printf("Solving...\n");

    if (solve_mip_fixedport(&mip_instance, &mip_params, &mip_solution) != 0) {
        fprintf(stderr, "Fixedport solve failed: status=%d solver_error=%d\n", mip_solution.status, mip_solution.solver_error);
        goto cleanup;
    }

    t_done = clock();
    if (!write_fixedport_json(db, output_path, &app,
                              fixedport_stations, fixedport_station_count,
                              candidate_ports, candidate_port_count,
                              port_lookup, port_lookup_count,
                              &mip_solution,
                              elapsed_seconds(t_start, t_after_load),
                              elapsed_seconds(t_start, t_done))) {
        fprintf(stderr, "Failed to write %s\n", output_path);
        goto cleanup;
    }

    printf("Wrote %s\n", output_path);
    printf("  stations: %d\n", fixedport_station_count);
    printf("  fixed_port_visits: %d\n", candidate_port_count);
    printf("  objective_distance_nm: %.2f\n", mip_solution.objective_value);
    ok = 1;

cleanup:
    free(candidate_ports);
    free_port_info_array(port_lookup, port_lookup_count);
    free_mip_fixedport_solution(&mip_solution);
    if (db) sqlite3_close(db);
    free_app_instance(&app);
    return ok ? 0 : 1;
}
