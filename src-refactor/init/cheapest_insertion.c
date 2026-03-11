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

int ci_solve(const nn_instance_t *inst, nn_solution_t *sol,
             int boat_start_loc_id, int boat_end_loc_id,
             int boat_capacity)
{
    int *station_order = NULL;
    int station_order_n = 0;
    if (!build_station_order_ci(inst, &station_order, &station_order_n)) return -1;

    int *tour = NULL, tour_cap = 0, tour_len = 0;
    int *segment_starts = NULL, seg_starts_cap = 0;
    int *segment_ends = NULL, seg_ends_cap = 0;
    int *segment_catches = NULL, seg_catches_cap = 0;
    double *segment_dists = NULL; int seg_dists_cap = 0;
    int *visit_station_ids = NULL, visit_ids_cap = 0;
    int *visit_station_segment = NULL, visit_seg_cap = 0;

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
        int st_amount = (int)(inst->nodes[st_idx].amount + 0.5);

        // If the next station would overflow, close at nearest port first.
        if (current_load > 0 && current_load + st_amount > boat_capacity) {
            int port_idx = find_nearest_port(inst, current_loc_id);
            if (port_idx >= 0) {
                if (!grow_int_array(&tour, &tour_cap, tour_len + 1) ||
                    !grow_int_array(&segment_starts, &seg_starts_cap, segment_count + 1) ||
                    !grow_int_array(&segment_ends, &seg_ends_cap, segment_count + 1) ||
                    !grow_int_array(&segment_catches, &seg_catches_cap, segment_count + 1) ||
                    !grow_dist_array(&segment_dists, &seg_dists_cap, segment_count + 1)) {
                    free(station_order);
                    return -1;
                }

                int port_loc = inst->nodes[port_idx].start_loc_id;
                double d_port = get_distance(inst, current_loc_id, port_loc);
                if (d_port > 0.0) current_segment_dist += d_port;
                tour[tour_len++] = port_loc;

                segment_starts[segment_count] = segment_start_idx;
                segment_ends[segment_count] = tour_len - 1;
                segment_catches[segment_count] = current_load;
                segment_dists[segment_count] = current_segment_dist;
                segment_count++;

                current_loc_id = port_loc;
                current_load = 0;
                current_segment_dist = 0.0;
                segment_start_idx = tour_len;
            }
        }

        int stat_start = inst->nodes[st_idx].start_loc_id;
        int stat_end = inst->nodes[st_idx].end_loc_id;

        // Emit station start/end legs and register station->segment mapping.
        if (!grow_int_array(&tour, &tour_cap, tour_len + ((stat_end != stat_start) ? 2 : 1)) ||
            !grow_int_array(&visit_station_ids, &visit_ids_cap, visit_station_count + 1) ||
            !grow_int_array(&visit_station_segment, &visit_seg_cap, visit_station_count + 1)) {
            free(station_order);
            return -1;
        }

        double d1 = get_distance(inst, current_loc_id, stat_start);
        if (d1 > 0.0) current_segment_dist += d1;
        tour[tour_len++] = stat_start;

        if (stat_end != stat_start) {
            double d2 = get_distance(inst, stat_start, stat_end);
            if (d2 > 0.0) current_segment_dist += d2;
            tour[tour_len++] = stat_end;
            current_loc_id = stat_end;
        } else {
            current_loc_id = stat_start;
        }

        current_load += st_amount;
        visit_station_ids[visit_station_count] = inst->nodes[st_idx].table_id;
        visit_station_segment[visit_station_count] = segment_count;
        visit_station_count++;

        if (current_load >= boat_capacity && ord + 1 < station_order_n) {
            // Optional immediate cut once segment reaches capacity threshold.
            int port_idx = find_nearest_port(inst, current_loc_id);
            if (port_idx >= 0) {
                if (!grow_int_array(&tour, &tour_cap, tour_len + 1) ||
                    !grow_int_array(&segment_starts, &seg_starts_cap, segment_count + 1) ||
                    !grow_int_array(&segment_ends, &seg_ends_cap, segment_count + 1) ||
                    !grow_int_array(&segment_catches, &seg_catches_cap, segment_count + 1) ||
                    !grow_dist_array(&segment_dists, &seg_dists_cap, segment_count + 1)) {
                    free(station_order);
                    return -1;
                }

                int port_loc = inst->nodes[port_idx].start_loc_id;
                double d_port = get_distance(inst, current_loc_id, port_loc);
                if (d_port > 0.0) current_segment_dist += d_port;
                tour[tour_len++] = port_loc;

                segment_starts[segment_count] = segment_start_idx;
                segment_ends[segment_count] = tour_len - 1;
                segment_catches[segment_count] = current_load;
                segment_dists[segment_count] = current_segment_dist;
                segment_count++;

                current_loc_id = port_loc;
                current_load = 0;
                current_segment_dist = 0.0;
                segment_start_idx = tour_len;
            }
        }
    }

    if (current_load > 0 || segment_count == 0) {
        // Flush the final segment not yet closed by a port insertion.
        if (!grow_int_array(&segment_starts, &seg_starts_cap, segment_count + 1) ||
            !grow_int_array(&segment_ends, &seg_ends_cap, segment_count + 1) ||
            !grow_int_array(&segment_catches, &seg_catches_cap, segment_count + 1) ||
            !grow_dist_array(&segment_dists, &seg_dists_cap, segment_count + 1)) {
            free(station_order);
            return -1;
        }
        segment_starts[segment_count] = segment_start_idx;
        segment_ends[segment_count] = tour_len - 1;
        segment_catches[segment_count] = current_load;
        segment_dists[segment_count] = current_segment_dist;
        segment_count++;
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


