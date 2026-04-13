#ifndef GREEDY_INSERTION_H
#define GREEDY_INSERTION_H

#include "../include/init_types.h"

int gi_solve(const nn_instance_t *inst, nn_solution_t *sol,
             nn_solution_t *pre_capacity_sol,
             int boat_start_loc_id, int boat_end_loc_id,
             int boat_capacity);

#endif
