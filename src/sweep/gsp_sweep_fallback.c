#include "gsp_sweep_fallback.h"

#ifdef HAVE_GUROBI
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SWEEP_FALLBACK_EPS 1e-6

static int find_station_index(const nn_instance_t *inst, int station_id) {
    if (!inst) return -1;
    for (int i = 0; i < inst->num_stations; i++) {
        if (inst->nodes[i].table_id == station_id) return i;
    }
    return -1;
}

static int station_amount(const nn_instance_t *inst, int station_id) {
    int idx = find_station_index(inst, station_id);
    return (idx >= 0) ? inst->nodes[idx].amount : 0;
}

static int copy_route_segment_local(const gsp_route_segment_t *src, gsp_route_segment_t *dst) {
    if (!src || !dst) return 0;
    memset(dst, 0, sizeof(*dst));
    *dst = *src;
    dst->signed_station_ids = NULL;
    if (src->count > 0) {
        dst->signed_station_ids = (int*)malloc((size_t)src->count * sizeof(int));
        if (!dst->signed_station_ids) return 0;
        memcpy(dst->signed_station_ids, src->signed_station_ids, (size_t)src->count * sizeof(int));
    }
    return 1;
}

static int copy_segment_with_extra_station(const gsp_route_segment_t *src,
                                           int signed_station_id,
                                           gsp_route_segment_t *dst) {
    if (!copy_route_segment_local(src, dst)) return 0;
    if (signed_station_id != 0) {
        int *tmp = (int*)realloc(dst->signed_station_ids,
                                (size_t)(dst->count + 1) * sizeof(int));
        if (!tmp) {
            free(dst->signed_station_ids);
            memset(dst, 0, sizeof(*dst));
            return 0;
        }
        dst->signed_station_ids = tmp;
        dst->signed_station_ids[dst->count++] = signed_station_id;
        dst->capacity = dst->count;
    }
    return 1;
}

static int copy_segment_without_station(const gsp_route_segment_t *src,
                                        int remove_pos,
                                        gsp_route_segment_t *dst) {
    int k = 0;
    if (!src || !dst || remove_pos < 0 || remove_pos >= src->count || src->count <= 1) return 0;
    memset(dst, 0, sizeof(*dst));
    *dst = *src;
    dst->count = src->count - 1;
    dst->capacity = dst->count;
    dst->signed_station_ids = (int*)malloc((size_t)dst->count * sizeof(int));
    if (!dst->signed_station_ids) return 0;
    for (int i = 0; i < src->count; i++) {
        if (i == remove_pos) continue;
        dst->signed_station_ids[k++] = src->signed_station_ids[i];
    }
    return 1;
}

static double station_to_loc_distance(const nn_instance_t *inst, int signed_station_id, int loc_id) {
    int station_idx = find_station_index(inst, abs(signed_station_id));
    double d1, d2;
    if (station_idx < 0) return 1e100;
    if (!inst->distances || loc_id < 0 || loc_id >= inst->max_loc_id) return 1e100;
    d1 = (inst->nodes[station_idx].start_loc_id >= 0 &&
          inst->nodes[station_idx].start_loc_id < inst->max_loc_id &&
          inst->distances[inst->nodes[station_idx].start_loc_id])
        ? inst->distances[inst->nodes[station_idx].start_loc_id][loc_id] : 0.0;
    d2 = (inst->nodes[station_idx].end_loc_id >= 0 &&
          inst->nodes[station_idx].end_loc_id < inst->max_loc_id &&
          inst->distances[inst->nodes[station_idx].end_loc_id])
        ? inst->distances[inst->nodes[station_idx].end_loc_id][loc_id] : 0.0;
    if (d1 <= 0.0) return d2;
    if (d2 <= 0.0) return d1;
    return (d1 < d2) ? d1 : d2;
}

