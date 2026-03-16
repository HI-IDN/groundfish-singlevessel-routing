/* Nearest Neighbor Heuristic with Capacity-Aware Segmentation
 *
 * 1. Start from boat location with empty load
 * 2. Greedily pick nearest unvisited station that fits capacity
 * 3. If capacity blocks all options, retry without capacity filter (fallback)
 * 4. When no station is reachable, insert nearest port and reset load
 * 5. After visiting a station, if load >= capacity and more remain, insert port
 * 6. Segment boundaries occur at port insertions
 */

#include <stdio.h>
#include <stdlib.h>

#include "nearest_neighbor.h"
#include "init_utils.h"

int nn_solve(const nn_instance_t *inst, nn_solution_t *sol,
             int boat_start_loc_id, int boat_end_loc_id,
             int boat_capacity)
{
    int total_nodes = inst->num_stations + inst->num_ports;
    int *visited = (int*)calloc((size_t)total_nodes, sizeof(int));

    // Growable containers for tour and segments
    int tour_cap = 256;
    int *tour_nodes = (int*)malloc((size_t)tour_cap * sizeof(int));
    int tour_len = 0;

    int seg_starts_cap = 64, seg_ends_cap = 64, seg_catches_cap = 64, seg_dists_cap = 64;
    int *segment_starts = (int*)malloc((size_t)seg_starts_cap * sizeof(int));
    int *segment_ends = (int*)malloc((size_t)seg_ends_cap * sizeof(int));
    int *segment_catches = (int*)malloc((size_t)seg_catches_cap * sizeof(int));
    double *segment_dists = (double*)malloc((size_t)seg_dists_cap * sizeof(double));
    int segment_count = 0;

    int visit_ids_cap = 256, visit_seg_cap = 256, visit_dir_cap = 256;
    int *visit_station_ids = (int*)malloc((size_t)visit_ids_cap * sizeof(int));
    int *visit_station_segment = (int*)malloc((size_t)visit_seg_cap * sizeof(int));
    int *visit_station_direction = (int*)malloc((size_t)visit_seg_cap * sizeof(int));
    int visit_station_count = 0;

    if (!visited || !tour_nodes || !segment_starts || !segment_ends || !segment_catches ||
        !segment_dists || !visit_station_ids || !visit_station_segment || !visit_station_direction) {
        free(visited); free(tour_nodes); free(segment_starts); free(segment_ends);
        free(segment_catches); free(segment_dists); free(visit_station_ids);
        free(visit_station_segment); free(visit_station_direction);
        return -1;
    }

    int current_anchor_idx = -1;  // -1 = boat; otherwise station/port object index for legacy NN ordering
    int current_loc_id = boat_start_loc_id;  // actual current emitted location for route construction
    int current_load = 0;
    double current_segment_dist = 0.0;
    int segment_start_idx = 0;
    int remaining_stations = inst->num_stations;

    printf("[NN] Start: boat_start=%d boat_end=%d cap=%d stations=%d ports=%d\n",
           boat_start_loc_id, boat_end_loc_id, boat_capacity, inst->num_stations, inst->num_ports);

    while (remaining_stations > 0) {
        double best_dist = 1e100;
        int best_station_idx = -1;
        int best_entry = -1, best_exit = -1, best_dir = 0;

        // Phase 1: Find nearest unvisited station that fits capacity
        for (int i = 0; i < inst->num_stations; i++) {
            double cand_dist;
            if (visited[i]) continue;
            int station_amount = inst->nodes[i].amount;
            if (current_load > 0 && current_load + station_amount > boat_capacity) continue;

            cand_dist = (current_anchor_idx < 0)
                ? min_dist_from_loc_to_node(inst, boat_start_loc_id, i)
                : min_dist_node_pair(inst, current_anchor_idx, i);
            if (cand_dist > 0.0 && cand_dist < best_dist) {
                best_dist = cand_dist;
                best_station_idx = i;
            }
        }

        // Phase 2: If no candidate found and load is empty, retry without capacity filter
        if (best_station_idx < 0 && current_load <= 0) {
            for (int i = 0; i < inst->num_stations; i++) {
                double cand_dist;
                if (visited[i]) continue;
                cand_dist = (current_anchor_idx < 0)
                    ? min_dist_from_loc_to_node(inst, boat_start_loc_id, i)
                    : min_dist_node_pair(inst, current_anchor_idx, i);
                if (cand_dist > 0.0 && cand_dist < best_dist) {
                    best_dist = cand_dist;
                    best_station_idx = i;
                }
            }
        }

        // Phase 3: If still no station, insert nearest port and reset
        if (best_station_idx < 0) {
            if (current_anchor_idx < 0) {
                printf("[NN] No feasible station from boat start\n");
                break;
            }
            int nearest_port = find_nearest_port_from_node_pair(inst, current_anchor_idx);
            if (nearest_port < 0) {
                printf("[NN] No port available (load=%d)\n", current_load);
                break;
            }
            int new_loc, new_seg_start;
            if (!insert_port_segment(inst, nearest_port,
                    current_loc_id,
                    &tour_nodes, &tour_cap, &tour_len,
                    &segment_starts, &seg_starts_cap,
                    &segment_ends,   &seg_ends_cap,
                    &segment_catches,&seg_catches_cap,
                    &segment_dists,  &seg_dists_cap,
                    &segment_count, segment_start_idx,
                    current_load, &current_segment_dist,
                    &new_loc, &new_seg_start)) {
                free(visited); free(tour_nodes); free(segment_starts); free(segment_ends);
                free(segment_catches); free(segment_dists); free(visit_station_ids);
                free(visit_station_segment); free(visit_station_direction);
                return -1;
            }
            current_anchor_idx = nearest_port;
            current_loc_id = new_loc;
            current_load = 0;
            segment_start_idx = new_seg_start;
            continue;
        }

        // Phase 4: Emit selected station using the best concrete orientation from the actual current location.
        int station_amount = inst->nodes[best_station_idx].amount;
        int stat_entry = -1, stat_exit = -1, stat_dir = 0;
        double stat_added_dist = 0.0;

        if (!choose_station_orientation_with_dir(inst, current_loc_id, best_station_idx,
                                                 &stat_entry, &stat_exit,
                                                 &stat_added_dist, &stat_dir)) {
            free(visited); free(tour_nodes); free(segment_starts); free(segment_ends);
            free(segment_catches); free(segment_dists); free(visit_station_ids);
            free(visit_station_segment); free(visit_station_direction);
            return -1;
        }

        if (!grow_int_array(&tour_nodes, &tour_cap, tour_len + ((stat_exit != stat_entry) ? 2 : 1)) ||
            !grow_int_array(&visit_station_ids, &visit_ids_cap, visit_station_count + 1) ||
            !grow_int_array(&visit_station_segment, &visit_seg_cap, visit_station_count + 1) ||
            !grow_int_array(&visit_station_direction, &visit_dir_cap, visit_station_count + 1)) {
            free(visited); free(tour_nodes); free(segment_starts); free(segment_ends);
            free(segment_catches); free(segment_dists); free(visit_station_ids);
            free(visit_station_segment); free(visit_station_direction);
            return -1;
        }

        if (stat_added_dist > 0.0) current_segment_dist += stat_added_dist;
        tour_nodes[tour_len++] = stat_entry;
        if (stat_exit != stat_entry) {
            tour_nodes[tour_len++] = stat_exit;
        }

        current_load += station_amount;
        visited[best_station_idx] = 1;
        visit_station_ids[visit_station_count] = inst->nodes[best_station_idx].table_id;
        visit_station_segment[visit_station_count] = segment_count;
        visit_station_direction[visit_station_count] = stat_dir;
        visit_station_count++;
        current_anchor_idx = best_station_idx;
        current_loc_id = stat_exit;
        remaining_stations--;

        // Phase 5: After adding station, if load at capacity and more remain, insert port
        if (current_load >= boat_capacity && remaining_stations > 0) {
            int nearest_port = find_nearest_port_from_node_pair(inst, current_anchor_idx);
            if (nearest_port >= 0) {
                int new_loc, new_seg_start;
                if (!insert_port_segment(inst, nearest_port,
                        current_loc_id,
                        &tour_nodes, &tour_cap, &tour_len,
                        &segment_starts, &seg_starts_cap,
                        &segment_ends,   &seg_ends_cap,
                        &segment_catches,&seg_catches_cap,
                        &segment_dists,  &seg_dists_cap,
                        &segment_count, segment_start_idx,
                        current_load, &current_segment_dist,
                        &new_loc, &new_seg_start)) {
                    free(visited); free(tour_nodes); free(segment_starts); free(segment_ends);
                    free(segment_catches); free(segment_dists); free(visit_station_ids);
                    free(visit_station_segment); free(visit_station_direction);
                    return -1;
                }
                current_anchor_idx = nearest_port;
                current_loc_id = new_loc;
                current_load = 0;
                segment_start_idx = new_seg_start;
            }
        }
    }

    // Flush final segment
    if (current_load > 0 || segment_count == 0) {
        if (!flush_final_segment(
                &segment_starts, &seg_starts_cap,
                &segment_ends,   &seg_ends_cap,
                &segment_catches,&seg_catches_cap,
                &segment_dists,  &seg_dists_cap,
                &segment_count, segment_start_idx,
                tour_len, current_load, current_segment_dist)) {
            free(visited); free(tour_nodes); free(segment_starts); free(segment_ends);
            free(segment_catches); free(segment_dists); free(visit_station_ids);
            free(visit_station_segment); free(visit_station_direction);
            return -1;
        }
    }

    // Calculate total distance including return to boat end
    double total_dist = 0.0;
    for (int i = 0; i < segment_count; i++) total_dist += segment_dists[i];
    {
        double d_return = get_distance(inst, current_loc_id, boat_end_loc_id);
        if (d_return > 0.0) total_dist += d_return;
    }

    sol->tour = tour_nodes;
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

    printf("[NN] End: segments=%d stations=%d total_dist=%.2f total_catch=%d\n",
           sol->segment_count, sol->visit_station_count, sol->total_distance, sol->total_catch);

    free(visited);
    return 0;
}

