#ifndef GSP_DATA_TYPES_H
#define GSP_DATA_TYPES_H

#include "constants.h"

typedef struct {
    int id;
    char type;
    int lat_degmin;
    int lon_degmin;
    double lat_decimal;
    double lon_decimal;
} location_t;

typedef struct {
    int id;
    int ext_id;
    int subid;
    int flag;
    location_t *start;
    location_t *end;
    int botndypi_kastad;
    int botndypi_hift;
    char *remark;
} station_t;

typedef struct {
    int id;
    location_t *start;
    location_t *end;
    int capacity;
    int c1, c2, c3, c4, c5, c6;
    char *name;
} boat_t;

typedef struct {
    int num_nodes;
    int num_stations;
    int num_ports;
    int num_waypoints;

    location_t *nodes;
    station_t *stations;
    boat_t *boats;

    double *dist_matrix;
    int *feasible_matrix;
    double vessel_capacity;

    char *timestamp;
    char *data_source;
} instance_t;

typedef struct {
    char *strategy;
    int *tour;
    int tour_length;
    double total_distance;
    double total_distance_no_return;
    double runtime_seconds;
    int mip_status;
    double mip_gap;
    int mip_iterations;
    char **logs;
    int num_logs;
    char *timestamp;
} init_result_t;

typedef struct {
    int l2seg;
    int iteration;
    double total_distance;
    double improvement;
    double improvement_percent;
    int num_segments_tested;
    int num_segments_improved;
    double elapsed_seconds;
    char *timestamp;
} trajectory_point_t;

typedef struct {
    init_result_t *initial_solution;
    int *best_tour;
    int best_tour_length;
    double best_distance;
    int best_l2seg;
    int best_iteration;

    trajectory_point_t *trajectory;
    int trajectory_length;

    double total_runtime_seconds;
    int total_mip_calls;
    double total_improvement;
    double total_improvement_percent;

    char **logs;
    int num_logs;
    char *timestamp;
} sweep_result_t;

#endif