static double station_to_segment_pointset_distance(const nn_instance_t *inst,
                                                   int signed_station_id,
                                                   const gsp_route_segment_t *segment) {
    double best = 1e100;
    if (!inst || !segment) return best;
    {
        double d = station_to_loc_distance(inst, signed_station_id, segment->start_loc_id);
        if (d < best) best = d;
        d = station_to_loc_distance(inst, signed_station_id, segment->end_loc_id);
        if (d < best) best = d;
    }
    for (int i = 0; i < segment->count; i++) {
        int station_idx = find_station_index(inst, abs(segment->signed_station_ids[i]));
        double d;
        if (station_idx < 0) continue;
        d = station_to_loc_distance(inst, signed_station_id, inst->nodes[station_idx].start_loc_id);
        if (d < best) best = d;
        d = station_to_loc_distance(inst, signed_station_id, inst->nodes[station_idx].end_loc_id);
        if (d < best) best = d;
    }
    return best;
}

static int select_fallback_donor(const nn_instance_t *inst,
                                 const gsp_boat_t *boat,
                                 const gsp_route_segment_t *segments,
                                 int segment_count,
                                 int left_idx,
                                 int right_idx,
                                 int boundary_loc_id,
                                 int *out_donor_idx,
                                 int *out_station_pos,
                                 int *out_nearer_left) {
    double best_score = 1e100;
    int best_donor = -1;
    int best_pos = -1;
    int best_left = 1;

    if (!inst || !boat || !segments || segment_count <= 2) return 0;
    for (int s = 0; s < segment_count; s++) {
        int left_prev = (left_idx - 1 + segment_count) % segment_count;
        int right_next = (right_idx + 1) % segment_count;
        if (s == left_idx || s == right_idx) continue;
        if (s == left_prev || s == right_next) continue;
        if (segments[s].count <= 1) continue;
        for (int i = 0; i < segments[s].count; i++) {
            int signed_id = segments[s].signed_station_ids[i];
            int amount = station_amount(inst, abs(signed_id));
            int left_feasible = ((double)(segments[left_idx].catch_amount + amount) <= boat->boat_capacity);
            int right_feasible = ((double)(segments[right_idx].catch_amount + amount) <= boat->boat_capacity);
            double to_left;
            double to_right;
            double to_boundary;
            int nearer_left;
            double score;
            if (!left_feasible && !right_feasible) continue;
            to_left = station_to_segment_pointset_distance(inst, signed_id, &segments[left_idx]);
            to_right = station_to_segment_pointset_distance(inst, signed_id, &segments[right_idx]);
            if (left_feasible && right_feasible) nearer_left = (to_left <= to_right);
            else nearer_left = left_feasible;
            to_boundary = station_to_loc_distance(inst, signed_id, boundary_loc_id);
            score = to_boundary;
            if (to_left < score) score = to_left;
            if (to_right < score) score = to_right;
            if (score < best_score) {
                best_score = score;
                best_donor = s;
                best_pos = i;
                best_left = nearer_left;
            }
        }
    }

    if (best_donor < 0) return 0;
    if (out_donor_idx) *out_donor_idx = best_donor;
    if (out_station_pos) *out_station_pos = best_pos;
    if (out_nearer_left) *out_nearer_left = best_left;
    return 1;
}

