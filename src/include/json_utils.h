#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <stdio.h>

#include "init_types.h"

typedef struct {
    const int *values;
    int count;
} gsp_int_list_view_t;

typedef int (*gsp_waypoint_lookup_fn)(const void *ctx,
                                      int from_loc_id,
                                      int to_loc_id,
                                      int **out_waypoint_ids,
                                      int *out_waypoint_count);

typedef void (*gsp_json_extra_writer_fn)(FILE *fp, const char *indent, const void *ctx);

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
    const int *station_count;
    int station_count_count;
    int segment_count;
    const int *segment_catch_amount;
    int segment_catch_count;
    const gsp_distance_breakdown_t *segment_breakdowns;
    const gsp_distance_breakdown_t *grand_total;
    int feasible;
    gsp_json_extra_writer_fn extra_writer;
    const void *extra_ctx;
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
    int has_final_distance_breakdown;
    gsp_distance_breakdown_t final_distance_breakdown;
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

void gsp_summary_reset(gsp_summary_json_t *summary);
void gsp_summary_set_status_and_distance(gsp_summary_json_t *summary,
                                         const char *final_name,
                                         const char *stage_name,
                                         int feasible,
                                         const char *method_name,
                                         int has_baseline,
                                         double baseline_distance_nm,
                                         const double *distance_trajectory_nm,
                                         int distance_trajectory_count,
                                         double final_distance_nm);
void gsp_summary_set_final_distance_breakdown(gsp_summary_json_t *summary,
                                              const gsp_distance_breakdown_t *final_distance_breakdown);
void gsp_summary_set_runtime(gsp_summary_json_t *summary,
                             double preprocessing_seconds,
                             const double *solution_runtime_seconds,
                             int solution_runtime_count,
                             double postprocessing_seconds,
                             double grandtotal_seconds);
void gsp_summary_set_mip(gsp_summary_json_t *summary,
                         int solve_count,
                         double runtime_mean,
                         double runtime_max,
                         double gap_mean,
                         double gap_max);

typedef struct {
    const char *solver_version;
    const char *mode_name;
    const char *strategy_name;
    int boat_id;
    const char *boat_name;
    double boat_lat;
    double boat_lon;
    int boat_location_id;
    gsp_json_extra_writer_fn extra_writer;
    const void *extra_ctx;
} gsp_metadata_json_t;

typedef struct {
    int has_num_nodes;
    int num_nodes;
    int has_num_stations;
    int num_stations;
    int has_num_ports;
    int num_ports;
    int has_capacity;
    double capacity;
    int has_target_capacity;
    double target_capacity;
    int has_target_catch_slack_kg;
    int target_catch_slack_kg;
    gsp_json_extra_writer_fn extra_writer;
    const void *extra_ctx;
} gsp_problem_json_t;

typedef struct {
    const char *status_name;
    const char *gurobi_status_name;
    int gurobi_status_code;
    int include_preprocessing_seconds;
    double preprocessing_seconds;
    double runtime_seconds;
    double total_runtime_seconds;
    const char *method_name;
    double mip_gap;
} gsp_solver_stats_json_t;

void gsp_write_solution_json(FILE *fp,
                             const char *indent,
                             const gsp_solution_json_view_t *view,
                             int trailing_comma);

int gsp_write_segmented_solution_entry_json(FILE *fp,
                                            const char *entry_indent,
                                            const char *value_indent,
                                            const char *label,
                                            const char *variant_name,
                                            const nn_instance_t *inst,
                                            const nn_solution_t *sol,
                                            int boat_start_loc_id,
                                            int boat_end_loc_id,
                                            int is_feasible,
                                            const gsp_distance_breakdown_t *segment_breakdowns,
                                            const gsp_distance_breakdown_t *grand_total,
                                            gsp_waypoint_lookup_fn waypoint_lookup,
                                            const void *waypoint_lookup_ctx);

void gsp_write_metadata_json(FILE *fp,
                             const char *indent,
                             const gsp_metadata_json_t *metadata,
                             int trailing_comma);

void gsp_write_problem_json(FILE *fp,
                            const char *indent,
                            const gsp_problem_json_t *problem,
                            int trailing_comma);

void gsp_write_solver_stats_json(FILE *fp,
                                 const char *indent,
                                 const gsp_solver_stats_json_t *solver_stats,
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
                                     const gsp_distance_breakdown_t *final_distance_breakdown,
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
