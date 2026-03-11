#ifndef GREEDY_INSERTION_H
#define GREEDY_INSERTION_H

#include "nearest_neighbor.h"

int gi_solve(const nn_instance_t *inst, nn_solution_t *sol,
             int boat_start_loc_id, int boat_end_loc_id,
             int boat_capacity);

#endif
