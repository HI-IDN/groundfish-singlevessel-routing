#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "construction_ge.h"
#include "init_utils.h"

/* get_distance, find_nearest_port, grow_int_array, grow_dist_array
   are all provided by init_utils.h */

typedef struct {
    int u;
    int v;
    double w;
} ge_edge_t;

static double min_dist_station_pair(const nn_instance_t *inst, int a_idx, int b_idx)
{
    return min_dist_node_pair(inst, a_idx, b_idx);
}

static int ge_edge_cmp(const void *a, const void *b)
{
    const ge_edge_t *ea = (const ge_edge_t*)a;
    const ge_edge_t *eb = (const ge_edge_t*)b;
    int a_nan = isnan(ea->w);
    int b_nan = isnan(eb->w);

    if (a_nan != b_nan) return a_nan ? 1 : -1;
    if (ea->w < eb->w) return -1;
    if (ea->w > eb->w) return 1;
    if (ea->u < eb->u) return -1;
    if (ea->u > eb->u) return 1;
    if (ea->v < eb->v) return -1;
    if (ea->v > eb->v) return 1;
    return 0;
}

static int dsu_find(int *parent, int x)
{
    if (parent[x] == x) return x;
    parent[x] = dsu_find(parent, parent[x]);
    return parent[x];
}

static void dsu_union(int *parent, int a, int b)
{
    int ra = dsu_find(parent, a);
    int rb = dsu_find(parent, b);
    if (ra != rb) parent[rb] = ra;
}

static int build_station_order(const nn_instance_t *inst, int boat_start_loc_id, int boat_end_loc_id,
                               int **out_order, int *out_n)
{
    int n = inst->num_stations;
    int *order = NULL;
    ge_edge_t *edges = NULL;
    int *deg = NULL;
    int *adj1 = NULL;
    int *adj2 = NULL;
    int *parent = NULL;
    int edge_count = 0;
    int used_edges = 0;

    (void)boat_start_loc_id;
    (void)boat_end_loc_id;

    if (n == 0) {
        *out_order = NULL;
        *out_n = 0;
        return 1;
    }
    if (n == 1) {
        order = (int*)malloc(sizeof(int));
        if (!order) return 0;
        order[0] = 0;
        *out_order = order;
        *out_n = 1;
        return 1;
    }

    edge_count = n * (n - 1) / 2;
    edges = (ge_edge_t*)malloc((size_t)edge_count * sizeof(ge_edge_t));
    deg = (int*)calloc((size_t)n, sizeof(int));
    adj1 = (int*)malloc((size_t)n * sizeof(int));
    adj2 = (int*)malloc((size_t)n * sizeof(int));
    parent = (int*)malloc((size_t)n * sizeof(int));
    if (!edges || !deg || !adj1 || !adj2 || !parent) {
        free(edges);
        free(deg);
        free(adj1);
        free(adj2);
        free(parent);
        return 0;
    }

    {
        int eidx = 0;
        for (int i = 0; i < n; i++) {
            adj1[i] = -1;
            adj2[i] = -1;
            parent[i] = i;
            for (int j = i + 1; j < n; j++) {
                edges[eidx].u = i;
                edges[eidx].v = j;
                edges[eidx].w = min_dist_station_pair(inst, i, j);
                eidx++;
            }
        }
    }

    qsort(edges, (size_t)edge_count, sizeof(ge_edge_t), ge_edge_cmp);

    for (int i = 0; i < edge_count && used_edges < n; i++) {
        int u = edges[i].u;
        int v = edges[i].v;
        int ru, rv;

        if (edges[i].w < 0.0) continue;
        if (deg[u] >= 2 || deg[v] >= 2) continue;

        ru = dsu_find(parent, u);
        rv = dsu_find(parent, v);
        if (ru == rv) {
            if (used_edges != n - 1) continue;
        }

        if (adj1[u] == -1) adj1[u] = v;
        else adj2[u] = v;
        if (adj1[v] == -1) adj1[v] = u;
        else adj2[v] = u;
        deg[u]++;
        deg[v]++;
        used_edges++;
        dsu_union(parent, u, v);
    }

    {
        int ok = (used_edges == n);
        for (int i = 0; i < n; i++) {
            if (deg[i] != 2) {
                ok = 0;
                break;
            }
        }
        if (!ok) {
            fprintf(stderr, "[GE] Warning: failed to build full greedy-edge cycle, falling back to station order\n");
            order = (int*)malloc((size_t)n * sizeof(int));
            if (!order) {
                free(edges);
                free(deg);
                free(adj1);
                free(adj2);
                free(parent);
                return 0;
            }
            for (int i = 0; i < n; i++) order[i] = i;
            free(edges);
            free(deg);
            free(adj1);
            free(adj2);
            free(parent);
            *out_order = order;
            *out_n = n;
            return 1;
        }
    }

    order = (int*)malloc((size_t)n * sizeof(int));
    if (!order) {
        free(edges);
        free(deg);
        free(adj1);
        free(adj2);
        free(parent);
        return 0;
    }

    {
        int cur = 0;
        int prev = -1;
        for (int i = 0; i < n; i++) {
            int next;
            order[i] = cur;
            next = (adj1[cur] != prev) ? adj1[cur] : adj2[cur];
            prev = cur;
            cur = next;
        }
    }

    free(edges);
    free(deg);
    free(adj1);
    free(adj2);
    free(parent);
    *out_order = order;
    *out_n = n;
    return 1;
}

