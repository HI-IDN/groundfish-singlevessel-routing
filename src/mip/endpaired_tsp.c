#include "include/mip_endpaired_tsp.h"
#include "include/mip_paired_tour.h"

#include <stdlib.h>
#include <string.h>

static int find_station_local_index(const mip_endpaired_instance_t *instance, int station_id) {
    if (!instance || !instance->station_ids) return -1;
    for (int i = 0; i < instance->num_stations; i++) {
        if (instance->station_ids[i] == station_id) return i;
    }
    return -1;
}

static int apply_warm_start(GRBmodel *model,
                            const mip_endpaired_instance_t *instance,
                            int size,
                            const mip_params_t *params) {
    int n;
    double *start = NULL;
    int *seen = NULL;
    int prev_out = 1;
    int error = 0;

    if (!model || !instance || !params || !params->warm_start_station_ids) return 0;
    if (params->warm_start_order_length != instance->num_stations) return 0;

    n = 2 * size;
    start = (double*)mip_xcalloc((size_t)n * (size_t)n, sizeof(double));
    seen = (int*)mip_xcalloc((size_t)instance->num_stations, sizeof(int));

    start[0 * n + 1] = 1.0;

    for (int i = 0; i < params->warm_start_order_length; i++) {
        int signed_station_id = params->warm_start_station_ids[i];
        int local_idx = find_station_local_index(instance, abs(signed_station_id));
        int seg_idx;
        int entry_node;
        int exit_node;
        int next_in;

        if (local_idx < 0 || seen[local_idx]) {
            error = 1;
            goto quit;
        }
        seen[local_idx] = 1;

        seg_idx = local_idx + 1;
        entry_node = 2 * seg_idx;
        exit_node = 2 * seg_idx + 1;
        if (signed_station_id < 0) {
            next_in = exit_node;
            exit_node = entry_node;
        } else {
            next_in = entry_node;
        }

        start[prev_out * n + next_in] = 1.0;
        start[next_in * n + exit_node] = 1.0;
        prev_out = exit_node;
    }

    start[prev_out * n + 0] = 1.0;

    for (int i = 0; i < n * n; i++) {
        error = GRBsetdblattrelement(model, GRB_DBL_ATTR_START, i, start[i]);
        if (error) goto quit;
    }

quit:
    free(start);
    free(seen);
    return error;
}

static double lookup_distance_nm(const mip_endpaired_instance_t *instance, int from_loc_id, int to_loc_id) {
    if (!instance || !instance->distances) return 1e12;
    if (from_loc_id < 0 || from_loc_id >= instance->max_location_id) return 1e12;
    if (to_loc_id < 0 || to_loc_id >= instance->max_location_id) return 1e12;
    if (from_loc_id == to_loc_id) return 0.0;

    {
        double value = instance->distances[from_loc_id][to_loc_id];
        if (value >= 0.0) return value;
    }
    return 1e12;
}

