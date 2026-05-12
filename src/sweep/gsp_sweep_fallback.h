#ifndef GSP_SWEEP_FALLBACK_H
#define GSP_SWEEP_FALLBACK_H

#include "../include/init_types.h"
#include "../include/mip_report.h"

#ifdef HAVE_GUROBI
#include <gurobi_c.h>

typedef int (*gsp_sweep_optimize_boundary_candidate_fn)(
    GRBenv *env,
    const nn_instance_t *inst,
    const gsp_boat_t *boat,
    const gsp_route_segment_t *left,
    const gsp_route_segment_t *right,
    int boundary_index,
    int left_segment_index,
    int right_segment_index,
    int boundary_loc_id,
    int fallback_used,
    int pass_index,
    double l1seg_time_limit_seconds,
    double l2seg_time_limit_seconds,
    int *solve_count,
    gsp_mip_solve_detail_t **solve_details,
    int *solve_detail_count,
    int *solve_detail_capacity,
    int **best_left_ids,
    int *best_left_count,
    int *best_left_catch,
    double *best_left_dist,
    int **best_right_ids,
    int *best_right_count,
    int *best_right_catch,
    double *best_right_dist);

typedef int (*gsp_sweep_reoptimize_candidate_segment_fn)(
    GRBenv *env,
    const nn_instance_t *inst,
    const gsp_route_segment_t *baseline,
    gsp_route_segment_t *candidate,
    int pass_index,
    int boundary_index,
    int segment_index,
    double l1seg_time_limit_seconds,
    int *solve_count,
    gsp_mip_solve_detail_t **solve_details,
    int *solve_detail_count,
    int *solve_detail_capacity);

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
    double *changed_donor_before_distance);
#endif

#endif
