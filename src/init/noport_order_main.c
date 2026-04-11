#include "../mip/include/mip_noport.h"
#include "../include/feasibility.h"
#include "../include/init_types.h"
#include "../include/json_utils.h"
#include "../include/mip_report.h"

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

static int read_noport_config(const char *yaml_path,
                              int *boat_id_out,
                              double *l0seg_out,
                              double *global_time_limit_out,
                              int *thread_count_out,
                              int *include_haul_distance_out) {
    FILE *fp = NULL;
    char line[1024];
    int section = 0;

    if (boat_id_out) *boat_id_out = 2;
    if (l0seg_out) *l0seg_out = 0.0;
    if (global_time_limit_out) *global_time_limit_out = 0.0;
    if (thread_count_out) *thread_count_out = 0;
    if (include_haul_distance_out) *include_haul_distance_out = 0;

    fp = fopen(yaml_path, "r");
    if (!fp) {
        fprintf(stderr, "Warning: cannot open %s, using defaults boat_id=2 l0seg=0 threads=0\n", yaml_path);
        return 0;
    }

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
            else if (strncmp(trimmed, "objective:", 10) == 0) section = 3;
            else section = 0;
            continue;
        }

        if (section == 1 && strncmp(trimmed, "id:", 3) == 0 && boat_id_out) {
            *boat_id_out = atoi(trimmed + 3);
            continue;
        }

        if (section == 2 && strncmp(trimmed, "l0seg:", 6) == 0 && l0seg_out) {
            *l0seg_out = atof(trimmed + 6);
            continue;
        }
        if (section == 2 && strncmp(trimmed, "threads:", 8) == 0 && thread_count_out) {
            *thread_count_out = atoi(trimmed + 8);
            continue;
        }
        if (section == 3 && strncmp(trimmed, "include_haul_distance:", 22) == 0 && include_haul_distance_out) {
            char *value = trim_left(trimmed + 22);
            *include_haul_distance_out =
                !(strncmp(value, "false", 5) == 0 ||
                  strncmp(value, "False", 5) == 0 ||
                  strncmp(value, "FALSE", 5) == 0 ||
                  strncmp(value, "0", 1) == 0 ||
                  strncmp(value, "no", 2) == 0 ||
                  strncmp(value, "No", 2) == 0 ||
                  strncmp(value, "NO", 2) == 0);
            continue;
        }
    }

    fclose(fp);
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

