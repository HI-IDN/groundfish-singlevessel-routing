#include "include/mip_noport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gurobi_c.h>

#ifndef __stdcall
#define __stdcall
#endif

typedef struct {
    int n;
    int numvars;
} callback_data_t;

static const char *gurobi_status_name(int status) {
    switch (status) {
        case GRB_OPTIMAL: return "OPTIMAL";
        case GRB_SUBOPTIMAL: return "SUBOPTIMAL";
        case GRB_TIME_LIMIT: return "TIME_LIMIT";
        case GRB_INTERRUPTED: return "INTERRUPTED";
        case GRB_NODE_LIMIT: return "NODE_LIMIT";
        case GRB_ITERATION_LIMIT: return "ITERATION_LIMIT";
        case GRB_WORK_LIMIT: return "WORK_LIMIT";
        case GRB_INFEASIBLE: return "INFEASIBLE";
        case GRB_INF_OR_UNBD: return "INF_OR_UNBD";
        case GRB_UNBOUNDED: return "UNBOUNDED";
        default: return "OTHER";
    }
}

static int status_allows_incumbent(int status) {
    return status == GRB_OPTIMAL ||
           status == GRB_SUBOPTIMAL ||
           status == GRB_TIME_LIMIT ||
           status == GRB_INTERRUPTED ||
           status == GRB_NODE_LIMIT ||
           status == GRB_ITERATION_LIMIT ||
           status == GRB_WORK_LIMIT;
}

static void *xmalloc_local(size_t nbytes) {
    void *ptr = malloc(nbytes);
    if (!ptr) {
        fprintf(stderr, "Out of memory\n");
        exit(1);
    }
    return ptr;
}

static void *xcalloc_local(size_t count, size_t size) {
    void *ptr = calloc(count, size);
    if (!ptr) {
        fprintf(stderr, "Out of memory\n");
        exit(1);
    }
    return ptr;
}

