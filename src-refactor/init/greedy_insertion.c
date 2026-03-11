#include <stdio.h>
#include <stdlib.h>

#include "greedy_insertion.h"
#include "init_utils.h"

/* get_distance, find_nearest_port, grow_int_array, grow_dist_array
   are all provided by init_utils.h */

static int build_station_order(const nn_instance_t *inst, int boat_start_loc_id, int boat_end_loc_id,
                               int **out_order, int *out_n)
{
    int n = inst->num_stations;
    int *order = NULL;
    int order_cap = 0;
    int order_n = 0;
    int *used = (int*)calloc((size_t)n, sizeof(int));
    if (!used) return 0;

    if (n == 0) {
        *out_order = NULL;
        *out_n = 0;
        free(used);
        return 1;
    }

    int seed = -1;
    double best_seed = 1e100;
    // Seed with station closest to boat start, then insert by least delta.
    for (int i = 0; i < n; i++) {
        double d = get_distance(inst, boat_start_loc_id, inst->nodes[i].start_loc_id);
        if (d > 0.0 && d < best_seed) { best_seed = d; seed = i; }
    }
    if (seed < 0) { free(used); return 0; }

    if (!grow_int_array(&order, &order_cap, 1)) { free(used); return 0; }
    order[order_n++] = seed;
    used[seed] = 1;

    while (order_n < n) {
        double best_delta = 1e100;
        int best_station = -1;
        int best_pos = -1;

        // Evaluate every (unvisited station, insertion position) pair.
        for (int cand = 0; cand < n; cand++) {
            if (used[cand]) continue;
            for (int pos = 0; pos <= order_n; pos++) {
                int prev_loc  = (pos == 0)       ? boat_start_loc_id : inst->nodes[order[pos - 1]].end_loc_id;
                int next_loc  = (pos == order_n) ? boat_end_loc_id   : inst->nodes[order[pos]].start_loc_id;
                int cand_start = inst->nodes[cand].start_loc_id;
                int cand_end   = inst->nodes[cand].end_loc_id;

                double d_prev_next = get_distance(inst, prev_loc,   next_loc);
                double d_prev_cand = get_distance(inst, prev_loc,   cand_start);
                double d_inside    = get_distance(inst, cand_start, cand_end);
                double d_cand_next = get_distance(inst, cand_end,   next_loc);

                if (d_prev_cand < 0.0 || d_inside < 0.0 || d_cand_next < 0.0) continue;
                if (d_prev_next < 0.0) d_prev_next = 0.0;

                double delta = d_prev_cand + d_inside + d_cand_next - d_prev_next;
                if (delta < best_delta) { best_delta = delta; best_station = cand; best_pos = pos; }
            }
        }

        if (best_station < 0 || best_pos < 0) { free(order); free(used); return 0; }
        if (!grow_int_array(&order, &order_cap, order_n + 1)) { free(order); free(used); return 0; }

        for (int i = order_n; i > best_pos; i--) order[i] = order[i - 1];
        order[best_pos] = best_station;
        order_n++;
        used[best_station] = 1;
    }

    free(used);
    *out_order = order;
    *out_n = order_n;
    return 1;
}