static int load_stations(sqlite3 *db, app_instance_t *app) {
    sqlite3_stmt *stmt = NULL;
    int count = 0;

    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM stations;", -1, &stmt, NULL) != SQLITE_OK) return 1;
    if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    if (count < 0) return 1;
    app->stations = (Station*)calloc((size_t)count, sizeof(Station));
    if (count > 0 && !app->stations) return 1;
    app->n_stations = count;

    if (sqlite3_prepare_v2(db,
            "SELECT id, amount, comment, ext_id, start_location_id, end_location_id "
            "FROM stations ORDER BY id;",
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

static int append_int(int **arr, int *count, int *cap, int value) {
    int *tmp;
    int new_cap;
    if (*count >= *cap) {
        new_cap = (*cap == 0) ? 16 : (*cap * 2);
        tmp = (int*)realloc(*arr, (size_t)new_cap * sizeof(int));
        if (!tmp) return 0;
        *arr = tmp;
        *cap = new_cap;
    }
    (*arr)[(*count)++] = value;
    return 1;
}

static int append_int_if_new(int **arr, int *count, int *cap, int value) {
    for (int i = 0; i < *count; i++) {
        if ((*arr)[i] == value) return 1;
    }
    return append_int(arr, count, cap, value);
}

static int append_loc_if_changed(int **arr, int *count, int *cap, int value) {
    if (*count > 0 && (*arr)[*count - 1] == value) return 1;
    return append_int(arr, count, cap, value);
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
    static const char *sql =
        "SELECT waypoint_path FROM distances WHERE from_location_id = ? AND to_location_id = ?;";
    int count = 0;

    *out_ids = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, from_loc_id);
        sqlite3_bind_int(stmt, 2, to_loc_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *txt = sqlite3_column_text(stmt, 0);
            if (txt) count = parse_waypoint_path_json((const char*)txt, out_ids);
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
                count = parse_waypoint_path_json((const char*)txt, out_ids);
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

static const Station *find_station_by_id(const app_instance_t *app, int station_id) {
    for (int i = 0; i < app->n_stations; i++) {
        if (app->stations[i].station_id == station_id) return &app->stations[i];
    }
    return NULL;
}

static double distance_nm(const app_instance_t *app, int from_loc_id, int to_loc_id) {
    if (from_loc_id < 0 || from_loc_id >= app->max_location_id) return -1.0;
    if (to_loc_id < 0 || to_loc_id >= app->max_location_id) return -1.0;
    if (from_loc_id == to_loc_id) return 0.0;
    return app->distances[from_loc_id][to_loc_id];
}

static int add_distance_component(const app_instance_t *app,
                                  int from_loc_id,
                                  int to_loc_id,
                                  double *accumulator) {
    double d = distance_nm(app, from_loc_id, to_loc_id);
    if (d < 0.0) return 0;
    *accumulator += d;
    return 1;
}

static int compute_route_distance_breakdown(const app_instance_t *app,
                                            const mip_noport_solution_t *solution,
                                            gsp_distance_breakdown_t *breakdown) {
    int prev_loc_id;

    if (!app || !solution || !breakdown) return 0;
    memset(breakdown, 0, sizeof(*breakdown));

    prev_loc_id = app->boat.location_id;
    for (int i = 0; i < solution->order_length; i++) {
        int signed_station_id = solution->signed_station_ids[i];
        const Station *station = find_station_by_id(app, abs(signed_station_id));
        int entry_loc_id;
        int exit_loc_id;

        if (!station) return 0;
        if (signed_station_id < 0) {
            entry_loc_id = station->end_location_id;
            exit_loc_id = station->start_location_id;
        } else {
            entry_loc_id = station->start_location_id;
            exit_loc_id = station->end_location_id;
        }

        if (!add_distance_component(app, prev_loc_id, entry_loc_id,
                                    &breakdown->transit_distance_nm)) return 0;
        if (!add_distance_component(app, entry_loc_id, exit_loc_id,
                                    &breakdown->haul_distance_nm)) return 0;
        prev_loc_id = exit_loc_id;
    }

    if (!add_distance_component(app, prev_loc_id, app->boat.location_id,
                                &breakdown->transit_distance_nm)) return 0;
    breakdown->total_distance_nm = breakdown->transit_distance_nm + breakdown->haul_distance_nm;
    return 1;
}

static int build_route_locations(const app_instance_t *app,
                                 const mip_noport_solution_t *solution,
                                 int **route_out,
                                 int *route_len_out,
                                 int *total_catch_out) {
    int *route = NULL;
    int route_len = 0;
    int route_cap = 0;
    int total_catch = 0;

    if (!append_loc_if_changed(&route, &route_len, &route_cap, app->boat.location_id)) goto fail;

    for (int i = 0; i < solution->order_length; i++) {
        int signed_station_id = solution->signed_station_ids[i];
        const Station *station = find_station_by_id(app, abs(signed_station_id));
        if (!station) goto fail;
        if (signed_station_id < 0) {
            if (!append_loc_if_changed(&route, &route_len, &route_cap, station->end_location_id)) goto fail;
            if (!append_loc_if_changed(&route, &route_len, &route_cap, station->start_location_id)) goto fail;
        } else {
            if (!append_loc_if_changed(&route, &route_len, &route_cap, station->start_location_id)) goto fail;
            if (!append_loc_if_changed(&route, &route_len, &route_cap, station->end_location_id)) goto fail;
        }
        total_catch += station->amount;
    }

    if (!append_loc_if_changed(&route, &route_len, &route_cap, app->boat.location_id)) goto fail;

    *route_out = route;
    *route_len_out = route_len;
    *total_catch_out = total_catch;
    return 1;

fail:
    free(route);
    return 0;
}

static int write_noport_json(sqlite3 *db,
                             const char *output_path,
                             const app_instance_t *app,
                             const mip_noport_solution_t *solution,
                             double timeout_seconds,
                             double global_time_limit_seconds,
                             double preprocessing_seconds,
                             const gsp_distance_breakdown_t *distance_breakdown,
                             int include_haul_distance,
                             double total_runtime_seconds) {
    const char *final_variant_name = "capacity-infeasible";
    FILE *fp = NULL;
    int *route = NULL;
    int route_len = 0;
    int total_catch = 0;
    int *unique_waypoints = NULL;
    int unique_wp_count = 0;
    int unique_wp_cap = 0;
    int is_feasible = 1;
    int *positive_station_ids = NULL;
    int mip_seg_size = app->n_stations + 1;
    int mip_num_nodes = 2 * mip_seg_size;
    gsp_mip_solve_detail_t mip_detail;
    double mip_gap_percent = solution->gap * 100.0;
    double objective_distance_nm;

    if (!distance_breakdown) return 1;
    objective_distance_nm = include_haul_distance ?
        distance_breakdown->total_distance_nm :
        distance_breakdown->transit_distance_nm;

    if (!build_route_locations(app, solution, &route, &route_len, &total_catch)) return 1;

    fp = fopen(output_path, "w");
    if (!fp) {
        free(route);
        return 1;
    }

    if (solution->order_length > 0) {
        positive_station_ids = (int*)malloc((size_t)solution->order_length * sizeof(int));
        if (!positive_station_ids) {
            fclose(fp);
            free(route);
            return 1;
        }
        for (int i = 0; i < solution->order_length; i++) {
            positive_station_ids[i] = abs(solution->signed_station_ids[i]);
        }
    }

    if (!stations_have_no_duplicates(positive_station_ids, solution->order_length)) is_feasible = 0;
    if (solution->order_length != app->n_stations) is_feasible = 0;
    if (!segments_within_capacity(&total_catch, 1, app->boat.capacity)) is_feasible = 0;

    fprintf(fp, "{\n");
    fprintf(fp, "  \"metadata\": {\n");
    fprintf(fp, "    \"solver_version\": \"noport_1.0\",\n");
    fprintf(fp, "    \"timestamp\": \"%ld\",\n", (long)time(NULL));
    fprintf(fp, "    \"mode\": \"init_noport\",\n");
    fprintf(fp, "    \"strategy\": \"noport\",\n");
    fprintf(fp, "    \"boat_id\": %d,\n", app->boat.boat_id);
    fprintf(fp, "    \"boat_name\": \"%s\",\n", app->boat.name ? app->boat.name : "Unknown");
    fprintf(fp, "    \"boat_docked_location\": {\"lat\": %.6f, \"lon\": %.6f},\n", app->boat_start_lat, app->boat_start_lon);
    fprintf(fp, "    \"boat_location_id\": %d,\n", app->boat.location_id);
    fprintf(fp, "    \"global_time_limit_seconds\": %.6f,\n", global_time_limit_seconds);
    fprintf(fp, "    \"objective_distance_mode\": \"%s\"\n",
            include_haul_distance ? "total" : "transit");
    fprintf(fp, "  },\n");

    fprintf(fp, "  \"problem\": {\n");
    fprintf(fp, "    \"num_nodes\": %d,\n", route_len);
    fprintf(fp, "    \"num_stations\": %d,\n", app->n_stations);
    fprintf(fp, "    \"capacity\": %d\n", app->boat.capacity);
    fprintf(fp, "  },\n");

    fprintf(fp, "  \"solution\": {\n");
    fprintf(fp, "    \"%s\": {\n", final_variant_name);
    fprintf(fp, "    \"variant\": \"%s\",\n", final_variant_name);
    fprintf(fp, "    \"tour_segments_location_ids\": [\n");
    fprintf(fp, "      [");
    if (route_len > 0) {
        fprintf(fp, "%d", route[0]);
        for (int i = 0; i < route_len - 1; i++) {
            int *wps = NULL;
            int n_wps = lookup_waypoint_path(db, route[i], route[i + 1], &wps);
            for (int k = 0; k < n_wps; k++) {
                fprintf(fp, ", %d", wps[k]);
                append_int_if_new(&unique_waypoints, &unique_wp_count, &unique_wp_cap, wps[k]);
            }
            fprintf(fp, ", %d", route[i + 1]);
            free(wps);
        }
    }
    fprintf(fp, "]\n");
    fprintf(fp, "    ],\n");

    fprintf(fp, "    \"dock_location_ids\": [%d, %d],\n",
            app->boat.location_id, app->boat.location_id);

    fprintf(fp, "    \"unique_waypoint_location_ids\": [");
    for (int i = 0; i < unique_wp_count; i++) {
        if (i) fprintf(fp, ", ");
        fprintf(fp, "%d", unique_waypoints[i]);
    }
    fprintf(fp, "],\n");

    fprintf(fp, "    \"tour_segments_station_ids\": [\n");
    fprintf(fp, "      [");
    for (int i = 0; i < solution->order_length; i++) {
        if (i) fprintf(fp, ", ");
        fprintf(fp, "%d", abs(solution->signed_station_ids[i]));
    }
    fprintf(fp, "]\n");
    fprintf(fp, "    ],\n");

    fprintf(fp, "    \"signed_station_ids\": [");
    for (int i = 0; i < solution->order_length; i++) {
        if (i) fprintf(fp, ", ");
        fprintf(fp, "%d", solution->signed_station_ids[i]);
    }
    fprintf(fp, "],\n");

    fprintf(fp, "    \"tour_length\": [%d],\n", solution->order_length);
    fprintf(fp, "    \"segment_count\": 1,\n");
    fprintf(fp, "    \"segment_catch_amount\": [%d],\n", total_catch);
    fprintf(fp, "    \"objective_distance_nm\": %.2f,\n", objective_distance_nm);
    gsp_write_distance_nm_json(fp, "    ", distance_breakdown, 1, distance_breakdown, 1);
    fprintf(fp, "    \"feasible\": %s\n", is_feasible ? "true" : "false");
    fprintf(fp, "    }\n");
    fprintf(fp, "  },\n");

    gsp_mip_solve_detail_init(&mip_detail);
    mip_detail.station_count = app->n_stations;
    mip_detail.node_count = app->n_stations + 2;
    mip_detail.model_num_vars = mip_num_nodes * mip_num_nodes;
    mip_detail.model_num_constrs = 5 * mip_seg_size;
    mip_detail.runtime_seconds = solution->runtime_seconds;
    mip_detail.gap_percent = mip_gap_percent;
    gsp_write_segment_mip_section(fp, "l0seg", "noport_directed_tsp", timeout_seconds, &mip_detail, 1);

    fprintf(fp, "  \"summary\": {\n");
    {
        double distance_trajectory[1] = {distance_breakdown->total_distance_nm};
        double runtime_trajectory[1] = {solution->runtime_seconds};
        const char *stage_name =
            (solution->status == MIP_STATUS_OPTIMAL) ? "noport_complete" :
            (solution->status == MIP_STATUS_TIME_LIMIT) ? "time_limit" :
            (solution->status == MIP_STATUS_SUBOPTIMAL) ? "suboptimal" : "failed";
        gsp_write_summary_status_json(fp, "    ", final_variant_name, stage_name,
                                      is_feasible, "noport_mip", 1);
        gsp_write_summary_distance_json(fp, "    ", 0, 0.0,
                                        distance_trajectory, 1, distance_breakdown->total_distance_nm, 1);
        gsp_write_summary_runtime_json(fp, "    ", preprocessing_seconds,
                                       runtime_trajectory, 1, 0.0, total_runtime_seconds, 1);
        gsp_write_summary_mip_json(fp, "    ", 1,
                                   solution->runtime_seconds, solution->runtime_seconds,
                                   mip_gap_percent, mip_gap_percent, 0);
    }
    fprintf(fp, "  },\n");

    fprintf(fp, "  \"solver_stats\": {\n");
    fprintf(fp, "    \"status\": \"%s\",\n",
            (solution->status == MIP_STATUS_OPTIMAL) ? "optimal" :
            (solution->status == MIP_STATUS_TIME_LIMIT) ? "time_limit" :
            (solution->status == MIP_STATUS_SUBOPTIMAL) ? "suboptimal" : "failed");
    fprintf(fp, "    \"preprocessing_seconds\": %.6f,\n", preprocessing_seconds);
    fprintf(fp, "    \"runtime_seconds\": %.6f,\n", solution->runtime_seconds);
    fprintf(fp, "    \"total_runtime_seconds\": %.6f,\n", total_runtime_seconds);
    fprintf(fp, "    \"method\": \"noport_mip\",\n");
    fprintf(fp, "    \"mip_gap\": %.6f\n", solution->gap);
    fprintf(fp, "  }\n");
    fprintf(fp, "}\n");

    fclose(fp);
    free(route);
    free(unique_waypoints);
    free(positive_station_ids);
    return 0;
}

int main(int argc, char **argv) {
    const char *db_path = NULL;
    const char *config_path = NULL;
    const char *output_path = NULL;
    sqlite3 *db = NULL;
    app_instance_t app;
    mip_noport_instance_t mip_instance;
    mip_noport_params_t mip_params;
    mip_noport_solution_t mip_solution;
    int boat_id = 2;
    double l0seg_seconds = 0.0;
    double global_time_limit_seconds = 0.0;
    double effective_time_limit_seconds = 0.0;
    int thread_count = 0;
    int include_haul_distance = 1;
    gsp_distance_breakdown_t distance_breakdown;
    double preprocessing_seconds = 0.0;
    double total_runtime_seconds = 0.0;
    clock_t preprocess_start;
    clock_t preprocess_end;

    memset(&app, 0, sizeof(app));
    memset(&mip_instance, 0, sizeof(mip_instance));
    memset(&mip_params, 0, sizeof(mip_params));
    memset(&mip_solution, 0, sizeof(mip_solution));
    memset(&distance_breakdown, 0, sizeof(distance_breakdown));

    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--database") == 0) db_path = argv[i + 1];
        else if (strcmp(argv[i], "--config") == 0) config_path = argv[i + 1];
        else if (strcmp(argv[i], "--output") == 0) output_path = argv[i + 1];
    }

    if (!db_path || !config_path || !output_path) {
        fprintf(stderr, "Usage: %s --database <gsp_data.db> --config <gsp_solver.yaml> --output <sol/noport/noport.json>\n", argv[0]);
        return 1;
    }

    preprocess_start = clock();
    read_noport_config(config_path, &boat_id, &l0seg_seconds, &global_time_limit_seconds,
                       &thread_count, &include_haul_distance);

    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }

    if (load_boat(db, boat_id, &app) != 0 ||
        load_stations(db, &app) != 0 ||
        load_distances(db, &app) != 0) {
        fprintf(stderr, "Failed to load noport MIP instance from database\n");
        sqlite3_close(db);
        free_app_instance(&app);
        return 1;
    }

    mip_instance.boat = &app.boat;
    mip_instance.stations = app.stations;
    mip_instance.n_stations = app.n_stations;
    mip_instance.distances = app.distances;
    mip_instance.max_location_id = app.max_location_id;

    memset(&mip_params, 0, sizeof(mip_params));
    effective_time_limit_seconds = (l0seg_seconds > 0.0) ? l0seg_seconds : global_time_limit_seconds;
    if (l0seg_seconds > 0.0 && global_time_limit_seconds > 0.0 &&
        global_time_limit_seconds < effective_time_limit_seconds) {
        effective_time_limit_seconds = global_time_limit_seconds;
    }
    mip_params.time_limit_seconds = effective_time_limit_seconds;
    mip_params.thread_count = thread_count;
    mip_params.verbose = 1;
    mip_params.mip_gap = 0.0;
    mip_params.exclude_haul_distance = !include_haul_distance;

    preprocess_end = clock();
    preprocessing_seconds = elapsed_seconds(preprocess_start, preprocess_end);

    printf("Noport MIP instance\n");
    printf("  boat: %s (id=%d)\n", app.boat.name ? app.boat.name : "Unknown", app.boat.boat_id);
    printf("  stations: %d\n", app.n_stations);
    printf("  l0seg time limit: %s%.0f s\n",
           (l0seg_seconds > 0.0) ? "" : "uncapped ",
           (l0seg_seconds > 0.0) ? l0seg_seconds : 0.0);
    printf("  global time limit: %s%.0f s\n",
           (global_time_limit_seconds > 0.0) ? "" : "none ",
           (global_time_limit_seconds > 0.0) ? global_time_limit_seconds : 0.0);
    printf("  threads: %d\n", thread_count);
    printf("  objective distance mode: %s\n", include_haul_distance ? "total" : "transit");

    if (solve_mip_noport(&mip_instance, &mip_params, &mip_solution) != 0) {
        fprintf(stderr, "Gurobi no-port solve failed\n");
        sqlite3_close(db);
        free_app_instance(&app);
        free_mip_noport_solution(&mip_solution);
        return 1;
    }

    if (!compute_route_distance_breakdown(&app, &mip_solution, &distance_breakdown)) {
        fprintf(stderr, "Failed to compute no-port distance breakdown\n");
        sqlite3_close(db);
        free_app_instance(&app);
        free_mip_noport_solution(&mip_solution);
        return 1;
    }

    total_runtime_seconds = preprocessing_seconds + mip_solution.runtime_seconds;

    if (write_noport_json(db, output_path, &app, &mip_solution,
                          l0seg_seconds,
                          global_time_limit_seconds,
                          preprocessing_seconds,
                          &distance_breakdown,
                          include_haul_distance,
                          total_runtime_seconds) != 0) {
        fprintf(stderr, "Failed to write %s\n", output_path);
        sqlite3_close(db);
        free_app_instance(&app);
        free_mip_noport_solution(&mip_solution);
        return 1;
    }

    printf("[OK] Wrote %s\n", output_path);
    printf("  objective distance: %.2f nm\n",
           include_haul_distance ? distance_breakdown.total_distance_nm : distance_breakdown.transit_distance_nm);
    printf("  transit distance: %.2f nm\n", distance_breakdown.transit_distance_nm);
    printf("  haul distance: %.2f nm\n", distance_breakdown.haul_distance_nm);
    printf("  total distance: %.2f nm\n", distance_breakdown.total_distance_nm);
    printf("  stations visited: %d\n", mip_solution.order_length);
    printf("  runtime: %.2f s\n", mip_solution.runtime_seconds);
    printf("  total runtime: %.2f s\n", total_runtime_seconds);

    sqlite3_close(db);
    free_app_instance(&app);
    free_mip_noport_solution(&mip_solution);
    return 0;
}
