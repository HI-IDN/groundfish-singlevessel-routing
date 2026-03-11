#ifndef INIT_UTILS_H
#define INIT_UTILS_H

#include <stdlib.h>
#include "../include/init_types.h"

/* ── array growth ────────────────────────────────────────────── */

static inline int grow_int_array(int **arr, int *cap, int need)
{
    if (need <= *cap) return 1;
    int new_cap = (*cap <= 0) ? 64 : *cap;
    while (new_cap < need) new_cap *= 2;
    int *tmp = (int*)realloc(*arr, (size_t)new_cap * sizeof(int));
    if (!tmp) return 0;
    *arr = tmp; *cap = new_cap;
    return 1;
}

static inline int grow_dist_array(double **arr, int *cap, int need)
{
    if (need <= *cap) return 1;
    int new_cap = (*cap <= 0) ? 64 : *cap;
    while (new_cap < need) new_cap *= 2;
    double *tmp = (double*)realloc(*arr, (size_t)new_cap * sizeof(double));
    if (!tmp) return 0;
    *arr = tmp; *cap = new_cap;
    return 1;
}

/* ── distance lookup ─────────────────────────────────────────── */

static inline double get_distance(const nn_instance_t *inst, int from_id, int to_id)
{
    if (from_id < 0 || from_id >= inst->max_loc_id ||
        to_id   < 0 || to_id   >= inst->max_loc_id)
        return -1.0;
    return inst->distances[from_id][to_id];
}

/* ── nearest port ────────────────────────────────────────────── */

static inline int find_nearest_port(const nn_instance_t *inst, int from_loc_id)
{
    double min_dist = 1e100;
    int nearest = -1;
    for (int i = inst->num_stations; i < inst->num_stations + inst->num_ports; i++) {
        double d = get_distance(inst, from_loc_id, inst->nodes[i].start_loc_id);
        if (d > 0.0 && d < min_dist) { min_dist = d; nearest = i; }
    }
    return nearest;
}

#endif