static int solve_tsp_distance(GRBenv *env,
                              const mip_endpaired_instance_t *instance,
                              const double *dist,
                              int size,
                              const mip_params_t *params,
                              double *out_obj,
                              int **out_tour,
                              int *out_len,
                              int *out_status,
                              int *out_solver_error,
                              double *out_runtime,
                              double *out_gap) {
    int n = 2 * size;
    GRBmodel *model = NULL;
    int error = 0;
    int optimize_error = 0;
    int *ind = NULL;
    double *val = NULL;
    int solcount = 0;
    int status = 0;
    double runtime = 0.0;
    double gap = 0.0;

    error = GRBnewmodel(env, &model, "endpaired_tsp", 0, NULL, NULL, NULL, NULL, NULL);
    if (error) goto quit;

    mip_apply_common_params(model, params);

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            error = GRBaddvar(model, 0, NULL, NULL, dist[i * n + j], 0.0, 1.0, GRB_BINARY, NULL);
            if (error) goto quit;
        }
    }

    ind = (int*)mip_xmalloc((size_t)n * sizeof(int));
    val = (double*)mip_xmalloc((size_t)n * sizeof(double));
    for (int i = 0; i < n; i++) val[i] = 1.0;

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) ind[j] = i * n + j;
        error = GRBaddconstr(model, n, ind, val, GRB_EQUAL, 1.0, NULL);
        if (error) goto quit;
    }
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) ind[i] = i * n + j;
        error = GRBaddconstr(model, n, ind, val, GRB_EQUAL, 1.0, NULL);
        if (error) goto quit;
    }

    for (int i = 0; i < size; i++) {
        int a = 2 * i;
        int b = 2 * i + 1;
        ind[0] = a * n + b;
        ind[1] = b * n + a;
        val[0] = 1.0;
        val[1] = 1.0;
        error = GRBaddconstr(model, 2, ind, val, GRB_EQUAL, 1.0, NULL);
        if (error) goto quit;
    }

    for (int i = 0; i < n; i++) {
        error = GRBsetdblattrelement(model, GRB_DBL_ATTR_UB, i * n + i, 0.0);
        if (error) goto quit;
    }

    error = apply_warm_start(model, instance, size, params);
    if (error) goto quit;

    {
        mip_callback_data_t cb;
        cb.n = n;
        cb.numvars = n * n;
        error = GRBsetcallbackfunc(model, mip_subtourelim_directed, (void*)&cb);
        if (error) goto quit;
        error = GRBsetintparam(GRBgetenv(model), GRB_INT_PAR_LAZYCONSTRAINTS, 1);
        if (error) goto quit;
        optimize_error = GRBoptimize(model);
    }

    if (GRBgetintattr(model, GRB_INT_ATTR_STATUS, &status) != 0) status = 0;
    if (GRBgetintattr(model, GRB_INT_ATTR_SOLCOUNT, &solcount) != 0) solcount = 0;
    if (GRBgetdblattr(model, GRB_DBL_ATTR_RUNTIME, &runtime) != 0) runtime = 0.0;
    if (GRBgetdblattr(model, GRB_DBL_ATTR_MIPGAP, &gap) != 0) gap = 0.0;

    if (out_status) *out_status = status;
    if (out_solver_error) *out_solver_error = optimize_error;
    if (out_runtime) *out_runtime = runtime;
    if (out_gap) *out_gap = gap;

    if (mip_status_allows_incumbent(status) && solcount > 0) {
        error = GRBgetdblattr(model, GRB_DBL_ATTR_OBJVAL, out_obj);
        if (error) goto quit;
        if (out_tour && out_len) {
            double *sol = (double*)mip_xmalloc((size_t)n * (size_t)n * sizeof(double));
            error = GRBgetdblattrarray(model, GRB_DBL_ATTR_X, 0, n * n, sol);
            if (error) {
                free(sol);
                goto quit;
            }
            *out_tour = (int*)mip_xmalloc((size_t)n * sizeof(int));
            *out_len = 0;
            mip_findsubtour_directed(n, sol, out_len, *out_tour);
            free(sol);
        }
        error = 0;
    } else {
        error = optimize_error ? optimize_error : 1;
    }

quit:
    free(ind);
    free(val);
    if (model) GRBfreemodel(model);
    return error;
}

