#ifndef GSP_MIP_CAPACITY_AWARE_H
#define GSP_MIP_CAPACITY_AWARE_H

#include "mip_common.h"

typedef struct {
    int num_nodes;
    double *dist_matrix;
    int *feasible_matrix;
    int *node_type;
    double *demand;
    double vessel_capacity;
} mip_capacity_aware_instance_t;

int solve_mip_capacity_aware(const mip_capacity_aware_instance_t *instance,
                             const mip_params_t *params,
                             mip_solution_t *solution);

#endif

