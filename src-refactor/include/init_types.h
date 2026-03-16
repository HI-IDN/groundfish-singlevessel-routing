#ifndef INIT_TYPES_H
#define INIT_TYPES_H

typedef struct {
    int table_id;      /* stations.id or ports.id */
    int start_loc_id;  /* locations.id used as travel start */
    int end_loc_id;    /* locations.id used as travel end (ports: same as start) */
    int amount;
    int is_port;
} nn_node_t;

typedef struct {
    int num_stations;
    int num_ports;
    nn_node_t *nodes;

    /* Distance matrix: indexed by location ID
     * To access distance from loc_a to loc_b: distances[loc_a][loc_b] */
    double **distances;
    int *loc_to_idx;
    int max_loc_id;
} nn_instance_t;

typedef struct {
    int *tour;
    int tour_length;

    int *visit_station_ids;
    int visit_station_count;
    int *visit_station_segment;
    int *visit_station_direction;

    int segment_count;
    int *segment_starts;
    int *segment_ends;
    int *segment_catches;
    double *segment_dists;

    double total_distance;
    int total_catch;
} nn_solution_t;

#endif

