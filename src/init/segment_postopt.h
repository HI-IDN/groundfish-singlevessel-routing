#ifndef GSP_INIT_LOCAL_POSTOPT_H
#define GSP_INIT_LOCAL_POSTOPT_H

#include "../include/init_types.h"
#include "../include/mip_report.h"

int segment_copy_solution(const nn_solution_t *src, nn_solution_t *dst);
void segment_free_solution(nn_solution_t *sol);
double read_segment_mip_time_limit_from_yaml(const char *yaml_path);
double read_noport_mip_time_limit_from_yaml(const char *yaml_path);
double read_sweep_mip_time_limit_from_yaml(const char *yaml_path);
double read_fixedport_mip_time_limit_from_yaml(const char *yaml_path);
double read_noport_haul_distance_scale_from_yaml(const char *yaml_path);
double read_segment_haul_distance_scale_from_yaml(const char *yaml_path);
double read_sweep_haul_distance_scale_from_yaml(const char *yaml_path);
double read_fixedport_haul_distance_scale_from_yaml(const char *yaml_path);
int segment_apply_local_postopt(const nn_instance_t *inst,
                                const nn_solution_t *input,
                                int boat_start_loc_id,
                                int boat_end_loc_id,
                                double time_limit_seconds,
                                double haul_distance_scale,
                                nn_solution_t *output,
                                double *runtime_seconds_out,
                                int *segment_solve_count_out,
                                gsp_mip_solve_detail_t **solve_details_out,
                                int *solve_detail_count_out);

#endif
