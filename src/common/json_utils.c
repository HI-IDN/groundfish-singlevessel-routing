#include "../include/json_utils.h"

static void write_double_array(FILE *fp, const double *values, int count) {
    for (int i = 0; i < count; i++) {
        if (i) fprintf(fp, ", ");
        fprintf(fp, "%.6f", values ? values[i] : 0.0);
    }
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