static void zero_solution(nn_solution_t *sol)
{
    if (!sol) return;
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

static int build_noport_solution_ge(const nn_instance_t *inst, const int *station_order, int station_order_n,
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
            free(tour);
            free(visit_station_ids);
            free(visit_station_segment);
            free(visit_station_direction);
            free(segment_starts);
            free(segment_ends);
            free(segment_catches);
            free(segment_dists);
            return 0;
        }
        if (!grow_int_array(&tour, &tour_cap, tour_len + ((stat_exit != stat_entry) ? 2 : 1)) ||
            !grow_int_array(&visit_station_ids, &visit_ids_cap, ord + 1) ||
            !grow_int_array(&visit_station_segment, &visit_seg_cap, ord + 1) ||
            !grow_int_array(&visit_station_direction, &visit_dir_cap, ord + 1)) {
            free(tour);
            free(visit_station_ids);
            free(visit_station_segment);
            free(visit_station_direction);
            free(segment_starts);
            free(segment_ends);
            free(segment_catches);
            free(segment_dists);
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
        free(tour);
        free(visit_station_ids);
        free(visit_station_segment);
        free(visit_station_direction);
        free(segment_starts);
        free(segment_ends);
        free(segment_catches);
        free(segment_dists);
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

int gi_solve(const nn_instance_t *inst, nn_solution_t *sol,
             nn_solution_t *pre_capacity_sol,
             int boat_start_loc_id, int boat_end_loc_id,
             int boat_capacity)
{
    int *station_order = NULL;
    int station_order_n = 0;
    if (!build_station_order(inst, boat_start_loc_id, boat_end_loc_id, &station_order, &station_order_n))
        return -1;
    if (pre_capacity_sol) {
        zero_solution(pre_capacity_sol);
        if (!build_noport_solution_ge(inst, station_order, station_order_n,
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

    printf("[GI] Start: loc=%d cap=%d stations=%d ports=%d\n",
           boat_start_loc_id, boat_capacity, inst->num_stations, inst->num_ports);

    for (int ord = 0; ord < station_order_n; ord++) {
        int station_idx = station_order[ord];
        int next_catch = inst->nodes[station_idx].amount;

        // Pre-station trigger: if next station exceeds capacity, insert port first.
        if (current_load + next_catch > boat_capacity && current_load > 0) {
            int nearest_port = find_nearest_port(inst, current_loc_id);
            if (nearest_port < 0) { free(station_order); free_partial_solution(sol); return -1; }
            int new_loc, new_seg_start;
            if (!insert_port_segment(inst, nearest_port, current_loc_id,
                    &tour, &tour_cap, &tour_len,
                    &segment_starts, &seg_starts_cap,
                    &segment_ends,   &seg_ends_cap,
                    &segment_catches,&seg_catches_cap,
                    &segment_dists,  &seg_dists_cap,
                    &segment_count, segment_start_idx,
                    current_load, &current_segment_dist,
                    &new_loc, &new_seg_start)) {
                free(station_order); free_partial_solution(sol); return -1;
            }
            current_loc_id = new_loc;
            current_load = 0;
            segment_start_idx = new_seg_start;
        }

        int stat_entry, stat_exit;
        double stat_added = 0.0;
        if (!choose_station_orientation(inst, current_loc_id, station_idx, &stat_entry, &stat_exit, &stat_added)) {
            free(station_order); free_partial_solution(sol); return -1;
        }

        // Emit station traversal and attach it to the current segment id.
        if (!grow_int_array(&tour,                &tour_cap,      tour_len + ((stat_exit != stat_entry) ? 2 : 1)) ||
            !grow_int_array(&visit_station_ids,   &visit_ids_cap, visit_station_count + 1) ||
            !grow_int_array(&visit_station_segment,&visit_seg_cap,visit_station_count + 1) ||
            !grow_int_array(&visit_station_direction,&visit_dir_cap,visit_station_count + 1)) {
            free(station_order); free_partial_solution(sol); return -1;
        }

        if (stat_added > 0.0) current_segment_dist += stat_added;
        tour[tour_len++] = stat_entry;
        if (stat_exit != stat_entry) {
            tour[tour_len++] = stat_exit;
        }
        current_loc_id = stat_exit;

        current_load += next_catch;
        visit_station_ids[visit_station_count]     = inst->nodes[station_idx].table_id;
        visit_station_segment[visit_station_count] = segment_count;
        visit_station_direction[visit_station_count] = (stat_entry == inst->nodes[station_idx].end_loc_id &&
                                                        stat_exit == inst->nodes[station_idx].start_loc_id) ? -1 : 1;
        visit_station_count++;

        // Post-station trigger: if load at capacity and more stations remain, insert port.
        if (current_load >= boat_capacity && ord + 1 < station_order_n) {
            int nearest_port = find_nearest_port(inst, current_loc_id);
            if (nearest_port >= 0) {
                int new_loc, new_seg_start;
                if (!insert_port_segment(inst, nearest_port, current_loc_id,
                        &tour, &tour_cap, &tour_len,
                        &segment_starts, &seg_starts_cap,
                        &segment_ends,   &seg_ends_cap,
                        &segment_catches,&seg_catches_cap,
                        &segment_dists,  &seg_dists_cap,
                        &segment_count, segment_start_idx,
                        current_load, &current_segment_dist,
                        &new_loc, &new_seg_start)) {
                    free(station_order); free_partial_solution(sol); return -1;
                }
                current_loc_id = new_loc;
                current_load = 0;
                segment_start_idx = new_seg_start;
            }
        }
    }

    // Flush final open segment.
    if (!flush_final_segment(
            &segment_starts, &seg_starts_cap,
            &segment_ends,   &seg_ends_cap,
            &segment_catches,&seg_catches_cap,
            &segment_dists,  &seg_dists_cap,
            &segment_count, segment_start_idx,
            tour_len, current_load, current_segment_dist)) {
        free(station_order); free_partial_solution(sol); return -1;
    }

    double total_dist = 0.0;
    for (int i = 0; i < segment_count; i++) total_dist += segment_dists[i];
    double return_dist = get_distance(inst, current_loc_id, boat_end_loc_id);
    if (return_dist > 0.0) total_dist += return_dist;

    sol->tour               = tour;
    sol->tour_length        = tour_len;
    sol->visit_station_ids  = visit_station_ids;
    sol->visit_station_count= visit_station_count;
    sol->visit_station_segment = visit_station_segment;
    sol->visit_station_direction = visit_station_direction;
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

int ge_construction_solve(const nn_instance_t *inst, nn_solution_t *sol,
                          int boat_start_loc_id, int boat_end_loc_id)
{
    int *station_order = NULL;
    int station_order_n = 0;
    int ok;

    if (!build_station_order(inst, boat_start_loc_id, boat_end_loc_id,
                             &station_order, &station_order_n)) {
        return -1;
    }

    ok = build_ordered_solution_from_station_order(inst, station_order, station_order_n,
                                                   sol, boat_start_loc_id, boat_end_loc_id);
    free(station_order);
    return ok ? 0 : -1;
}

