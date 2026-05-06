#include "include/mip_fixedport.h"
#include "include/mip_paired_tour.h"

#include <stdlib.h>
#include <string.h>

static double lookup_distance_nm(const mip_fixedport_instance_t *instance, int from_loc_id, int to_loc_id) {
    if (!instance || !instance->distances) return 1e12;
    if (from_loc_id < 0 || from_loc_id >= instance->max_location_id) return 1e12;
    if (to_loc_id < 0 || to_loc_id >= instance->max_location_id) return 1e12;
    if (from_loc_id == to_loc_id) return 0.0;
    if (instance->distances[from_loc_id][to_loc_id] >= 0.0) {
        return instance->distances[from_loc_id][to_loc_id];
    }
    return 1e12;
}

int solve_mip_fixedport(const mip_fixedport_instance_t *instance,
                        const mip_params_t *params,
                        mip_fixedport_solution_t *solution) {
    mip_params_t local_params;
    GRBenv *env = NULL;
    int owns_env = 0;
    GRBmodel *model = NULL;
    int size;
    int n;
    int *entry = NULL;
    int *exit = NULL;
    int *amount = NULL;
    int *is_port = NULL;
    double *dist = NULL;
    int *ind = NULL;
    double *val = NULL;
    int *tour = NULL;
    int tour_len = 0;
    int *letour = NULL;
    int letour_len = 0;
    double *sol = NULL;
    int error = 0;
    int optimize_error = 0;
    int status = 0;
    int solcount = 0;
    double runtime = 0.0;
    double gap = 0.0;
    double obj = 0.0;
    double haul_distance_multiplier = 1.0;

    if (!instance || !instance->boat || !solution) return 1;
    memset(solution, 0, sizeof(*solution));

    local_params = params ? *params : (mip_params_t){0};
    size = 1 + instance->n_stations + instance->candidate_port_count;
    n = 2 * size;

    entry = (int*)mip_xmalloc((size_t)size * sizeof(int));
    exit = (int*)mip_xmalloc((size_t)size * sizeof(int));
    amount = (int*)mip_xcalloc((size_t)size, sizeof(int));
    is_port = (int*)mip_xcalloc((size_t)size, sizeof(int));
    dist = (double*)mip_xmalloc((size_t)n * (size_t)n * sizeof(double));

    entry[0] = instance->boat->location_id;
    exit[0] = (instance->end_location_id > 0)
        ? instance->end_location_id
        : instance->boat->location_id;
    is_port[0] = 1;

    for (int i = 0; i < instance->n_stations; i++) {
        int city = i + 1;
        entry[city] = instance->stations[i].start_location_id;
        exit[city] = instance->stations[i].end_location_id;
        amount[city] = instance->stations[i].amount;
    }
    for (int p = 0; p < instance->candidate_port_count; p++) {
        int city = 1 + instance->n_stations + p;
        int loc_id = instance->candidate_port_location_ids[p];
        entry[city] = loc_id;
        exit[city] = loc_id;
        is_port[city] = 1;
    }

    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
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

    dist[0 * n + 1] = 0.0;
    dist[1 * n + 0] = 0.0;

    for (int city = 1; city <= instance->n_stations; city++) {
        dist[(2 * city + 0) * n + (2 * city + 1)] = 0.0;
        dist[(2 * city + 1) * n + (2 * city + 0)] = 0.0;
    }

    if (local_params.shared_env) {
        env = local_params.shared_env;
    } else {
        error = GRBloadenv(&env, NULL);
        if (error) goto quit;
        owns_env = 1;
    }

    error = GRBnewmodel(env, &model, "fixedport_capacity", 0, NULL, NULL, NULL, NULL, NULL);
    if (error) goto quit;
    mip_apply_common_params(model, &local_params);

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            error = GRBaddvar(model, 0, NULL, NULL, dist[i * n + j], 0.0, 1.0, GRB_BINARY, NULL);
            if (error) goto quit;
        }
    }

    {
        int xcount = n * n;
        int capacity = instance->boat->capacity;
        int w_offset = xcount;
        int v_offset = xcount + xcount;
        int *ind2 = NULL;
        double *val2 = NULL;

        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                error = GRBaddvar(model, 0, NULL, NULL, 0.0, 0.0, (double)capacity, GRB_CONTINUOUS, NULL);
                if (error) goto quit;
            }
        }
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                int city = i / 2;
                int allow_v = (i != j) && (city == j / 2) && !is_port[city];
                double ub = allow_v ? (double)capacity : 0.0;
                error = GRBaddvar(model, 0, NULL, NULL, 0.0, 0.0, ub, GRB_CONTINUOUS, NULL);
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

        for (int city = 0; city < size; city++) {
            int a = 2 * city;
            int b = 2 * city + 1;
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

        ind2 = (int*)mip_xmalloc((size_t)(n + 4) * sizeof(int));
        val2 = (double*)mip_xmalloc((size_t)(n + 4) * sizeof(double));

        for (int city = 0; city < size; city++) {
            int a;
            int b;
            int nnz;
            if (is_port[city]) continue;
            a = 2 * city;
            b = 2 * city + 1;

            nnz = 0;
            for (int k = 0; k < n; k++) {
                if (k == b) continue;
                ind2[nnz] = w_offset + k * n + a;
                val2[nnz++] = 1.0;
            }
            ind2[nnz] = v_offset + a * n + b;
            val2[nnz++] = 1.0;
            ind2[nnz] = w_offset + a * n + b;
            val2[nnz++] = -1.0;
            error = GRBaddconstr(model, nnz, ind2, val2, GRB_EQUAL, 0.0, NULL);
            if (error) goto quit;

            nnz = 0;
            for (int k = 0; k < n; k++) {
                if (k == a) continue;
                ind2[nnz] = w_offset + k * n + b;
                val2[nnz++] = 1.0;
            }
            ind2[nnz] = v_offset + b * n + a;
            val2[nnz++] = 1.0;
            ind2[nnz] = w_offset + b * n + a;
            val2[nnz++] = -1.0;
            error = GRBaddconstr(model, nnz, ind2, val2, GRB_EQUAL, 0.0, NULL);
            if (error) goto quit;

            ind2[0] = v_offset + a * n + b;
            val2[0] = 1.0;
            ind2[1] = v_offset + b * n + a;
            val2[1] = 1.0;
            error = GRBaddconstr(model, 2, ind2, val2, GRB_EQUAL, (double)amount[city], NULL);
            if (error) goto quit;

            nnz = 0;
            ind2[nnz] = w_offset + a * n + b;
            val2[nnz++] = 1.0;
            for (int k = 0; k < n; k++) {
                if (k == a || k == b) continue;
                ind2[nnz] = w_offset + b * n + k;
                val2[nnz++] = -1.0;
            }
            error = GRBaddconstr(model, nnz, ind2, val2, GRB_EQUAL, 0.0, NULL);
            if (error) goto quit;

            nnz = 0;
            ind2[nnz] = w_offset + b * n + a;
            val2[nnz++] = 1.0;
            for (int k = 0; k < n; k++) {
                if (k == a || k == b) continue;
                ind2[nnz] = w_offset + a * n + k;
                val2[nnz++] = -1.0;
            }
            error = GRBaddconstr(model, nnz, ind2, val2, GRB_EQUAL, 0.0, NULL);
            if (error) goto quit;
        }

        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                ind2[0] = w_offset + i * n + j;
                val2[0] = 1.0;
                ind2[1] = i * n + j;
                val2[1] = -(double)capacity;
                error = GRBaddconstr(model, 2, ind2, val2, GRB_LESS_EQUAL, 0.0, NULL);
                if (error) goto quit;
            }
        }

        for (int city = 0; city < size; city++) {
            int a;
            int b;
            if (is_port[city]) continue;
            a = 2 * city;
            b = 2 * city + 1;
            ind2[0] = v_offset + a * n + b;
            val2[0] = 1.0;
            ind2[1] = a * n + b;
            val2[1] = -(double)capacity;
            error = GRBaddconstr(model, 2, ind2, val2, GRB_LESS_EQUAL, 0.0, NULL);
            if (error) goto quit;
            ind2[0] = v_offset + b * n + a;
            val2[0] = 1.0;
            ind2[1] = b * n + a;
            val2[1] = -(double)capacity;
            error = GRBaddconstr(model, 2, ind2, val2, GRB_LESS_EQUAL, 0.0, NULL);
            if (error) goto quit;
        }

        for (int i = 0; i < n; i++) {
            if (!is_port[i / 2]) continue;
            for (int j = 0; j < n; j++) {
                ind2[0] = w_offset + i * n + j;
                val2[0] = 1.0;
                error = GRBaddconstr(model, 1, ind2, val2, GRB_EQUAL, 0.0, NULL);
                if (error) goto quit;
            }
        }

        free(ind2);
        free(val2);
    }

    {
        mip_callback_data_t cb;
        cb.n = n;
        cb.numvars = 3 * n * n;
        error = GRBsetcallbackfunc(model, mip_subtourelim_directed, (void*)&cb);
        if (error) goto quit;
        error = GRBsetintparam(GRBgetenv(model), GRB_INT_PAR_LAZYCONSTRAINTS, 1);
        if (error) goto quit;
        optimize_error = GRBoptimize(model);
    }

    if (GRBgetintattr(model, GRB_INT_ATTR_STATUS, &status) != 0) status = 0;
    if (GRBgetintattr(model, GRB_INT_ATTR_SOLCOUNT, &solcount) != 0) solcount = 0;
    if (GRBgetdblattr(model, GRB_DBL_ATTR_RUNTIME, &runtime) != 0) runtime = 0.0;

    /* Full fixed-port construction can request a first feasible incumbent
     * after the nominal limit. Local refinement leaves this off and rejects
     * the boundary if no incumbent is found within its L2SEG budget. */
    if (local_params.wait_for_first_incumbent && status == GRB_TIME_LIMIT && solcount == 0) {
        if (local_params.verbose) {
            printf("Time limit reached with no incumbent - continuing until first solution is found...\n");
            fflush(stdout);
        }
        GRBsetdblparam(GRBgetenv(model), GRB_DBL_PAR_TIMELIMIT, GRB_INFINITY);
        optimize_error = GRBoptimize(model);
        if (GRBgetintattr(model, GRB_INT_ATTR_STATUS,   &status)   != 0) status   = 0;
        if (GRBgetintattr(model, GRB_INT_ATTR_SOLCOUNT, &solcount)  != 0) solcount = 0;
        if (GRBgetdblattr(model, GRB_DBL_ATTR_RUNTIME,  &runtime)   != 0) runtime  = 0.0;
    }

    if (GRBgetdblattr(model, GRB_DBL_ATTR_MIPGAP, &gap) != 0) gap = 0.0;

    solution->status = status;
    solution->solver_error = optimize_error;
    solution->runtime_seconds = runtime;
    solution->gap = gap;

    if (!mip_status_allows_incumbent(status) || solcount <= 0) {
        error = optimize_error ? optimize_error : 1;
        goto quit;
    }

    error = GRBgetdblattr(model, GRB_DBL_ATTR_OBJVAL, &obj);
    if (error) goto quit;
    sol = (double*)mip_xmalloc((size_t)n * (size_t)n * sizeof(double));
    error = GRBgetdblattrarray(model, GRB_DBL_ATTR_X, 0, n * n, sol);
    if (error) goto quit;

    tour = (int*)mip_xmalloc((size_t)n * sizeof(int));
    mip_findsubtour_directed(n, sol, &tour_len, tour);
    mip_orient_node_tour(tour, tour_len);
    letour = mip_node_tour_to_letour(tour, tour_len, size, &letour_len);

    if (letour_len > 1) {
        solution->signed_visit_ids = (int*)mip_xmalloc((size_t)(letour_len - 1) * sizeof(int));
        memcpy(solution->signed_visit_ids, letour + 1, (size_t)(letour_len - 1) * sizeof(int));
        solution->visit_count = letour_len - 1;
    }
    solution->objective_value = obj;
    error = 0;

quit:
    free(entry);
    free(exit);
    free(amount);
    free(is_port);
    free(dist);
    free(ind);
    free(val);
    free(sol);
    free(tour);
    free(letour);
    if (model) GRBfreemodel(model);
    if (owns_env && env) GRBfreeenv(env);
    return (solution->visit_count > 0) ? 0 : error;
}

void free_mip_fixedport_solution(mip_fixedport_solution_t *solution) {
    if (!solution) return;
    free(solution->signed_visit_ids);
    memset(solution, 0, sizeof(*solution));
}
