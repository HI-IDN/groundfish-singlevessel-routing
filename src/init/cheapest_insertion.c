#include <stdio.h>
#include <stdlib.h>

#include "cheapest_insertion.h"
#include "init_utils.h"

static double min_dist_station_pair(const nn_instance_t *inst, int a_idx, int b_idx)
{
    int a0 = inst->nodes[a_idx].start_loc_id;
    int a1 = inst->nodes[a_idx].end_loc_id;
    int b0 = inst->nodes[b_idx].start_loc_id;
    int b1 = inst->nodes[b_idx].end_loc_id;

    double d00 = get_distance(inst, a0, b0);
    double d01 = get_distance(inst, a0, b1);
    double d10 = get_distance(inst, a1, b0);
    double d11 = get_distance(inst, a1, b1);

    double best = -1.0;
    if (d00 > 0.0) best = d00;
    if (d01 > 0.0 && (best < 0.0 || d01 < best)) best = d01;
    if (d10 > 0.0 && (best < 0.0 || d10 < best)) best = d10;
    if (d11 > 0.0 && (best < 0.0 || d11 < best)) best = d11;
    return best;
}

static int build_station_order_ci(const nn_instance_t *inst, int **out_order, int *out_n)
{
    int n = inst->num_stations;
    *out_order = NULL;
    *out_n = 0;
    if (n <= 0) return 1;

    int *tour = NULL;
    int tour_cap = 0;
    int tour_len = 0;
    int *used = (int*)calloc((size_t)n, sizeof(int));
    if (!used) return 0;

    if (n == 1) {
        if (!grow_int_array(&tour, &tour_cap, 1)) {
            free(used);
            return 0;
        }
        tour[0] = 0;
        *out_order = tour;
        *out_n = 1;
        free(used);
        return 1;
    }

    int a = 0, b = 1;
    double best_pair = min_dist_station_pair(inst, a, b);
    // Start from the closest station pair, then insert by minimum edge delta.
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            double d = min_dist_station_pair(inst, i, j);
            if (d > 0.0 && (best_pair < 0.0 || d < best_pair)) {
                best_pair = d;
                a = i;
                b = j;
            }
        }
    }

    if (!grow_int_array(&tour, &tour_cap, 2)) {
        free(used);
        return 0;
    }
    tour[tour_len++] = a;
    tour[tour_len++] = b;
    used[a] = 1;
    used[b] = 1;

    while (tour_len < n) {
        double best_delta = 1e100;
        int best_u = -1;
        int best_pos = -1;

        // Cheapest insertion over current cycle edges (i -> j).
        for (int u = 0; u < n; u++) {
            if (used[u]) continue;
            for (int i = 0; i < tour_len; i++) {
                int j = (i + 1) % tour_len;
                double d_ij = min_dist_station_pair(inst, tour[i], tour[j]);
                double d_iu = min_dist_station_pair(inst, tour[i], u);
                double d_uj = min_dist_station_pair(inst, u, tour[j]);
                if (d_iu < 0.0 || d_uj < 0.0) continue;
                if (d_ij < 0.0) d_ij = 0.0;
                double delta = d_iu + d_uj - d_ij;
                if (delta < best_delta) {
                    best_delta = delta;
                    best_u = u;
                    best_pos = j;
                }
            }
        }

        if (best_u < 0) {
            free(tour);
            free(used);
            return 0;
        }

        if (!grow_int_array(&tour, &tour_cap, tour_len + 1)) {
            free(tour);
            free(used);
            return 0;
        }
        for (int k = tour_len; k > best_pos; k--) {
            tour[k] = tour[k - 1];
        }
        tour[best_pos] = best_u;
        used[best_u] = 1;
        tour_len++;
    }

    free(used);
    *out_order = tour;
    *out_n = tour_len;
    return 1;
}

