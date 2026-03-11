#ifndef NEAREST_NEIGHBOR_H
#define NEAREST_NEIGHBOR_H

typedef struct {
    int table_id;      /* stations.id or ports.id */
    int start_loc_id;  /* locations.id used as travel start */
    int end_loc_id;    /* locations.id used as travel end (ports: same as start) */
    double amount;
    int is_port;
} nn_node_t;

typedef struct {
    int num_stations;
    int num_ports;
    nn_node_t *nodes;

    /* Distance matrix: indexed by location ID
     * To access distance from loc_a to loc_b: distances[loc_a][loc_b] */
    double **distances;
    int *loc_to_idx;  /* Maps location ID to index in distances matrix */
    int max_loc_id;
} nn_instance_t;

typedef struct {
    int *tour;                  /* location IDs in order */
    int tour_length;

    int *visit_station_ids;     /* station IDs visited in order */
    int visit_station_count;
    int *visit_station_segment; /* segment index for each station visit */

    int segment_count;
    int *segment_starts;        /* index in tour where each segment starts */
    int *segment_ends;          /* index in tour where each segment ends */
    double *segment_catches;    /* catch amount for each segment */
    double *segment_dists;      /* distance for each segment */

    double total_distance;
    double total_catch;
} nn_solution_t;

/* Solve NN heuristic with capacity-aware segmentation using pre-loaded distance matrix */
int nn_solve(const nn_instance_t *inst, nn_solution_t *sol,
             int boat_start_loc_id, int boat_end_loc_id,
             double boat_capacity);

#endif