int gsp_sweep_try_donor_fallback(
    GRBenv *env,
    const nn_instance_t *inst,
    const gsp_boat_t *boat,
    gsp_route_segment_t *segments,
    int segment_count,
    int left_idx,
    int right_idx,
    int boundary_index,
    int boundary_loc_id,
    int pass_index,
    double l1seg_time_limit_seconds,
    double l2seg_time_limit_seconds,
    int *solve_count,
    gsp_mip_solve_detail_t **solve_details,
    int *solve_detail_count,
    int *solve_detail_capacity,
    gsp_sweep_optimize_boundary_candidate_fn optimize_boundary_candidate,
    gsp_sweep_reoptimize_candidate_segment_fn reoptimize_candidate_segment,
    int *changed_donor_index,
    double *changed_donor_before_distance) {
    gsp_route_segment_t *left = &segments[left_idx];
    gsp_route_segment_t *right = &segments[right_idx];
    int donor_idx = -1, donor_pos = -1, nearer_left = 1;
    int donor_signed_id = 0;
    gsp_route_segment_t fallback_left = {0};
    gsp_route_segment_t fallback_right = {0};
    gsp_route_segment_t fallback_donor = {0};
    int *fallback_left_ids = NULL, *fallback_right_ids = NULL;
    int fallback_left_count = 0, fallback_right_count = 0;
    int fallback_left_catch = 0, fallback_right_catch = 0;
    double fallback_left_dist = 0.0, fallback_right_dist = 0.0;
    double fallback_current_total;
    int accepted = 0;

    if (!optimize_boundary_candidate || !reoptimize_candidate_segment) return 0;
    if (!select_fallback_donor(inst, boat, segments, segment_count, left_idx, right_idx,
                               boundary_loc_id, &donor_idx, &donor_pos, &nearer_left)) {
        return 0;
    }
    donor_signed_id = segments[donor_idx].signed_station_ids[donor_pos];

    if (!copy_segment_with_extra_station(left, nearer_left ? donor_signed_id : 0, &fallback_left)) goto cleanup;
    if (!copy_segment_with_extra_station(right, nearer_left ? 0 : donor_signed_id, &fallback_right)) goto cleanup;
    if (!copy_segment_without_station(&segments[donor_idx], donor_pos, &fallback_donor)) goto cleanup;

    if (!optimize_boundary_candidate(env, inst, boat, &fallback_left, &fallback_right,
                                     boundary_index, left_idx + 1, right_idx + 1,
                                     boundary_loc_id, 1, pass_index,
                                     l1seg_time_limit_seconds, l2seg_time_limit_seconds,
                                     solve_count, solve_details, solve_detail_count,
                                     solve_detail_capacity,
                                     &fallback_left_ids, &fallback_left_count,
                                     &fallback_left_catch, &fallback_left_dist,
                                     &fallback_right_ids, &fallback_right_count,
                                     &fallback_right_catch, &fallback_right_dist)) {
        goto cleanup;
    }
    if (!reoptimize_candidate_segment(env, inst, &segments[donor_idx], &fallback_donor,
                                      pass_index, boundary_index, donor_idx + 1,
                                      l1seg_time_limit_seconds, solve_count,
                                      solve_details, solve_detail_count,
                                      solve_detail_capacity)) {
        goto cleanup;
    }

    fallback_current_total = left->distance_nm + right->distance_nm + segments[donor_idx].distance_nm;
    if (fallback_left_dist + fallback_right_dist + fallback_donor.distance_nm + SWEEP_FALLBACK_EPS < fallback_current_total) {
        double donor_before_distance = segments[donor_idx].distance_nm;
        free(left->signed_station_ids);
        free(right->signed_station_ids);
        free(segments[donor_idx].signed_station_ids);
        left->signed_station_ids = fallback_left_ids;
        right->signed_station_ids = fallback_right_ids;
        segments[donor_idx].signed_station_ids = fallback_donor.signed_station_ids;
        fallback_left_ids = NULL;
        fallback_right_ids = NULL;
        fallback_donor.signed_station_ids = NULL;
        left->count = fallback_left_count;
        right->count = fallback_right_count;
        left->capacity = fallback_left_count;
        right->capacity = fallback_right_count;
        segments[donor_idx].count = fallback_donor.count;
        segments[donor_idx].capacity = fallback_donor.count;
        left->catch_amount = fallback_left_catch;
        right->catch_amount = fallback_right_catch;
        segments[donor_idx].catch_amount = fallback_donor.catch_amount;
        left->distance_nm = fallback_left_dist;
        right->distance_nm = fallback_right_dist;
        segments[donor_idx].distance_nm = fallback_donor.distance_nm;
        if (changed_donor_index) *changed_donor_index = donor_idx;
        if (changed_donor_before_distance) *changed_donor_before_distance = donor_before_distance;
        accepted = 1;
    }

cleanup:
    free(fallback_left.signed_station_ids);
    free(fallback_right.signed_station_ids);
    free(fallback_donor.signed_station_ids);
    free(fallback_left_ids);
    free(fallback_right_ids);
    return accepted;
}
#endif