static void zero_solution(nn_solution_t *sol)
{
    if (sol) {
        sol->tour = NULL;
        sol->tour_length = 0;
        sol->visit_station_ids = NULL;
        sol->visit_station_count = 0;
        sol->visit_station_segment = NULL;
        sol->visit_station_direction = NULL;
        sol->segment_count = 0;
        sol->segment_starts = NULL;
        sol->segment_ends = NULL;
        sol->segment_catches = NULL;
        sol->segment_dists = NULL;
        sol->total_distance = 0.0;
        sol->total_catch = 0;
    }
}

static void free_partial_solution(nn_solution_t *sol)
{
    if (!sol) return;
    free(sol->tour);
    free(sol->visit_station_ids);
    free(sol->visit_station_segment);
    free(sol->visit_station_direction);
    free(sol->segment_starts);
    free(sol->segment_ends);
    free(sol->segment_catches);
    free(sol->segment_dists);
    zero_solution(sol);
}

static int build_noport_solution_ci(const nn_instance_t *inst, const int *station_order, int station_order_n,
                                    nn_solution_t *sol, int boat_start_loc_id, int boat_end_loc_id)
{
    int *tour = NULL, tour_cap = 0, tour_len = 0;
    int *visit_station_ids = NULL, visit_ids_cap = 0;
    int *visit_station_segment = NULL, visit_seg_cap = 0;
    int *visit_station_direction = NULL, visit_dir_cap = 0;
    int *segment_starts = NULL, seg_starts_cap = 0;
    int *segment_ends = NULL, seg_ends_cap = 0;
    int *segment_catches = NULL, seg_catches_cap = 0;
    double *segment_dists = NULL;
    int seg_dists_cap = 0;
    int current_loc_id = boat_start_loc_id;
    int total_catch = 0;
    double total_dist = 0.0;

    if (!sol) return 0;
    zero_solution(sol);

    for (int ord = 0; ord < station_order_n; ord++) {
        int st_idx = station_order[ord];
        int stat_entry = -1, stat_exit = -1, stat_dir = 0;
        double stat_added = 0.0;
        if (!choose_station_orientation_with_dir(inst, current_loc_id, st_idx,
                                                 &stat_entry, &stat_exit, &stat_added, &stat_dir)) {
            free(tour); free(visit_station_ids); free(visit_station_segment); free(visit_station_direction);
            free(segment_starts); free(segment_ends); free(segment_catches); free(segment_dists);
            return 0;
        }
        if (!grow_int_array(&tour, &tour_cap, tour_len + ((stat_exit != stat_entry) ? 2 : 1)) ||
            !grow_int_array(&visit_station_ids, &visit_ids_cap, ord + 1) ||
            !grow_int_array(&visit_station_segment, &visit_seg_cap, ord + 1) ||
            !grow_int_array(&visit_station_direction, &visit_dir_cap, ord + 1)) {
            free(tour); free(visit_station_ids); free(visit_station_segment); free(visit_station_direction);
            free(segment_starts); free(segment_ends); free(segment_catches); free(segment_dists);
            return 0;
        }
        if (stat_added > 0.0) total_dist += stat_added;
        tour[tour_len++] = stat_entry;
        if (stat_exit != stat_entry) tour[tour_len++] = stat_exit;
        current_loc_id = stat_exit;
        total_catch += inst->nodes[st_idx].amount;
        visit_station_ids[ord] = inst->nodes[st_idx].table_id;
        visit_station_segment[ord] = 0;
        visit_station_direction[ord] = stat_dir;
    }

    if (!grow_int_array(&segment_starts, &seg_starts_cap, 1) ||
        !grow_int_array(&segment_ends, &seg_ends_cap, 1) ||
        !grow_int_array(&segment_catches, &seg_catches_cap, 1) ||
        !grow_dist_array(&segment_dists, &seg_dists_cap, 1)) {
        free(tour); free(visit_station_ids); free(visit_station_segment); free(visit_station_direction);
        free(segment_starts); free(segment_ends); free(segment_catches); free(segment_dists);
        return 0;
    }

    if (tour_len > 0) {
        double return_dist = get_distance(inst, current_loc_id, boat_end_loc_id);
        if (return_dist > 0.0) total_dist += return_dist;
    } else {
        double return_dist = get_distance(inst, boat_start_loc_id, boat_end_loc_id);
        if (return_dist > 0.0) total_dist += return_dist;
    }

    segment_starts[0] = 0;
    segment_ends[0] = (tour_len > 0) ? (tour_len - 1) : -1;
    segment_catches[0] = total_catch;
    segment_dists[0] = total_dist;

    sol->tour = tour;
    sol->tour_length = tour_len;
    sol->visit_station_ids = visit_station_ids;
    sol->visit_station_count = station_order_n;
    sol->visit_station_segment = visit_station_segment;
    sol->visit_station_direction = visit_station_direction;
    sol->segment_count = 1;
    sol->segment_starts = segment_starts;
    sol->segment_ends = segment_ends;
    sol->segment_catches = segment_catches;
    sol->segment_dists = segment_dists;
    sol->total_distance = total_dist;
    sol->total_catch = total_catch;
    return 1;
}

