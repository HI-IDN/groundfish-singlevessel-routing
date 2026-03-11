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

/* Get minimum distance between two nodes using min-pair of endpoints.
 * For nodes with (start_loc, end_loc), tries all 4 combinations and picks cheapest. */
static inline double min_dist_node_pair(const nn_instance_t *inst, int a_idx, int b_idx)
{
    int a_start = inst->nodes[a_idx].start_loc_id;
    int a_end = inst->nodes[a_idx].end_loc_id;
    int b_start = inst->nodes[b_idx].start_loc_id;
    int b_end = inst->nodes[b_idx].end_loc_id;

    double d_ss = get_distance(inst, a_start, b_start);
    double d_se = get_distance(inst, a_start, b_end);
    double d_es = get_distance(inst, a_end, b_start);
    double d_ee = get_distance(inst, a_end, b_end);

    double best = -1.0;
    if (d_ss > 0.0) best = d_ss;
    if (d_se > 0.0 && (best < 0.0 || d_se < best)) best = d_se;
    if (d_es > 0.0 && (best < 0.0 || d_es < best)) best = d_es;
    if (d_ee > 0.0 && (best < 0.0 || d_ee < best)) best = d_ee;
    return best;
}

/* Get minimum distance from a location to a node's endpoints. */
static inline double min_dist_from_loc_to_node(const nn_instance_t *inst, int from_loc, int to_node_idx)
{
    int to_start = inst->nodes[to_node_idx].start_loc_id;
    int to_end = inst->nodes[to_node_idx].end_loc_id;

    double d_start = get_distance(inst, from_loc, to_start);
    double d_end = get_distance(inst, from_loc, to_end);

    if (d_start < 0.0) return d_end;
    if (d_end < 0.0) return d_start;
    return (d_start < d_end) ? d_start : d_end;
}

#endif