static double lookup_distance_nm(const mip_noport_instance_t *instance, int from_loc_id, int to_loc_id) {
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

static void findsubtour_directed(int n, const double *sol, int *tourlen_out, int *tour_out) {
    int *unvisited = (int*)xcalloc_local((size_t)n, sizeof(int));
    int *best = (int*)xmalloc_local((size_t)n * sizeof(int));
    int *current = (int*)xmalloc_local((size_t)n * sizeof(int));
    int best_len = n + 1;
    int remaining = n;

    for (int i = 0; i < n; i++) unvisited[i] = 1;

    while (remaining > 0) {
        int start = -1;
        int len = 0;
        int cursor;

        for (int i = 0; i < n; i++) {
            if (unvisited[i]) {
                start = i;
                break;
            }
        }
        if (start < 0) break;

        cursor = start;
        while (cursor >= 0 && unvisited[cursor]) {
            current[len++] = cursor;
            unvisited[cursor] = 0;
            remaining--;

            {
                int next = -1;
                for (int j = 0; j < n; j++) {
                    if (sol[cursor * n + j] > 0.5 && unvisited[j]) {
                        next = j;
                        break;
                    }
                }
                cursor = next;
            }
        }

        if (len < best_len) {
            best_len = len;
            memcpy(best, current, (size_t)len * sizeof(int));
        }
    }

    memcpy(tour_out, best, (size_t)best_len * sizeof(int));
    *tourlen_out = best_len;

    free(unvisited);
    free(best);
    free(current);
}

static int *node_tour_to_letour(const int *tour, int len, int size, int *out_len) {
    int *letour = (int*)xmalloc_local((size_t)size * sizeof(int));
    int count = 0;

    for (int i = 0; i < len; i++) {
        int city = tour[i] / 2;
        int seen = 0;
        for (int j = 0; j < count; j++) {
            if (abs(letour[j]) == city) {
                seen = 1;
                break;
            }
        }
        if (!seen) letour[count++] = (tour[i] % 2 == 1) ? -city : city;
    }

    {
        int pos0 = -1;
        for (int i = 0; i < count; i++) {
            if (letour[i] == 0) {
                pos0 = i;
                break;
            }
        }
        if (pos0 > 0) {
            int *rotated = (int*)xmalloc_local((size_t)count * sizeof(int));
            int idx = 0;
            for (int i = pos0; i < count; i++) rotated[idx++] = letour[i];
            for (int i = 0; i < pos0; i++) rotated[idx++] = letour[i];
            memcpy(letour, rotated, (size_t)count * sizeof(int));
            free(rotated);
        }
    }

    *out_len = count;
    return letour;
}

static void orient_node_tour(int *tour, int len) {
    if (!tour || len <= 1) return;

    {
        int pos0 = -1;
        for (int i = 0; i < len; i++) {
            if (tour[i] == 0) {
                pos0 = i;
                break;
            }
        }
        if (pos0 > 0) {
            int *rotated = (int*)xmalloc_local((size_t)len * sizeof(int));
            int idx = 0;
            for (int i = pos0; i < len; i++) rotated[idx++] = tour[i];
            for (int i = 0; i < pos0; i++) rotated[idx++] = tour[i];
            memcpy(tour, rotated, (size_t)len * sizeof(int));
            free(rotated);
        }
    }

    if (len > 1 && tour[1] == 1) {
        for (int i = 1, j = len - 1; i < j; i++, j--) {
            int tmp = tour[i];
            tour[i] = tour[j];
            tour[j] = tmp;
        }
    }
}

static int __stdcall subtourelim(GRBmodel *model, void *cbdata, int where, void *usrdata) {
    callback_data_t *cb = (callback_data_t*)usrdata;
    int error = 0;

    (void)model;

    if (where == GRB_CB_MIPSOL) {
        double *sol = (double*)xmalloc_local((size_t)cb->numvars * sizeof(double));
        int *tour = (int*)xmalloc_local((size_t)cb->n * sizeof(int));
        int len = 0;

        GRBcbget(cbdata, where, GRB_CB_MIPSOL_SOL, sol);
        findsubtour_directed(cb->n, sol, &len, tour);

        if (len < cb->n) {
            int max_pairs = len * (len - 1) / 2;
            int nz = 2 * max_pairs;
            int *ind = (int*)xmalloc_local((size_t)nz * sizeof(int));
            double *val = (double*)xmalloc_local((size_t)nz * sizeof(double));
            int k = 0;

            for (int a = 0; a < len; a++) {
                for (int b = a + 1; b < len; b++) {
                    int i = tour[a];
                    int j = tour[b];
                    ind[k] = i * cb->n + j;
                    val[k] = 1.0;
                    k++;
                    ind[k] = j * cb->n + i;
                    val[k] = 1.0;
                    k++;
                }
            }
            error = GRBcblazy(cbdata, k, ind, val, GRB_LESS_EQUAL, (double)len - 1.0);
            free(ind);
            free(val);
        }

        free(sol);
        free(tour);
    }

    return error;
}

static int solve_tsp_distance(GRBenv *env,
                              const double *dist,
                              int size,
                              const mip_noport_params_t *params,
                              double *out_obj,
                              int **out_tour,
                              int *out_len,
                              int *out_status,
                              double *out_runtime,
                              double *out_gap) {
    int n = 2 * size;
    GRBmodel *model = NULL;
    int error = 0;
    int *ind = NULL;
    double *val = NULL;
    int solcount = 0;
    int status = 0;
    double runtime = 0.0;
    double gap = 0.0;

    error = GRBnewmodel(env, &model, "noport", 0, NULL, NULL, NULL, NULL, NULL);
    if (error) goto quit;

    {
        GRBenv *model_env = GRBgetenv(model);
        if (params && params->time_limit_seconds > 0.0) {
            GRBsetdblparam(model_env, "TimeLimit", params->time_limit_seconds);
        }
        if (params && params->thread_count > 0) {
            GRBsetintparam(model_env, "Threads", params->thread_count);
        }
        if (params && params->mip_gap > 0.0) {
            GRBsetdblparam(model_env, "MIPGap", params->mip_gap);
        }
        GRBsetintparam(model_env, "OutputFlag", (params && params->verbose) ? 1 : 0);
        GRBsetintparam(model_env, "LogToConsole", (params && params->verbose) ? 1 : 0);
    }

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            char name[64];
            snprintf(name, sizeof(name), "e_%d_%d", i, j);
            error = GRBaddvar(model, 0, NULL, NULL, dist[i * n + j], 0.0, 1.0, GRB_BINARY, name);
            if (error) goto quit;
        }
    }

    ind = (int*)xmalloc_local((size_t)n * sizeof(int));
    val = (double*)xmalloc_local((size_t)n * sizeof(double));
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
        val[0] = 1.0;
        ind[1] = b * n + a;
        val[1] = 1.0;
        error = GRBaddconstr(model, 2, ind, val, GRB_EQUAL, 1.0, NULL);
        if (error) goto quit;
    }

    for (int i = 0; i < n; i++) {
        error = GRBsetdblattrelement(model, GRB_DBL_ATTR_UB, i * n + i, 0.0);
        if (error) goto quit;
    }

    {
        callback_data_t cb;
        cb.n = n;
        cb.numvars = n * n;
        error = GRBsetcallbackfunc(model, subtourelim, (void*)&cb);
        if (error) goto quit;
        error = GRBsetintparam(GRBgetenv(model), GRB_INT_PAR_LAZYCONSTRAINTS, 1);
        if (error) goto quit;
        error = GRBoptimize(model);
        if (error) goto quit;
    }

    GRBgetintattr(model, GRB_INT_ATTR_STATUS, &status);
    GRBgetintattr(model, GRB_INT_ATTR_SOLCOUNT, &solcount);
    GRBgetdblattr(model, GRB_DBL_ATTR_RUNTIME, &runtime);
    if (GRBgetdblattr(model, GRB_DBL_ATTR_MIPGAP, &gap) != 0) gap = 0.0;

    if (out_status) *out_status = status;
    if (out_runtime) *out_runtime = runtime;
    if (out_gap) *out_gap = gap;

    if (params && params->verbose) {
        fprintf(stderr,
                "No-port TSP status=%s(%d) solcount=%d runtime=%.2f s gap=%.6f\n",
                gurobi_status_name(status), status, solcount, runtime, gap);
    }

    if (status_allows_incumbent(status) && solcount > 0) {
        error = GRBgetdblattr(model, GRB_DBL_ATTR_OBJVAL, out_obj);
        if (error) goto quit;
        if (out_tour && out_len) {
            double *sol = NULL;
            sol = (double*)xmalloc_local((size_t)n * (size_t)n * sizeof(double));
            error = GRBgetdblattrarray(model, GRB_DBL_ATTR_X, 0, n * n, sol);
            if (error) {
                free(sol);
                goto quit;
            }
            *out_tour = (int*)xmalloc_local((size_t)n * sizeof(int));
            *out_len = 0;
            findsubtour_directed(n, sol, out_len, *out_tour);
            free(sol);
        }
    } else {
        if (params && params->verbose) {
            fprintf(stderr, "No incumbent solution available for export\n");
        }
        error = 1;
    }

