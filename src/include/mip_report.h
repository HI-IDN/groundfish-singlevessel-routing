#ifndef GSP_MIP_REPORT_H
#define GSP_MIP_REPORT_H

#include <stdio.h>

typedef struct {
    int pass_index;
    int boundary_index;
    int candidate_split_index;
    int segment_index;
    int segment_role;
    int station_count;
    int node_count;
    int moved_stations;
    int model_num_vars;
    int model_num_constrs;
    double runtime_seconds;
    double gap_percent;
} gsp_mip_solve_detail_t;

void gsp_mip_solve_detail_init(gsp_mip_solve_detail_t *detail);

int gsp_append_mip_solve_detail(gsp_mip_solve_detail_t **arr,
                                int *count,
                                int *capacity,
                                const gsp_mip_solve_detail_t *detail);

void gsp_compute_mip_summary(const gsp_mip_solve_detail_t *details,
                             int detail_count,
                             double *runtime_mean,
                             double *runtime_max,
                             double *gap_mean,
                             double *gap_max);

void gsp_write_json_double_or_null(FILE *fp, double value);

void gsp_write_segment_mip_section(FILE *fp,
                                   const char *phase,
                                   const char *model_name,
                                   double timeout_seconds,
                                   const gsp_mip_solve_detail_t *details,
                                   int detail_count);

void gsp_write_boundary_mip_section(FILE *fp,
                                    const char *phase,
                                    const char *model_name,
                                    double timeout_seconds,
                                    const gsp_mip_solve_detail_t *details,
                                    int detail_count);

#endif
