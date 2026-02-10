#ifndef GSP_OUTPUT_H
#define GSP_OUTPUT_H

#include "data_types.h"

char *serialize_solution_json(const init_result_t *sol);
char *serialize_solution_csv(const init_result_t *sol);
char *serialize_trajectory_json(const trajectory_point_t *traj, int len, const int *best_tour);
int write_solution_json(const char *filepath, const init_result_t *sol);
int append_trajectory_csv(const char *filepath, const trajectory_point_t *point);

#endif

