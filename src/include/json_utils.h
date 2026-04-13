#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <stdio.h>

#include "init_types.h"

typedef struct {
    const int *values;
    int count;
} gsp_int_list_view_t;

typedef struct {
    const char *variant_name;
    const gsp_int_list_view_t *tour_segments_location_ids;
    int tour_segments_location_count;
    const int *dock_location_ids;
    int dock_location_count;
    const int *unique_waypoint_location_ids;
    int unique_waypoint_location_count;
    const gsp_int_list_view_t *tour_segments_station_ids;
    int tour_segments_station_count;
    const int *tour_length;
    int tour_length_count;
    int segment_count;
    const int *segment_catch_amount;
    int segment_catch_count;
    const gsp_distance_breakdown_t *segment_breakdowns;
    const gsp_distance_breakdown_t *grand_total;
    int feasible;
} gsp_solution_json_view_t;

typedef struct {
    const char *final_name;
    const char *stage_name;
    int feasible;
    const char *method_name;
    int has_baseline;
    double baseline_distance_nm;
    const double *distance_trajectory_nm;
    int distance_trajectory_count;
    double final_distance_nm;
    double preprocessing_seconds;
    const double *solution_runtime_seconds;
    int solution_runtime_count;
    double postprocessing_seconds;
    double grandtotal_seconds;
    int include_runtime;
    int include_mip;
    int mip_solve_count;
    double mip_runtime_mean;
    double mip_runtime_max;
    double mip_gap_mean;
    double mip_gap_max;
} gsp_summary_json_t;

void gsp_write_solution_json(FILE *fp,
                             const char *indent,
                             const gsp_solution_json_view_t *view,
                             int trailing_comma);

void gsp_write_distance_nm_json(FILE *fp,
                                const char *indent,
                                const gsp_distance_breakdown_t *segment_breakdowns,
                                int segment_count,
                                const gsp_distance_breakdown_t *grand_total,
                                int trailing_comma);

void gsp_write_summary_status_json(FILE *fp,
                                   const char *indent,
                                   const char *final_name,
                                   const char *stage_name,
                                   int feasible,
                                   const char *method_name,
                                   int trailing_comma);

void gsp_write_summary_distance_json(FILE *fp,
                                     const char *indent,
                                     int has_baseline,
                                     double baseline_distance_nm,
                                     const double *trajectory_distance_nm,
                                     int trajectory_count,
                                     double final_distance_nm,
                                     int trailing_comma);

void gsp_write_summary_runtime_json(FILE *fp,
                                    const char *indent,
                                    double preprocessing_seconds,
                                    const double *solution_runtime_seconds,
                                    int solution_runtime_count,
                                    double postprocessing_seconds,
                                    double grandtotal_seconds,
                                    int trailing_comma);

void gsp_write_summary_mip_json(FILE *fp,
                                const char *indent,
                                int solve_count,
                                double runtime_mean,
                                double runtime_max,
                                double gap_mean,
                                double gap_max,
                                int trailing_comma);

void gsp_write_summary_json(FILE *fp,
                            const char *indent,
                            const gsp_summary_json_t *summary,
                            int trailing_comma);

int gsp_build_dock_location_ids_from_segment_ends(int boat_start_loc_id,
                                                  const int *segment_end_location_ids,
                                                  int segment_count,
                                                  int **out_dock_location_ids,
                                                  int *out_dock_location_count);

#endif
