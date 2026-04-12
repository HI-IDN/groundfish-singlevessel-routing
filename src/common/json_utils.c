#include "../include/json_utils.h"

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
    gsp_write_distance_nm_json(fp, child, view->segment_breakdowns, view->segment_count,
                               view->grand_total, 1);
    fprintf(fp, "%s  \"feasible\": %s\n", base, view->feasible ? "true" : "false");
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
