#ifndef GSP_INIT_LOCAL_POSTOPT_H
#define GSP_INIT_LOCAL_POSTOPT_H

#include "../include/init_types.h"

int init_copy_solution(const nn_solution_t *src, nn_solution_t *dst);
void init_free_solution(nn_solution_t *sol);
double read_init_local_postopt_time_limit_from_yaml(const char *yaml_path);
int init_apply_local_postopt(const nn_instance_t *inst,
                             const nn_solution_t *input,
                             int boat_start_loc_id,
                             int boat_end_loc_id,
                             double time_limit_seconds,
                             nn_solution_t *output,
                             double *runtime_seconds_out,
                             int *segment_solve_count_out);

#endif
