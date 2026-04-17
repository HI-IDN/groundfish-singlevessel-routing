#include "../include/json_utils.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>

static void write_double_array(FILE *fp, const double *values, int count) {
    for (int i = 0; i < count; i++) {
        if (i) fprintf(fp, ", ");
        fprintf(fp, "%.6f", values ? values[i] : 0.0);
    }
}

static void write_int_array(FILE *fp, const int *values, int count) {
    for (int i = 0; i < count; i++) {
        if (i) fprintf(fp, ", ");
        fprintf(fp, "%d", values ? values[i] : 0);
    }
}

static void make_child_indent(const char *base, char *out, size_t out_size) {
    snprintf(out, out_size, "%s  ", base ? base : "");
}

static void write_nested_int_arrays(FILE *fp,
                                    const char *indent,
                                    const char *key,
                                    const gsp_int_list_view_t *lists,
                                    int list_count,
                                    int trailing_comma) {
    const char *base = indent ? indent : "";
    fprintf(fp, "%s\"%s\": [\n", base, key ? key : "");
    for (int i = 0; i < list_count; i++) {
        fprintf(fp, "%s  [", base);
        write_int_array(fp, lists ? lists[i].values : NULL, lists ? lists[i].count : 0);
        fprintf(fp, "]%s\n", (i + 1 < list_count) ? "," : "");
    }
    fprintf(fp, "%s]%s\n", base, trailing_comma ? "," : "");
}

