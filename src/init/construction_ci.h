#ifndef CHEAPEST_INSERTION_H
#define CHEAPEST_INSERTION_H

#include "../include/init_types.h"

int ci_solve(const nn_instance_t *inst, nn_solution_t *sol,
             nn_solution_t *pre_capacity_sol,
             int boat_start_loc_id, int boat_end_loc_id,
             int boat_capacity);

int ci_construction_solve(const nn_instance_t *inst, nn_solution_t *sol,
                          int boat_start_loc_id, int boat_end_loc_id);

#endif