quit:
    free(ind);
    free(val);
    if (error && env) {
        const char *msg = GRBgeterrormsg(env);
        if (msg && *msg) fprintf(stderr, "Gurobi error: %s\n", msg);
    }
    if (model) GRBfreemodel(model);
    return error;
}

int solve_mip_noport(const mip_noport_instance_t *instance,
                     const mip_noport_params_t *params,
                     mip_noport_solution_t *solution) {
    GRBenv *env = NULL;
    double *dist = NULL;
    int *entry = NULL;
    int *exit = NULL;
    int *node_tour = NULL;
    int node_len = 0;
    int seg_size;
    int n;
    int error = 0;

    if (!instance || !instance->boat || !solution) return 1;

    memset(solution, 0, sizeof(*solution));

    seg_size = 1 + instance->n_stations;
    n = 2 * seg_size;
    dist = (double*)xmalloc_local((size_t)n * (size_t)n * sizeof(double));
    entry = (int*)xmalloc_local((size_t)seg_size * sizeof(int));
    exit = (int*)xmalloc_local((size_t)seg_size * sizeof(int));

    entry[0] = instance->boat->start_location_id;
    exit[0] = instance->boat->end_location_id;
    for (int i = 0; i < instance->n_stations; i++) {
        entry[i + 1] = instance->stations[i].start_location_id;
        exit[i + 1] = instance->stations[i].end_location_id;
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

    dist[1 * n + 0] = 0.0;
    dist[0 * n + 1] = 0.0;

    error = GRBloadenv(&env, NULL);
    if (error) goto quit;

    error = solve_tsp_distance(env, dist, seg_size, params,
                               &solution->objective_value,
                               &node_tour,
                               &node_len,
                               &solution->status,
                               &solution->runtime_seconds,
                               &solution->gap);
    if (error) goto quit;

    solution->total_distance_nm = solution->objective_value;

    if (instance->n_stations > 0 && node_tour) {
        int letour_len = 0;
        int *letour = NULL;

        orient_node_tour(node_tour, node_len);
        letour = node_tour_to_letour(node_tour, node_len, seg_size, &letour_len);
        solution->signed_station_ids = (int*)xmalloc_local((size_t)instance->n_stations * sizeof(int));
        solution->order_length = 0;

        for (int i = 0; i < letour_len; i++) {
            int local_idx;
            int sign;
            if (letour[i] == 0) continue;
            sign = (letour[i] < 0) ? -1 : 1;
            local_idx = abs(letour[i]) - 1;
            if (local_idx < 0 || local_idx >= instance->n_stations) continue;
            solution->signed_station_ids[solution->order_length++] =
                sign * instance->stations[local_idx].station_id;
        }

        free(letour);
    }

quit:
    if (params && params->verbose) {
        fprintf(stderr,
                "No-port solve summary: status=%s(%d) runtime=%.2f s gap=%.6f order_length=%d\n",
                gurobi_status_name(solution->status),
                solution->status,
                solution->runtime_seconds,
                solution->gap,
                solution->order_length);
    }
    if (error && solution) {
        free_mip_noport_solution(solution);
        if (solution) {
            memset(solution, 0, sizeof(*solution));
            solution->status = MIP_STATUS_INFEASIBLE;
        }
    }
    free(node_tour);
    free(dist);
    free(entry);
    free(exit);
    if (env) GRBfreeenv(env);
    return error;
}

void free_mip_noport_solution(mip_noport_solution_t *solution) {
    if (!solution) return;
    free(solution->signed_station_ids);
    memset(solution, 0, sizeof(*solution));
}