static int append_int_local_json(int **arr, int *n, int *cap, int v) {
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

static int append_unique_int_local_json(int **arr, int *n, int *cap, int v) {
    for (int i = 0; i < *n; i++) {
        if ((*arr)[i] == v) return 1;
    }
    return append_int_local_json(arr, n, cap, v);
}

void gsp_summary_reset(gsp_summary_json_t *summary) {
    if (!summary) return;
    memset(summary, 0, sizeof(*summary));
}

void gsp_summary_set_status_and_distance(gsp_summary_json_t *summary,
                                         const char *final_name,
                                         const char *stage_name,
                                         int feasible,
                                         const char *method_name,
                                         int has_baseline,
                                         double baseline_distance_nm,
                                         const double *distance_trajectory_nm,
                                         int distance_trajectory_count,
                                         double final_distance_nm) {
    if (!summary) return;
    summary->final_name = final_name;
    summary->stage_name = stage_name;
    summary->feasible = feasible;
    summary->method_name = method_name;
    summary->has_baseline = has_baseline;
    summary->baseline_distance_nm = baseline_distance_nm;
    summary->distance_trajectory_nm = distance_trajectory_nm;
    summary->distance_trajectory_count = distance_trajectory_count;
    summary->final_distance_nm = final_distance_nm;
}

void gsp_summary_set_runtime(gsp_summary_json_t *summary,
                             double preprocessing_seconds,
                             const double *solution_runtime_seconds,
                             int solution_runtime_count,
                             double postprocessing_seconds,
                             double grandtotal_seconds) {
    if (!summary) return;
    summary->preprocessing_seconds = preprocessing_seconds;
    summary->solution_runtime_seconds = solution_runtime_seconds;
    summary->solution_runtime_count = solution_runtime_count;
    summary->postprocessing_seconds = postprocessing_seconds;
    summary->grandtotal_seconds = grandtotal_seconds;
    summary->include_runtime = 1;
}

void gsp_summary_set_mip(gsp_summary_json_t *summary,
                         int solve_count,
                         double runtime_mean,
                         double runtime_max,
                         double gap_mean,
                         double gap_max) {
    if (!summary) return;
    summary->include_mip = 1;
    summary->mip_solve_count = solve_count;
    summary->mip_runtime_mean = runtime_mean;
    summary->mip_runtime_max = runtime_max;
    summary->mip_gap_mean = gap_mean;
    summary->mip_gap_max = gap_max;
}

void gsp_write_solution_json(FILE *fp,
                             const char *indent,
                             const gsp_solution_json_view_t *view,
                             int trailing_comma) {
    const char *base = indent ? indent : "";
    char child[64];
    if (!fp || !view) return;
    make_child_indent(base, child, sizeof(child));

    fprintf(fp, "%s{\n", base);
    if (view->variant_name) {
        fprintf(fp, "%s  \"variant\": \"%s\",\n", base, view->variant_name);
    }
    write_nested_int_arrays(fp, base, "tour_segments_location_ids",
                            view->tour_segments_location_ids,
                            view->tour_segments_location_count, 1);
    fprintf(fp, "%s  \"dock_location_ids\": [", base);
    write_int_array(fp, view->dock_location_ids, view->dock_location_count);
    fprintf(fp, "],\n");
    fprintf(fp, "%s  \"unique_waypoint_location_ids\": [", base);
    write_int_array(fp, view->unique_waypoint_location_ids, view->unique_waypoint_location_count);
    fprintf(fp, "],\n");
    write_nested_int_arrays(fp, base, "tour_segments_station_ids",
                            view->tour_segments_station_ids,
                            view->tour_segments_station_count, 1);
    fprintf(fp, "%s  \"tour_length\": [", base);
    write_int_array(fp, view->tour_length, view->tour_length_count);
    fprintf(fp, "],\n");
    fprintf(fp, "%s  \"segment_count\": %d,\n", base, view->segment_count);
    fprintf(fp, "%s  \"segment_catch_amount\": [", base);
    write_int_array(fp, view->segment_catch_amount, view->segment_catch_count);
    fprintf(fp, "],\n");
    if (view->extra_writer) {
        view->extra_writer(fp, base, view->extra_ctx);
        fprintf(fp, ",\n");
    }
    gsp_write_distance_nm_json(fp, child, view->segment_breakdowns, view->segment_count,
                               view->grand_total, 1);
    fprintf(fp, "%s  \"feasible\": %s\n", base, view->feasible ? "true" : "false");
    fprintf(fp, "%s}%s\n", base, trailing_comma ? "," : "");
}

int gsp_write_segmented_solution_entry_json(FILE *fp,
                                            const char *entry_indent,
                                            const char *value_indent,
                                            const char *label,
                                            const char *variant_name,
                                            const nn_instance_t *inst,
                                            const nn_solution_t *sol,
                                            int boat_start_loc_id,
                                            int boat_end_loc_id,
                                            int is_feasible,
                                            const gsp_distance_breakdown_t *segment_breakdowns,
                                            const gsp_distance_breakdown_t *grand_total,
                                            gsp_waypoint_lookup_fn waypoint_lookup,
                                            const void *waypoint_lookup_ctx) {
    gsp_int_list_view_t *location_segments = NULL;
    gsp_int_list_view_t *station_segments = NULL;
    int *unique_waypoint_location_ids = NULL;
    int uniq_wp_n = 0, uniq_wp_cap = 0;
    int *dock_location_ids = NULL;
    int dock_n = 0;
    int *tour_length = NULL;
    int ok = 0;
    const char *entry_base = entry_indent ? entry_indent : "";
    const char *value_base = value_indent ? value_indent : "";

    if (!fp || !inst || !sol || !segment_breakdowns || !grand_total || !waypoint_lookup) return 0;

    location_segments = (gsp_int_list_view_t*)calloc((size_t)sol->segment_count, sizeof(gsp_int_list_view_t));
    station_segments = (gsp_int_list_view_t*)calloc((size_t)sol->segment_count, sizeof(gsp_int_list_view_t));
    tour_length = (int*)calloc((size_t)sol->segment_count, sizeof(int));
    if (!location_segments || !station_segments || !tour_length) goto cleanup;

    {
        int *segment_end_location_ids = (int*)malloc((size_t)sol->segment_count * sizeof(int));
        if (!segment_end_location_ids) goto cleanup;
        for (int s = 0; s < sol->segment_count; s++) {
            segment_end_location_ids[s] =
                (s == sol->segment_count - 1) ? boat_end_loc_id : sol->tour[sol->segment_ends[s]];
        }
        if (!gsp_build_dock_location_ids_from_segment_ends(boat_start_loc_id,
                                                           segment_end_location_ids,
                                                           sol->segment_count,
                                                           &dock_location_ids,
                                                           &dock_n)) {
            free(segment_end_location_ids);
            goto cleanup;
        }
        free(segment_end_location_ids);
    }

    for (int s = 0; s < sol->segment_count; s++) {
        int start = sol->segment_starts[s];
        int end = sol->segment_ends[s];
        int base_cap = (end - start + 1) + 2;
        int *base = (int*)malloc((size_t)base_cap * sizeof(int));
        int *expanded = NULL;
        int expanded_n = 0, expanded_cap = 0;
        int *station_ids = NULL;
        int station_n = 0, station_cap = 0;
        int base_n = 0;
        if (!base) goto cleanup;

        tour_length[s] = end - start + 1;
        base[base_n++] = (s == 0) ? boat_start_loc_id : sol->tour[sol->segment_ends[s - 1]];
        for (int i = start; i <= end; i++) base[base_n++] = sol->tour[i];
        if (s == sol->segment_count - 1 && (base_n == 0 || base[base_n - 1] != boat_end_loc_id)) {
            base[base_n++] = boat_end_loc_id;
        }

        for (int i = 0; i < sol->visit_station_count; i++) {
            if (sol->visit_station_segment[i] == s) {
                if (!append_int_local_json(&station_ids, &station_n, &station_cap,
                                           sol->visit_station_ids[i] *
                                           ((sol->visit_station_direction &&
                                             sol->visit_station_direction[i] < 0) ? -1 : 1))) {
                    free(base);
                    free(station_ids);
                    goto cleanup;
                }
            }
        }

        if (base_n > 0) {
            if (!append_int_local_json(&expanded, &expanded_n, &expanded_cap, base[0])) {
                free(base);
                free(station_ids);
                goto cleanup;
            }
            for (int i = 0; i < base_n - 1; i++) {
                int *wps = NULL;
                int n_wps = 0;
                if (!waypoint_lookup(waypoint_lookup_ctx, base[i], base[i + 1], &wps, &n_wps)) {
                    free(base);
                    free(station_ids);
                    free(wps);
                    goto cleanup;
                }
                for (int k = 0; k < n_wps; k++) {
                    if (!append_int_local_json(&expanded, &expanded_n, &expanded_cap, wps[k]) ||
                        !append_unique_int_local_json(&unique_waypoint_location_ids, &uniq_wp_n, &uniq_wp_cap, wps[k])) {
                        free(base);
                        free(station_ids);
                        free(wps);
                        goto cleanup;
                    }
                }
                if (!append_int_local_json(&expanded, &expanded_n, &expanded_cap, base[i + 1])) {
                    free(base);
                    free(station_ids);
                    free(wps);
                    goto cleanup;
                }
                free(wps);
            }
        }

        free(base);
        location_segments[s].values = expanded;
        location_segments[s].count = expanded_n;
        station_segments[s].values = station_ids;
        station_segments[s].count = station_n;
    }

    fprintf(fp, "%s\"%s\": ", entry_base, label ? label : "");
    {
        gsp_solution_json_view_t view = {0};
        view.variant_name = variant_name ? variant_name : label;
        view.tour_segments_location_ids = location_segments;
        view.tour_segments_location_count = sol->segment_count;
        view.dock_location_ids = dock_location_ids;
        view.dock_location_count = dock_n;
        view.unique_waypoint_location_ids = unique_waypoint_location_ids;
        view.unique_waypoint_location_count = uniq_wp_n;
        view.tour_segments_station_ids = station_segments;
        view.tour_segments_station_count = sol->segment_count;
        view.tour_length = tour_length;
        view.tour_length_count = sol->segment_count;
        view.segment_count = sol->segment_count;
        view.segment_catch_amount = sol->segment_catches;
        view.segment_catch_count = sol->segment_count;
        view.segment_breakdowns = segment_breakdowns;
        view.grand_total = grand_total;
        view.feasible = is_feasible;
        gsp_write_solution_json(fp, value_base, &view, 0);
    }
    ok = 1;

cleanup:
    if (location_segments) {
        for (int s = 0; s < sol->segment_count; s++) free((int*)location_segments[s].values);
    }
    if (station_segments) {
        for (int s = 0; s < sol->segment_count; s++) free((int*)station_segments[s].values);
    }
    free(location_segments);
    free(station_segments);
    free(unique_waypoint_location_ids);
    free(dock_location_ids);
    free(tour_length);
    return ok;
}

void gsp_write_metadata_json(FILE *fp,
                             const char *indent,
                             const gsp_metadata_json_t *metadata,
                             int trailing_comma) {
    const char *base = indent ? indent : "";
    const char *child = base;
    if (!fp || !metadata) return;

    fprintf(fp, "%s\"metadata\": {\n", base);
    fprintf(fp, "%s  \"solver_version\": \"%s\",\n", child,
            metadata->solver_version ? metadata->solver_version : "");
    fprintf(fp, "%s  \"timestamp\": \"%ld\",\n", child, (long)time(NULL));
    fprintf(fp, "%s  \"mode\": \"%s\",\n", child,
            metadata->mode_name ? metadata->mode_name : "");
    fprintf(fp, "%s  \"strategy\": \"%s\",\n", child,
            metadata->strategy_name ? metadata->strategy_name : "");
    fprintf(fp, "%s  \"boat_id\": %d,\n", child, metadata->boat_id);
    fprintf(fp, "%s  \"boat_name\": \"%s\",\n", child,
            metadata->boat_name ? metadata->boat_name : "Unknown");
    fprintf(fp, "%s  \"boat_docked_location\": {\"lat\": %.6f, \"lon\": %.6f},\n",
            child, metadata->boat_lat, metadata->boat_lon);
    fprintf(fp, "%s  \"boat_location_id\": %d", child, metadata->boat_location_id);
    if (metadata->extra_writer) {
        fprintf(fp, ",\n");
        metadata->extra_writer(fp, child, metadata->extra_ctx);
        fprintf(fp, "\n");
    } else {
        fprintf(fp, "\n");
    }
    fprintf(fp, "%s}%s\n", base, trailing_comma ? "," : "");
}

void gsp_write_problem_json(FILE *fp,
                            const char *indent,
                            const gsp_problem_json_t *problem,
                            int trailing_comma) {
    const char *base = indent ? indent : "";
    int wrote_any = 0;
    if (!fp || !problem) return;

    fprintf(fp, "%s\"problem\": {\n", base);
    if (problem->has_num_nodes) {
        fprintf(fp, "%s  \"num_nodes\": %d", base, problem->num_nodes);
        wrote_any = 1;
    }
    if (problem->has_num_stations) {
        fprintf(fp, "%s%s\"num_stations\": %d", wrote_any ? ",\n" : "", base, problem->num_stations);
        wrote_any = 1;
    }
    if (problem->has_num_ports) {
        fprintf(fp, "%s%s\"num_ports\": %d", wrote_any ? ",\n" : "", base, problem->num_ports);
        wrote_any = 1;
    }
    if (problem->has_capacity) {
        fprintf(fp, "%s%s\"capacity\": %.0f", wrote_any ? ",\n" : "", base, problem->capacity);
        wrote_any = 1;
    }
    if (problem->has_target_capacity) {
        fprintf(fp, "%s%s\"target_capacity\": %.0f", wrote_any ? ",\n" : "", base, problem->target_capacity);
        wrote_any = 1;
    }
    if (problem->has_target_catch_slack_kg) {
        fprintf(fp, "%s%s\"target_catch_slack_kg\": %d", wrote_any ? ",\n" : "",
                base, problem->target_catch_slack_kg);
        wrote_any = 1;
    }
    if (problem->extra_writer) {
        fprintf(fp, "%s%s", wrote_any ? ",\n" : "", "");
        problem->extra_writer(fp, base, problem->extra_ctx);
        fprintf(fp, "\n");
    } else if (wrote_any) {
        fprintf(fp, "\n");
    }
    fprintf(fp, "%s}%s\n", base, trailing_comma ? "," : "");
}

void gsp_write_solver_stats_json(FILE *fp,
                                 const char *indent,
                                 const gsp_solver_stats_json_t *solver_stats,
                                 int trailing_comma) {
    const char *base = indent ? indent : "";
    if (!fp || !solver_stats) return;

    fprintf(fp, "%s\"solver_stats\": {\n", base);
    fprintf(fp, "%s  \"status\": \"%s\",\n", base,
            solver_stats->status_name ? solver_stats->status_name : "unknown");
    fprintf(fp, "%s  \"gurobi_status\": \"%s\",\n", base,
            solver_stats->gurobi_status_name ? solver_stats->gurobi_status_name : "unknown");
    fprintf(fp, "%s  \"gurobi_status_code\": %d,\n", base, solver_stats->gurobi_status_code);
    if (solver_stats->include_preprocessing_seconds) {
        fprintf(fp, "%s  \"preprocessing_seconds\": %.6f,\n", base, solver_stats->preprocessing_seconds);
    }
    fprintf(fp, "%s  \"runtime_seconds\": %.6f,\n", base, solver_stats->runtime_seconds);
    fprintf(fp, "%s  \"total_runtime_seconds\": %.6f,\n", base, solver_stats->total_runtime_seconds);
    fprintf(fp, "%s  \"method\": \"%s\",\n", base,
            solver_stats->method_name ? solver_stats->method_name : "unknown");
    fprintf(fp, "%s  \"mip_gap\": %.6f\n", base, solver_stats->mip_gap);
    fprintf(fp, "%s}%s\n", base, trailing_comma ? "," : "");
}

void gsp_write_distance_nm_json(FILE *fp,
                                const char *indent,
                                const gsp_distance_breakdown_t *segment_breakdowns,
                                int segment_count,
                                const gsp_distance_breakdown_t *grand_total,
                                int trailing_comma) {
    const char *base = indent ? indent : "";
    const gsp_distance_breakdown_t zero = {0.0, 0.0, 0.0};
    const gsp_distance_breakdown_t *total = grand_total ? grand_total : &zero;

    if (!fp) return;

    fprintf(fp, "%s\"distance_nm\": {\n", base);
    fprintf(fp, "%s  \"segment\": {\n", base);

    fprintf(fp, "%s    \"transit\": [", base);
    for (int s = 0; s < segment_count; s++) {
        if (s) fprintf(fp, ", ");
        fprintf(fp, "%.2f", segment_breakdowns ? segment_breakdowns[s].transit_distance_nm : 0.0);
    }
    fprintf(fp, "],\n");

    fprintf(fp, "%s    \"haul\": [", base);
    for (int s = 0; s < segment_count; s++) {
        if (s) fprintf(fp, ", ");
        fprintf(fp, "%.2f", segment_breakdowns ? segment_breakdowns[s].haul_distance_nm : 0.0);
    }
    fprintf(fp, "],\n");

    fprintf(fp, "%s    \"total\": [", base);
    for (int s = 0; s < segment_count; s++) {
        if (s) fprintf(fp, ", ");
        fprintf(fp, "%.2f", segment_breakdowns ? segment_breakdowns[s].total_distance_nm : 0.0);
    }
    fprintf(fp, "]\n");

    fprintf(fp, "%s  },\n", base);
    fprintf(fp, "%s  \"grand_total\": {\n", base);
    fprintf(fp, "%s    \"transit\": %.2f,\n", base, total->transit_distance_nm);
    fprintf(fp, "%s    \"haul\": %.2f,\n", base, total->haul_distance_nm);
    fprintf(fp, "%s    \"total\": %.2f\n", base, total->total_distance_nm);
    fprintf(fp, "%s  }\n", base);
    fprintf(fp, "%s}%s\n", base, trailing_comma ? "," : "");
}

void gsp_write_summary_status_json(FILE *fp,
                                   const char *indent,
                                   const char *final_name,
                                   const char *stage_name,
                                   int feasible,
                                   const char *method_name,
                                   int trailing_comma) {
    const char *base = indent ? indent : "";
    if (!fp) return;
    fprintf(fp, "%s\"status\": {\n", base);
    fprintf(fp, "%s  \"final\": \"%s\",\n", base, final_name ? final_name : "");
    fprintf(fp, "%s  \"stage\": \"%s\",\n", base, stage_name ? stage_name : "");
    fprintf(fp, "%s  \"feasible\": %s,\n", base, feasible ? "true" : "false");
    fprintf(fp, "%s  \"method\": \"%s\"\n", base, method_name ? method_name : "unknown");
    fprintf(fp, "%s}%s\n", base, trailing_comma ? "," : "");
}

void gsp_write_summary_distance_json(FILE *fp,
                                     const char *indent,
                                     int has_baseline,
                                     double baseline_distance_nm,
                                     const double *trajectory_distance_nm,
                                     int trajectory_count,
                                     double final_distance_nm,
                                     int trailing_comma) {
    const char *base = indent ? indent : "";
    if (!fp) return;
    fprintf(fp, "%s\"distance_nm\": {\n", base);
    if (has_baseline) {
        fprintf(fp, "%s  \"baseline\": %.2f,\n", base, baseline_distance_nm);
    }
    fprintf(fp, "%s  \"trajectory\": [", base);
    for (int i = 0; i < trajectory_count; i++) {
        if (i) fprintf(fp, ", ");
        fprintf(fp, "%.2f", trajectory_distance_nm ? trajectory_distance_nm[i] : 0.0);
    }
    fprintf(fp, "],\n");
    fprintf(fp, "%s  \"final\": %.2f\n", base, final_distance_nm);
    fprintf(fp, "%s}%s\n", base, trailing_comma ? "," : "");
}

void gsp_write_summary_runtime_json(FILE *fp,
                                    const char *indent,
                                    double preprocessing_seconds,
                                    const double *solution_runtime_seconds,
                                    int solution_runtime_count,
                                    double postprocessing_seconds,
                                    double grandtotal_seconds,
                                    int trailing_comma) {
    const char *base = indent ? indent : "";
    if (!fp) return;
    fprintf(fp, "%s\"runtime_seconds\": {\n", base);
    fprintf(fp, "%s  \"preprocessing\": %.6f,\n", base, preprocessing_seconds);
    fprintf(fp, "%s  \"solution\": [", base);
    write_double_array(fp, solution_runtime_seconds, solution_runtime_count);
    fprintf(fp, "],\n");
    fprintf(fp, "%s  \"postprocessing\": %.6f,\n", base, postprocessing_seconds);
    fprintf(fp, "%s  \"grandtotal\": %.6f\n", base, grandtotal_seconds);
    fprintf(fp, "%s}%s\n", base, trailing_comma ? "," : "");
}

void gsp_write_summary_mip_json(FILE *fp,
                                const char *indent,
                                int solve_count,
                                double runtime_mean,
                                double runtime_max,
                                double gap_mean,
                                double gap_max,
                                int trailing_comma) {
    const char *base = indent ? indent : "";
    if (!fp) return;
    fprintf(fp, "%s\"mip\": {\n", base);
    fprintf(fp, "%s  \"solves\": %d,\n", base, solve_count);
    fprintf(fp, "%s  \"runtime_seconds\": {\n", base);
    fprintf(fp, "%s    \"mean\": ", base);
    if (runtime_mean >= 0.0) fprintf(fp, "%.6f", runtime_mean);
    else fprintf(fp, "null");
    fprintf(fp, ",\n");
    fprintf(fp, "%s    \"max\": ", base);
    if (runtime_max >= 0.0) fprintf(fp, "%.6f", runtime_max);
    else fprintf(fp, "null");
    fprintf(fp, "\n");
    fprintf(fp, "%s  },\n", base);
    fprintf(fp, "%s  \"gap_percent\": {\n", base);
    fprintf(fp, "%s    \"mean\": ", base);
    if (gap_mean >= 0.0) fprintf(fp, "%.6f", gap_mean);
    else fprintf(fp, "null");
    fprintf(fp, ",\n");
    fprintf(fp, "%s    \"max\": ", base);
    if (gap_max >= 0.0) fprintf(fp, "%.6f", gap_max);
    else fprintf(fp, "null");
    fprintf(fp, "\n");
    fprintf(fp, "%s  }\n", base);
    fprintf(fp, "%s}%s\n", base, trailing_comma ? "," : "");
}

void gsp_write_summary_json(FILE *fp,
                            const char *indent,
                            const gsp_summary_json_t *summary,
                            int trailing_comma) {
    const char *base = indent ? indent : "";
    char child[64];
    if (!fp || !summary) return;
    make_child_indent(base, child, sizeof(child));

    fprintf(fp, "%s\"summary\": {\n", base);
    gsp_write_summary_status_json(fp, child, summary->final_name, summary->stage_name,
                                  summary->feasible, summary->method_name,
                                  summary->include_runtime || summary->include_mip || summary->distance_trajectory_count > 0);
    if (summary->distance_trajectory_count > 0) {
        gsp_write_summary_distance_json(fp, child, summary->has_baseline,
                                        summary->baseline_distance_nm,
                                        summary->distance_trajectory_nm,
                                        summary->distance_trajectory_count,
                                        summary->final_distance_nm,
                                        summary->include_runtime || summary->include_mip);
    }
    if (summary->include_runtime) {
        gsp_write_summary_runtime_json(fp, child, summary->preprocessing_seconds,
                                       summary->solution_runtime_seconds,
                                       summary->solution_runtime_count,
                                       summary->postprocessing_seconds,
                                       summary->grandtotal_seconds,
                                       summary->include_mip);
    }
    if (summary->include_mip) {
        gsp_write_summary_mip_json(fp, child, summary->mip_solve_count,
                                   summary->mip_runtime_mean, summary->mip_runtime_max,
                                   summary->mip_gap_mean, summary->mip_gap_max, 0);
    }
    fprintf(fp, "%s}%s\n", base, trailing_comma ? "," : "");
}

int gsp_build_dock_location_ids_from_segment_ends(int boat_start_loc_id,
                                                  const int *segment_end_location_ids,
                                                  int segment_count,
                                                  int **out_dock_location_ids,
                                                  int *out_dock_location_count) {
    int *dock_location_ids = NULL;

    if (out_dock_location_ids) *out_dock_location_ids = NULL;
    if (out_dock_location_count) *out_dock_location_count = 0;
    if (segment_count < 0) return 0;
    if (segment_count > 0 && !segment_end_location_ids) return 0;

    dock_location_ids = (int*)malloc((size_t)(segment_count + 1) * sizeof(int));
    if (!dock_location_ids) return 0;

    dock_location_ids[0] = boat_start_loc_id;
    for (int s = 0; s < segment_count; s++) {
        dock_location_ids[s + 1] = segment_end_location_ids[s];
    }

    if (out_dock_location_ids) *out_dock_location_ids = dock_location_ids;
    else free(dock_location_ids);
    if (out_dock_location_count) *out_dock_location_count = segment_count + 1;
    return 1;
}
