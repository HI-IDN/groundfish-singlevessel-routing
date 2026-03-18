#include "include/mip_noport.h"
#include "include/mip_endpaired_tsp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int solve_mip_noport(const mip_noport_instance_t *instance,
                     const mip_noport_params_t *params,
                     mip_noport_solution_t *solution) {
    mip_endpaired_instance_t endpaired;
    mip_endpaired_solution_t endpaired_solution;
    int *station_ids = NULL;
    int *start_loc_ids = NULL;
    int *end_loc_ids = NULL;
    int *amounts = NULL;
    int error = 0;

    if (!instance || !instance->boat || !solution) return 1;

    memset(solution, 0, sizeof(*solution));
    memset(&endpaired, 0, sizeof(endpaired));
    memset(&endpaired_solution, 0, sizeof(endpaired_solution));

    if (instance->n_stations > 0) {
        station_ids = (int*)mip_xmalloc((size_t)instance->n_stations * sizeof(int));
        start_loc_ids = (int*)mip_xmalloc((size_t)instance->n_stations * sizeof(int));
        end_loc_ids = (int*)mip_xmalloc((size_t)instance->n_stations * sizeof(int));
        amounts = (int*)mip_xmalloc((size_t)instance->n_stations * sizeof(int));
    }

    for (int i = 0; i < instance->n_stations; i++) {
        station_ids[i] = instance->stations[i].station_id;
        start_loc_ids[i] = instance->stations[i].start_location_id;
        end_loc_ids[i] = instance->stations[i].end_location_id;
        amounts[i] = instance->stations[i].amount;
    }

    endpaired.num_stations = instance->n_stations;
    endpaired.station_ids = station_ids;
    endpaired.station_start_loc_ids = start_loc_ids;
    endpaired.station_end_loc_ids = end_loc_ids;
    endpaired.station_amounts = amounts;
    endpaired.distances = instance->distances;
    endpaired.max_location_id = instance->max_location_id;

    error = solve_mip_endpaired_tsp(&endpaired, params,
                                    instance->boat->start_location_id,
                                    instance->boat->end_location_id,
                                    &endpaired_solution);
    if (error) {
        solution->status = MIP_STATUS_INFEASIBLE;
    } else {
        solution->signed_station_ids = endpaired_solution.signed_station_ids;
        solution->order_length = endpaired_solution.order_length;
        solution->total_distance_nm = endpaired_solution.total_distance_nm;
        solution->objective_value = endpaired_solution.objective_value;
        solution->status = endpaired_solution.status;
        solution->gap = endpaired_solution.gap;
        solution->runtime_seconds = endpaired_solution.runtime_seconds;
        endpaired_solution.signed_station_ids = NULL;
    }

    if (params && params->verbose) {
        fprintf(stderr,
                "No-port solve summary: status=%s(%d) runtime=%.2f s gap=%.6f order_length=%d\n",
                mip_gurobi_status_name(solution->status),
                solution->status,
                solution->runtime_seconds,
                solution->gap,
                solution->order_length);
    }

    free_mip_endpaired_solution(&endpaired_solution);
    free(station_ids);
    free(start_loc_ids);
    free(end_loc_ids);
    free(amounts);
    return error;
}

void free_mip_noport_solution(mip_noport_solution_t *solution) {
    if (!solution) return;
    free(solution->signed_station_ids);
    memset(solution, 0, sizeof(*solution));
}
