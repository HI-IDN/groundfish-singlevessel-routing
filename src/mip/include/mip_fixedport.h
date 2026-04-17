#ifndef GSP_MIP_FIXEDPORT_H
#define GSP_MIP_FIXEDPORT_H

#include "mip_common.h"
#include "../../include/dat_parser.h"

typedef struct {
    const Boat *boat;
    const Station *stations;
    int n_stations;
    const int *candidate_port_location_ids;
    int candidate_port_count;
    double **distances;
    int max_location_id;
} mip_fixedport_instance_t;

typedef struct {
    int *signed_visit_ids;
    int visit_count;
    double objective_value;
    int status;
    int solver_error;
    double gap;
    double runtime_seconds;
} mip_fixedport_solution_t;

int solve_mip_fixedport(const mip_fixedport_instance_t *instance,
                        const mip_params_t *params,
                        mip_fixedport_solution_t *solution);

void free_mip_fixedport_solution(mip_fixedport_solution_t *solution);

#endif
