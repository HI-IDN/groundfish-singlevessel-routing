#ifndef NEAREST_NEIGHBOR_H
#define NEAREST_NEIGHBOR_H

#include "../include/init_types.h"

/* Solve NN heuristic with capacity-aware segmentation using pre-loaded distance matrix */
int nn_solve(const nn_instance_t *inst, nn_solution_t *sol,
             int boat_start_loc_id, int boat_end_loc_id,
             int boat_capacity);

#endif
