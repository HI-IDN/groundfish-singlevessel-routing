#ifndef GSP_MIP_CAPACITY_AWARE_H
#define GSP_MIP_CAPACITY_AWARE_H

#include "../../include/data_types.h"

typedef struct {
    int num_nodes;
    double *dist_matrix;
    int *feasible_matrix;
    int *node_type;
    double *demand;
    double vessel_capacity;
} mip_instance_t;

typedef struct {
    double time_limit_seconds;
    int thread_count;
    int verbose;
    double mip_gap;
    int heuristic_only;
} mip_params_t;

typedef struct {
    int *tour;
    int tour_length;
    double total_distance;
    double obj_value;
    int status;
    double gap;
    double runtime_seconds;
    int solver_iterations;
} mip_solution_t;

int solve_mip_capacity_aware(const mip_instance_t *instance, const mip_params_t *params, mip_solution_t *solution);

#endif