int ci_solve(const nn_instance_t *inst, nn_solution_t *sol,
             nn_solution_t *pre_capacity_sol,
             int boat_start_loc_id, int boat_end_loc_id,
             int boat_capacity)
{
    int *station_order = NULL;
    int station_order_n = 0;
    if (!build_station_order_ci(inst, &station_order, &station_order_n)) return -1;
    if (pre_capacity_sol) {
        zero_solution(pre_capacity_sol);
        if (!build_noport_solution_ci(inst, station_order, station_order_n,
                                      pre_capacity_sol, boat_start_loc_id, boat_end_loc_id)) {
            free(station_order);
            return -1;
        }
    }

    int *tour = NULL, tour_cap = 0, tour_len = 0;
    int *segment_starts = NULL, seg_starts_cap = 0;
    int *segment_ends = NULL, seg_ends_cap = 0;
    int *segment_catches = NULL, seg_catches_cap = 0;
    double *segment_dists = NULL; int seg_dists_cap = 0;
    int *visit_station_ids = NULL, visit_ids_cap = 0;
    int *visit_station_segment = NULL, visit_seg_cap = 0;
    int *visit_station_direction = NULL, visit_dir_cap = 0;

    int segment_count = 0;
    int visit_station_count = 0;
    int current_loc_id = boat_start_loc_id;
    int current_load = 0;
    double current_segment_dist = 0.0;
    int segment_start_idx = 0;

    printf("[CI] Start: loc=%d cap=%d stations=%d ports=%d\n",
           boat_start_loc_id, boat_capacity, inst->num_stations, inst->num_ports);

    for (int ord = 0; ord < station_order_n; ord++) {
        int st_idx = station_order[ord];
        int st_amount = inst->nodes[st_idx].amount;

        // Pre-station trigger: if next station exceeds capacity, insert port first.
        if (current_load > 0 && current_load + st_amount > boat_capacity) {
            int port_idx = find_nearest_port(inst, current_loc_id);
            if (port_idx >= 0) {
                int new_loc, new_seg_start;
                if (!insert_port_segment(inst, port_idx, current_loc_id,
                        &tour, &tour_cap, &tour_len,
                        &segment_starts, &seg_starts_cap,
                        &segment_ends,   &seg_ends_cap,
                        &segment_catches,&seg_catches_cap,
                        &segment_dists,  &seg_dists_cap,
                        &segment_count, segment_start_idx,
                        current_load, &current_segment_dist,
                        &new_loc, &new_seg_start)) {
                    free(station_order);
                    free_partial_solution(sol);
                    return -1;
                }
                current_loc_id = new_loc;
                current_load = 0;
                segment_start_idx = new_seg_start;
            }
        }

        int stat_entry, stat_exit;
        double stat_added = 0.0;
        if (!choose_station_orientation(inst, current_loc_id, st_idx, &stat_entry, &stat_exit, &stat_added)) {
            free(station_order);
            free_partial_solution(sol);
            return -1;
        }

        // Emit station traversal and register station->segment mapping.
        if (!grow_int_array(&tour, &tour_cap, tour_len + ((stat_exit != stat_entry) ? 2 : 1)) ||
            !grow_int_array(&visit_station_ids, &visit_ids_cap, visit_station_count + 1) ||
            !grow_int_array(&visit_station_segment, &visit_seg_cap, visit_station_count + 1) ||
            !grow_int_array(&visit_station_direction, &visit_dir_cap, visit_station_count + 1)) {
            free(station_order);
            free_partial_solution(sol);
            return -1;
        }

        if (stat_added > 0.0) current_segment_dist += stat_added;
        tour[tour_len++] = stat_entry;
        if (stat_exit != stat_entry) {
            tour[tour_len++] = stat_exit;
        }
        current_loc_id = stat_exit;

        current_load += st_amount;
        visit_station_ids[visit_station_count] = inst->nodes[st_idx].table_id;
        visit_station_segment[visit_station_count] = segment_count;
        visit_station_direction[visit_station_count] = (stat_entry == inst->nodes[st_idx].end_loc_id &&
                                                        stat_exit == inst->nodes[st_idx].start_loc_id) ? -1 : 1;
        visit_station_count++;

        // Post-station trigger: if load at capacity and more stations remain, insert port.
        if (current_load >= boat_capacity && ord + 1 < station_order_n) {
            int port_idx = find_nearest_port(inst, current_loc_id);
            if (port_idx >= 0) {
                int new_loc, new_seg_start;
                if (!insert_port_segment(inst, port_idx, current_loc_id,
                        &tour, &tour_cap, &tour_len,
                        &segment_starts, &seg_starts_cap,
                        &segment_ends,   &seg_ends_cap,
                        &segment_catches,&seg_catches_cap,
                        &segment_dists,  &seg_dists_cap,
                        &segment_count, segment_start_idx,
                        current_load, &current_segment_dist,
                        &new_loc, &new_seg_start)) {
                    free(station_order);
                    free_partial_solution(sol);
                    return -1;
                }
                current_loc_id = new_loc;
                current_load = 0;
                segment_start_idx = new_seg_start;
            }
        }
    }

    // Flush final open segment.
    if (current_load > 0 || segment_count == 0) {
        if (!flush_final_segment(
                &segment_starts, &seg_starts_cap,
                &segment_ends,   &seg_ends_cap,
                &segment_catches,&seg_catches_cap,
                &segment_dists,  &seg_dists_cap,
                &segment_count, segment_start_idx,
                tour_len, current_load, current_segment_dist)) {
            free(station_order);
            free_partial_solution(sol);
            return -1;
        }
    }

    double total_dist = 0.0;
    for (int i = 0; i < segment_count; i++) total_dist += segment_dists[i];
    double return_dist = get_distance(inst, current_loc_id, boat_end_loc_id);
    if (return_dist > 0.0) total_dist += return_dist;

    sol->tour = tour;
    sol->tour_length = tour_len;
    sol->visit_station_ids = visit_station_ids;
    sol->visit_station_count = visit_station_count;
    sol->visit_station_segment = visit_station_segment;
    sol->visit_station_direction = visit_station_direction;
    sol->segment_count = segment_count;
    sol->segment_starts = segment_starts;
    sol->segment_ends = segment_ends;
    sol->segment_catches = segment_catches;
    sol->segment_dists = segment_dists;
    sol->total_distance = total_dist;

    sol->total_catch = 0;
    for (int i = 0; i < segment_count; i++) sol->total_catch += segment_catches[i];

    printf("[CI] Done: segments=%d stations=%d total_dist=%.2f total_catch=%d\n",
           sol->segment_count, sol->visit_station_count, sol->total_distance, sol->total_catch);

    free(station_order);
    return 0;
}