int gi_solve(const nn_instance_t *inst, nn_solution_t *sol,
             int boat_start_loc_id, int boat_end_loc_id,
             int boat_capacity)
{
    int *station_order = NULL;
    int station_order_n = 0;
    if (!build_station_order(inst, boat_start_loc_id, boat_end_loc_id, &station_order, &station_order_n))
        return -1;

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

    printf("[GI] Start: loc=%d cap=%d stations=%d ports=%d\n",
           boat_start_loc_id, boat_capacity, inst->num_stations, inst->num_ports);

    for (int ord = 0; ord < station_order_n; ord++) {
        int station_idx = station_order[ord];
        int next_catch = (int)(inst->nodes[station_idx].amount + 0.5);

        // Split segment at nearest port when next station would exceed capacity.
        if (current_load + next_catch > boat_capacity && current_load > 0) {
            int nearest_port = find_nearest_port(inst, current_loc_id);
            if (nearest_port < 0) { free(station_order); return -1; }

            if (!grow_int_array(&tour,            &tour_cap,        tour_len + 1)      ||
                !grow_int_array(&segment_starts,  &seg_starts_cap,  segment_count + 1) ||
                !grow_int_array(&segment_ends,    &seg_ends_cap,    segment_count + 1) ||
                !grow_int_array(&segment_catches, &seg_catches_cap, segment_count + 1) ||
                !grow_dist_array(&segment_dists,  &seg_dists_cap,   segment_count + 1)) {
                free(station_order); return -1;
            }

            int port_loc = inst->nodes[nearest_port].start_loc_id;
            double d_port = get_distance(inst, current_loc_id, port_loc);
            if (d_port > 0.0) current_segment_dist += d_port;
            tour[tour_len++] = port_loc;
            segment_starts[segment_count]  = segment_start_idx;
            segment_ends[segment_count]    = tour_len - 1;
            segment_catches[segment_count] = current_load;
            segment_dists[segment_count]   = current_segment_dist;
            segment_count++;

            current_loc_id = port_loc;
            current_load = 0;
            current_segment_dist = 0.0;
            segment_start_idx = tour_len;
        }

        int stat_start = inst->nodes[station_idx].start_loc_id;
        int stat_end   = inst->nodes[station_idx].end_loc_id;
        // Emit station traversal and attach it to the current segment id.
        if (!grow_int_array(&tour,                &tour_cap,      tour_len + ((stat_end != stat_start) ? 2 : 1)) ||
            !grow_int_array(&visit_station_ids,   &visit_ids_cap, visit_station_count + 1) ||
            !grow_int_array(&visit_station_segment,&visit_seg_cap,visit_station_count + 1)) {
            free(station_order); return -1;
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

        current_load += next_catch;
        visit_station_ids[visit_station_count]     = inst->nodes[station_idx].table_id;
        visit_station_segment[visit_station_count] = segment_count;
        visit_station_count++;
    }

    if (!grow_int_array(&segment_starts,  &seg_starts_cap,  segment_count + 1) ||
        !grow_int_array(&segment_ends,    &seg_ends_cap,    segment_count + 1) ||
        !grow_int_array(&segment_catches, &seg_catches_cap, segment_count + 1) ||
        !grow_dist_array(&segment_dists,  &seg_dists_cap,   segment_count + 1)) {
        free(station_order); return -1;
    }
    // Flush final open segment.
    segment_starts[segment_count]  = segment_start_idx;
    segment_ends[segment_count]    = tour_len - 1;
    segment_catches[segment_count] = current_load;
    segment_dists[segment_count]   = current_segment_dist;
    segment_count++;

    double total_dist = 0.0;
    for (int i = 0; i < segment_count; i++) total_dist += segment_dists[i];
    double return_dist = get_distance(inst, current_loc_id, boat_end_loc_id);
    if (return_dist > 0.0) total_dist += return_dist;

    sol->tour               = tour;
    sol->tour_length        = tour_len;
    sol->visit_station_ids  = visit_station_ids;
    sol->visit_station_count= visit_station_count;
    sol->visit_station_segment = visit_station_segment;
    sol->segment_count      = segment_count;
    sol->segment_starts     = segment_starts;
    sol->segment_ends       = segment_ends;
    sol->segment_catches    = segment_catches;
    sol->segment_dists      = segment_dists;
    sol->total_distance     = total_dist;
    sol->total_catch        = 0;
    for (int i = 0; i < segment_count; i++) sol->total_catch += segment_catches[i];

    printf("[GI] Done: segments=%d stations=%d total_dist=%.2f total_catch=%d\n",
           sol->segment_count, sol->visit_station_count, sol->total_distance, sol->total_catch);

    free(station_order);
    return 0;
}

