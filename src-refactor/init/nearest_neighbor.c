/* Nearest Neighbor Heuristic with Capacity-Aware Segmentation
 * Uses pre-loaded distance matrix from database
 */

#include <stdio.h>
#include <stdlib.h>

#include "nearest_neighbor.h"

static int ensure_int_capacity(int **arr, int *cap, int need)
{
    if (need <= *cap) return 1;
    int new_cap = (*cap <= 0) ? 64 : *cap;
    while (new_cap < need) new_cap *= 2;
    int *tmp = (int*)realloc(*arr, (size_t)new_cap * sizeof(int));
    if (!tmp) return 0;
    *arr = tmp;
    *cap = new_cap;
    return 1;
}

static int ensure_double_capacity(double **arr, int *cap, int need)
{
    if (need <= *cap) return 1;
    int new_cap = (*cap <= 0) ? 64 : *cap;
    while (new_cap < need) new_cap *= 2;
    double *tmp = (double*)realloc(*arr, (size_t)new_cap * sizeof(double));
    if (!tmp) return 0;
    *arr = tmp;
    *cap = new_cap;
    return 1;
}

/* Get distance from pre-loaded matrix using location IDs */
static double get_distance(const nn_instance_t *inst, int from_id, int to_id) {
    if (from_id < 0 || from_id >= inst->max_loc_id ||
        to_id < 0 || to_id >= inst->max_loc_id) {
        return -1.0;
    }

    return inst->distances[from_id][to_id];
}

/* Find nearest port from current location ID */
static int find_nearest_port(const nn_instance_t *inst, int from_loc_id) {
    double min_dist = 1e100;
    int nearest_port = -1;

    for (int i = inst->num_stations; i < inst->num_stations + inst->num_ports; i++) {
        double dist = get_distance(inst, from_loc_id, inst->nodes[i].start_loc_id);
        if (dist > 0.0 && dist < min_dist) {
            min_dist = dist;
            nearest_port = i;
        }
    }

    return nearest_port;
}

/* Nearest Neighbor with capacity-aware segmentation */
int nn_solve(const nn_instance_t *inst, nn_solution_t *sol,
             int boat_start_loc_id, int boat_end_loc_id,
             int boat_capacity) {

    int total_nodes = inst->num_stations + inst->num_ports;

    int *visited = (int*)calloc((size_t)total_nodes, sizeof(int));

    int tour_cap = 256;
    int *tour_nodes = (int*)malloc((size_t)tour_cap * sizeof(int));
    int tour_len = 0;

    int seg_starts_cap = 64;
    int seg_ends_cap = 64;
    int seg_catches_cap = 64;
    int seg_dists_cap = 64;
    int *segment_starts = (int*)malloc((size_t)seg_starts_cap * sizeof(int));
    int *segment_ends = (int*)malloc((size_t)seg_ends_cap * sizeof(int));
    int *segment_catches = (int*)malloc((size_t)seg_catches_cap * sizeof(int));
    double *segment_dists = (double*)malloc((size_t)seg_dists_cap * sizeof(double));
    int segment_count = 0;

    int visit_ids_cap = 256;
    int visit_seg_cap = 256;
    int *visit_station_ids = (int*)malloc((size_t)visit_ids_cap * sizeof(int));
    int *visit_station_segment = (int*)malloc((size_t)visit_seg_cap * sizeof(int));
    int visit_station_count = 0;

    if (!visited || !tour_nodes || !segment_starts || !segment_ends || !segment_catches ||
        !segment_dists || !visit_station_ids || !visit_station_segment) {
        free(visited);
        free(tour_nodes);
        free(segment_starts);
        free(segment_ends);
        free(segment_catches);
        free(segment_dists);
        free(visit_station_ids);
        free(visit_station_segment);
        return -1;
    }

    int current_loc_id = boat_start_loc_id;
    int current_load = 0;
    double current_segment_dist = 0.0;
    int segment_start_idx = 0;

    printf("[NN] Start: loc=%d cap=%d stations=%d ports=%d\n",
           boat_start_loc_id, boat_capacity, inst->num_stations, inst->num_ports);

    while (1) {
        double min_dist = 1e100;
        int nearest_station = -1;

        for (int i = 0; i < inst->num_stations; i++) {
            if (!visited[i]) {
                double dist = get_distance(inst, current_loc_id, inst->nodes[i].start_loc_id);
                if (dist > 0.0 && dist < min_dist) {
                    min_dist = dist;
                    nearest_station = i;
                }
            }
        }

        if (nearest_station == -1) break;

        int next_catch = (int)(inst->nodes[nearest_station].amount + 0.5);
        if (current_load + next_catch > boat_capacity && current_load > 0) {
            int nearest_port = find_nearest_port(inst, current_loc_id);

            if (nearest_port != -1) {
                if (!ensure_int_capacity(&tour_nodes, &tour_cap, tour_len + 1)) return -1;
                if (!ensure_int_capacity(&segment_starts, &seg_starts_cap, segment_count + 2)) return -1;
                if (!ensure_int_capacity(&segment_ends, &seg_ends_cap, segment_count + 1)) return -1;
                if (!ensure_int_capacity(&segment_catches, &seg_catches_cap, segment_count + 1)) return -1;
                if (!ensure_double_capacity(&segment_dists, &seg_dists_cap, segment_count + 1)) return -1;

                int port_loc = inst->nodes[nearest_port].start_loc_id;
                tour_nodes[tour_len++] = port_loc;

                double port_dist = get_distance(inst, current_loc_id, port_loc);
                if (port_dist > 0.0) current_segment_dist += port_dist;

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

        int stat_start = inst->nodes[nearest_station].start_loc_id;
        int stat_end = inst->nodes[nearest_station].end_loc_id;

        if (!ensure_int_capacity(&tour_nodes, &tour_cap, tour_len + ((stat_end != stat_start) ? 2 : 1))) return -1;
        if (!ensure_int_capacity(&visit_station_ids, &visit_ids_cap, visit_station_count + 1)) return -1;
        if (!ensure_int_capacity(&visit_station_segment, &visit_seg_cap, visit_station_count + 1)) return -1;

        double d1 = get_distance(inst, current_loc_id, stat_start);
        if (d1 > 0.0) current_segment_dist += d1;
        tour_nodes[tour_len++] = stat_start;

        if (stat_end != stat_start) {
            double d2 = get_distance(inst, stat_start, stat_end);
            if (d2 > 0.0) current_segment_dist += d2;
            tour_nodes[tour_len++] = stat_end;
            current_loc_id = stat_end;
        } else {
            current_loc_id = stat_start;
        }

        current_load += next_catch;
        visited[nearest_station] = 1;

        visit_station_ids[visit_station_count] = inst->nodes[nearest_station].table_id;
        visit_station_segment[visit_station_count] = segment_count;
        visit_station_count++;
    }

    if (current_load > 0 || segment_count == 0) {
        if (!ensure_int_capacity(&segment_starts, &seg_starts_cap, segment_count + 1)) return -1;
        if (!ensure_int_capacity(&segment_ends, &seg_ends_cap, segment_count + 1)) return -1;
        if (!ensure_int_capacity(&segment_catches, &seg_catches_cap, segment_count + 1)) return -1;
        if (!ensure_double_capacity(&segment_dists, &seg_dists_cap, segment_count + 1)) return -1;
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

    sol->tour = tour_nodes;
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

    printf("[NN] Done: segments=%d stations=%d total_dist=%.2f total_catch=%d\n",
           sol->segment_count, sol->visit_station_count, sol->total_distance, sol->total_catch);

    free(visited);
    return 0;
}

