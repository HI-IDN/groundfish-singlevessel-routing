#ifndef GSP_MIP_ENDPAIRED_TSP_H
#define GSP_MIP_ENDPAIRED_TSP_H

#include "mip_common.h"

typedef struct {
    int num_stations;
    const int *station_ids;
    const int *station_start_loc_ids;
    const int *station_end_loc_ids;
    const int *station_amounts;
    double **distances;
    int max_location_id;
} mip_endpaired_instance_t;

typedef struct {
    int *signed_station_ids;
    int order_length;
    int catch_amount;
    double total_distance_nm;
    double objective_value;
    int status;
    double gap;
    double runtime_seconds;
} mip_endpaired_solution_t;

int solve_mip_endpaired_tsp(const mip_endpaired_instance_t *instance,
                            const mip_params_t *params,
                            int start_loc_id,
                            int end_loc_id,
                            mip_endpaired_solution_t *solution);

void free_mip_endpaired_solution(mip_endpaired_solution_t *solution);

#endif