int solve_mip_endpaired_tsp(const mip_endpaired_instance_t *instance,
                            const mip_params_t *params,
                            int start_loc_id,
                            int end_loc_id,
                            mip_endpaired_solution_t *solution) {
    mip_params_t local_params;
    GRBenv *env = NULL;
    int owns_env = 0;
    double *dist = NULL;
    int *entry = NULL;
    int *exit = NULL;
    int *node_tour = NULL;
    int node_len = 0;
    int seg_size;
    int n;
    int error = 0;

    if (!instance || !solution) return 1;
    memset(solution, 0, sizeof(*solution));

    local_params = params ? *params : (mip_params_t){0};

    seg_size = 1 + instance->num_stations;
    n = 2 * seg_size;
    dist = (double*)mip_xmalloc((size_t)n * (size_t)n * sizeof(double));
    entry = (int*)mip_xmalloc((size_t)seg_size * sizeof(int));
    exit = (int*)mip_xmalloc((size_t)seg_size * sizeof(int));

    entry[0] = start_loc_id;
    exit[0] = end_loc_id;
    for (int i = 0; i < instance->num_stations; i++) {
        entry[i + 1] = instance->station_start_loc_ids[i];
        exit[i + 1] = instance->station_end_loc_ids[i];
        if (instance->station_amounts) solution->catch_amount += instance->station_amounts[i];
    }

    for (int i = 0; i < seg_size; i++) {
        for (int j = 0; j < seg_size; j++) {
            int from_entry = entry[i];
            int from_exit = exit[i];
            int to_entry = entry[j];
            int to_exit = exit[j];
            dist[(2 * i + 0) * n + (2 * j + 0)] = lookup_distance_nm(instance, from_entry, to_entry);
            dist[(2 * i + 0) * n + (2 * j + 1)] = lookup_distance_nm(instance, from_entry, to_exit);
            dist[(2 * i + 1) * n + (2 * j + 0)] = lookup_distance_nm(instance, from_exit, to_entry);
            dist[(2 * i + 1) * n + (2 * j + 1)] = lookup_distance_nm(instance, from_exit, to_exit);
        }
    }

    if (local_params.exclude_haul_distance) {
        for (int i = 1; i < seg_size; i++) {
            dist[(2 * i + 0) * n + (2 * i + 1)] = 0.0;
            dist[(2 * i + 1) * n + (2 * i + 0)] = 0.0;
        }
    }

    dist[1 * n + 0] = 0.0;
    dist[0 * n + 1] = 0.0;

    env = local_params.shared_env;
    if (!env) {
        error = GRBloadenv(&env, NULL);
        if (error) goto quit;
        owns_env = 1;
    }

    error = solve_tsp_distance(env, instance, dist, seg_size, &local_params,
                               &solution->objective_value,
                               &node_tour,
                               &node_len,
                               &solution->status,
                               &solution->solver_error,
                               &solution->runtime_seconds,
                               &solution->gap);
    if (error) goto quit;

    solution->total_distance_nm = solution->objective_value;
    solution->model_num_vars = n * n;
    solution->model_num_constrs = 5 * seg_size;

    if (instance->num_stations > 0 && node_tour) {
        int letour_len = 0;
        int *letour = NULL;

        mip_orient_node_tour(node_tour, node_len);
        letour = mip_node_tour_to_letour(node_tour, node_len, seg_size, &letour_len);
        solution->signed_station_ids = (int*)mip_xmalloc((size_t)instance->num_stations * sizeof(int));
        solution->order_length = 0;

        for (int i = 0; i < letour_len; i++) {
            int token = letour[i];
            int local_idx;
            int sign;
            if (token == 0) continue;
            sign = (token < 0) ? -1 : 1;
            local_idx = abs(token) - 1;
            if (local_idx < 0 || local_idx >= instance->num_stations) continue;
            solution->signed_station_ids[solution->order_length++] =
                sign * instance->station_ids[local_idx];
        }
        free(letour);
    }

quit:
    if (error) {
        free_mip_endpaired_solution(solution);
        solution->status = 0;
        solution->solver_error = error;
    }
    free(node_tour);
    free(dist);
    free(entry);
    free(exit);
    if (owns_env && env) GRBfreeenv(env);
    return error;
}

void free_mip_endpaired_solution(mip_endpaired_solution_t *solution) {
    if (!solution) return;
    free(solution->signed_station_ids);
    memset(solution, 0, sizeof(*solution));
}
