#include "mip_report.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void gsp_mip_solve_detail_init(gsp_mip_solve_detail_t *detail) {
    if (!detail) return;
    memset(detail, 0, sizeof(*detail));
    detail->pass_index = -1;
    detail->boundary_index = -1;
    detail->candidate_split_index = -1;
    detail->segment_index = -1;
    detail->segment_role = 0;
}

int gsp_append_mip_solve_detail(gsp_mip_solve_detail_t **arr,
                                int *count,
                                int *capacity,
                                const gsp_mip_solve_detail_t *detail) {
    if (!arr || !count || !capacity || !detail) return 0;
    if (*count >= *capacity) {
        int new_capacity = (*capacity <= 0) ? 16 : (*capacity * 2);
        gsp_mip_solve_detail_t *tmp = (gsp_mip_solve_detail_t*)realloc(
            *arr, (size_t)new_capacity * sizeof(gsp_mip_solve_detail_t));
        if (!tmp) return 0;
        *arr = tmp;
        *capacity = new_capacity;
    }
    (*arr)[*count] = *detail;
    (*count)++;
    return 1;
}

void gsp_compute_mip_summary(const gsp_mip_solve_detail_t *details,
                             int detail_count,
                             double *runtime_mean,
                             double *runtime_max,
                             double *gap_mean,
                             double *gap_max) {
    double runtime_sum = 0.0;
    double gap_sum = 0.0;
    int count = 0;

    if (runtime_mean) *runtime_mean = -1.0;
    if (runtime_max) *runtime_max = -1.0;
    if (gap_mean) *gap_mean = -1.0;
    if (gap_max) *gap_max = -1.0;

    if (!details || detail_count <= 0) return;

    for (int i = 0; i < detail_count; i++) {
        runtime_sum += details[i].runtime_seconds;
        gap_sum += details[i].gap_percent;
        if (runtime_max && (count == 0 || details[i].runtime_seconds > *runtime_max)) {
            *runtime_max = details[i].runtime_seconds;
        }
        if (gap_max && (count == 0 || details[i].gap_percent > *gap_max)) {
            *gap_max = details[i].gap_percent;
        }
        count++;
    }

    if (count > 0) {
        if (runtime_mean) *runtime_mean = runtime_sum / (double)count;
        if (gap_mean) *gap_mean = gap_sum / (double)count;
    }
}

void gsp_write_json_double_or_null(FILE *fp, double value) {
    if (!fp) return;
    if (value < 0.0 || isnan(value)) fprintf(fp, "null");
    else fprintf(fp, "%.6f", value);
}

static void gsp_write_mip_header(FILE *fp,
                                 const char *phase,
                                 const char *model_name,
                                 double timeout_seconds) {
    if (!fp) return;
    fprintf(fp, "  \"mip\": {\n");
    fprintf(fp, "    \"phase\": \"%s\",\n", phase ? phase : "unknown");
    fprintf(fp, "    \"model\": \"%s\",\n", model_name ? model_name : "unknown");
    fprintf(fp, "    \"timeout_seconds\": %.6f,\n", timeout_seconds);
}

void gsp_write_segment_mip_section(FILE *fp,
                                   const char *phase,
                                   const char *model_name,
                                   double timeout_seconds,
                                   const gsp_mip_solve_detail_t *details,
                                   int detail_count) {
    if (!fp) return;
    gsp_write_mip_header(fp, phase, model_name, timeout_seconds);
    fprintf(fp, "    \"solve_detail_tuple\": [\"size\", \"runtime_seconds\", \"gap_percent\"],\n");
    fprintf(fp, "    \"solves\": [");
    for (int i = 0; i < detail_count; i++) {
        const gsp_mip_solve_detail_t *detail = &details[i];
        if (i) fprintf(fp, ", ");
        fprintf(fp, "[%d, %.6f, %.6f]",
                detail->station_count,
                detail->runtime_seconds,
                detail->gap_percent);
    }
    fprintf(fp, "]\n");
    fprintf(fp, "  },\n");
}

void gsp_write_boundary_mip_section(FILE *fp,
                                    const char *phase,
                                    const char *model_name,
                                    double timeout_seconds,
                                    const gsp_mip_solve_detail_t *details,
                                    int detail_count) {
    if (!fp) return;
    gsp_write_mip_header(fp, phase, model_name, timeout_seconds);
    fprintf(fp, "    \"solve_detail_tuple\": [\"pass_index\", \"boundary_index\", \"candidate_split_index\", \"segment_index\", \"segment_role\", \"station_count\", \"node_count\", \"moved_stations\", \"mip_size\", \"runtime_seconds\", \"gap_percent\"],\n");
    fprintf(fp, "    \"solves\": [");
    for (int i = 0; i < detail_count; i++) {
        const gsp_mip_solve_detail_t *detail = &details[i];
        if (i) fprintf(fp, ", ");
        fprintf(fp, "[%d, %d, %d, %d, %d, %d, %d, %d, [%d, %d], %.6f, %.6f]",
                detail->pass_index,
                detail->boundary_index,
                detail->candidate_split_index,
                detail->segment_index,
                detail->segment_role,
                detail->station_count,
                detail->node_count,
                detail->moved_stations,
                detail->model_num_vars,
                detail->model_num_constrs,
                detail->runtime_seconds,
                detail->gap_percent);
    }
    fprintf(fp, "]\n");
    fprintf(fp, "  },\n");
}
