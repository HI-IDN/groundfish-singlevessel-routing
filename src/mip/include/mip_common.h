#ifndef GSP_MIP_COMMON_H
#define GSP_MIP_COMMON_H

#include <stddef.h>

#include "../../include/constants.h"
#include <gurobi_c.h>

typedef struct {
    double time_limit_seconds;
    int thread_count;
    int verbose;
    double mip_gap;
    int heuristic_only;
    GRBenv *shared_env;
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

const char *mip_gurobi_status_name(int status);
int mip_status_allows_incumbent(int status);
void mip_apply_common_params(GRBmodel *model, const mip_params_t *params);
void *mip_xmalloc(size_t nbytes);
void *mip_xcalloc(size_t count, size_t size);

#endif
