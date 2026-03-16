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

static inline int find_nearest_port_from_node_pair(const nn_instance_t *inst, int from_node_idx)
{
    double min_dist = 1e100;
    int nearest = -1;
    if (!inst || from_node_idx < 0 || from_node_idx >= inst->num_stations + inst->num_ports) return -1;
    for (int i = inst->num_stations; i < inst->num_stations + inst->num_ports; i++) {
        double d = min_dist_node_pair(inst, from_node_idx, i);
        if (d > 0.0 && d < min_dist) {
            min_dist = d;
            nearest = i;
        }
    }
    return nearest;
}

/* ── segmentation helpers ────────────────────────────────────── */

/* Insert a port into the tour, close the current segment, and reset state.
 * Returns 0 on allocation failure, 1 on success.
 * from_loc_id  : current location ID (used to compute distance to port)
 * port_node_idx: index into inst->nodes[] for the chosen port            */
static inline int insert_port_segment(
    const nn_instance_t *inst,
    int port_node_idx,
    int from_loc_id,
    int **tour,        int *tour_cap,        int *tour_len,
    int **seg_starts,  int *seg_starts_cap,
    int **seg_ends,    int *seg_ends_cap,
    int **seg_catches, int *seg_catches_cap,
    double **seg_dists,int *seg_dists_cap,
    int *segment_count,
    int  segment_start_idx,
    int  current_load,
    double *current_segment_dist,
    int *new_loc_id,
    int *new_segment_start_idx)
{
    if (!grow_int_array(tour,       tour_cap,       *tour_len + 1)       ||
        !grow_int_array(seg_starts, seg_starts_cap, *segment_count + 1)  ||
        !grow_int_array(seg_ends,   seg_ends_cap,   *segment_count + 1)  ||
        !grow_int_array(seg_catches,seg_catches_cap,*segment_count + 1)  ||
        !grow_dist_array(seg_dists, seg_dists_cap,  *segment_count + 1))
        return 0;

    int port_loc = inst->nodes[port_node_idx].start_loc_id;
    double d = get_distance(inst, from_loc_id, port_loc);
    if (d > 0.0) *current_segment_dist += d;

    (*tour)[(*tour_len)++] = port_loc;
    (*seg_starts)[*segment_count] = segment_start_idx;
    (*seg_ends)  [*segment_count] = *tour_len - 1;
    (*seg_catches)[*segment_count] = current_load;
    (*seg_dists) [*segment_count] = *current_segment_dist;
    (*segment_count)++;

    *new_loc_id            = port_loc;
    *new_segment_start_idx = *tour_len;
    *current_segment_dist  = 0.0;
    return 1;
}

/* Flush the still-open trailing segment (no port at end).
 * Returns 0 on allocation failure, 1 on success.                         */
static inline int flush_final_segment(
    int **seg_starts,  int *seg_starts_cap,
    int **seg_ends,    int *seg_ends_cap,
    int **seg_catches, int *seg_catches_cap,
    double **seg_dists,int *seg_dists_cap,
    int *segment_count,
    int  segment_start_idx,
    int  tour_len,
    int  current_load,
    double current_segment_dist)
{
    if (!grow_int_array(seg_starts, seg_starts_cap, *segment_count + 1)  ||
        !grow_int_array(seg_ends,   seg_ends_cap,   *segment_count + 1)  ||
        !grow_int_array(seg_catches,seg_catches_cap,*segment_count + 1)  ||
        !grow_dist_array(seg_dists, seg_dists_cap,  *segment_count + 1))
        return 0;

    (*seg_starts) [*segment_count] = segment_start_idx;
    (*seg_ends)   [*segment_count] = tour_len - 1;
    (*seg_catches)[*segment_count] = current_load;
    (*seg_dists)  [*segment_count] = current_segment_dist;
    (*segment_count)++;
    return 1;
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

/* Choose station traversal orientation from a current location.
 * direction: +1 means start->end, -1 means end->start. */
static inline int choose_station_orientation_with_dir(
    const nn_instance_t *inst,
    int current_loc_id,
    int station_idx,
    int *entry_loc,
    int *exit_loc,
    double *added_dist,
    int *direction)
{
    int s = inst->nodes[station_idx].start_loc_id;
    int e = inst->nodes[station_idx].end_loc_id;

    double d_cs = get_distance(inst, current_loc_id, s);
    double d_se = get_distance(inst, s, e);
    double d_ce = get_distance(inst, current_loc_id, e);
    double d_es = get_distance(inst, e, s);

    double opt_se = (d_cs > 0.0 && d_se > 0.0) ? (d_cs + d_se) : -1.0;
    double opt_es = (d_ce > 0.0 && d_es > 0.0) ? (d_ce + d_es) : -1.0;

    if (s == e) {
        if (d_cs <= 0.0) return 0;
        if (entry_loc) *entry_loc = s;
        if (exit_loc) *exit_loc = e;
        if (added_dist) *added_dist = d_cs;
        if (direction) *direction = +1;
        return 1;
    }

    if (opt_se <= 0.0 && opt_es <= 0.0) return 0;

    if (opt_es > 0.0 && (opt_se <= 0.0 || opt_es < opt_se)) {
        if (entry_loc) *entry_loc = e;
        if (exit_loc) *exit_loc = s;
        if (added_dist) *added_dist = opt_es;
        if (direction) *direction = -1;
    } else {
        if (entry_loc) *entry_loc = s;
        if (exit_loc) *exit_loc = e;
        if (added_dist) *added_dist = opt_se;
        if (direction) *direction = +1;
    }
    return 1;
}

/* Clean wrapper when caller does not need +1/-1 direction. */
static inline int choose_station_orientation(
    const nn_instance_t *inst,
    int current_loc_id,
    int station_idx,
    int *entry_loc,
    int *exit_loc,
    double *added_dist)
{
    return choose_station_orientation_with_dir(
        inst, current_loc_id, station_idx,
        entry_loc, exit_loc, added_dist, NULL);
}


#endif
