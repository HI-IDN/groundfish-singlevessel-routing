#ifndef GSP_MIP_NOPORT_H
#define GSP_MIP_NOPORT_H

#include "mip_common.h"
#include "constants.h"
#include "dat_parser.h"

typedef struct {
    const Boat *boat;
    const Station *stations;
    int n_stations;
    double **distances;
    int max_location_id;
} mip_noport_instance_t;

typedef mip_params_t mip_noport_params_t;

typedef struct {
    int *signed_station_ids;
    int order_length;
    double total_distance_nm;
    double objective_value;
    int status;
    int solver_error;
    double gap;
    double runtime_seconds;
} mip_noport_solution_t;

int solve_mip_noport(const mip_noport_instance_t *instance,
                     const mip_noport_params_t *params,
                     mip_noport_solution_t *solution);

void free_mip_noport_solution(mip_noport_solution_t *solution);

#endif
