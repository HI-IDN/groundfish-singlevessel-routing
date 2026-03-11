#ifndef FEASIBILITY_H
#define FEASIBILITY_H

/* Returns 1 if station IDs have no duplicates, 0 otherwise. */
int stations_have_no_duplicates(const int *station_ids, int n_station_ids);

/* Returns 1 if every segment catch is <= capacity, 0 otherwise. */
int segments_within_capacity(const int *segment_catches, int n_segments, double capacity);

#endif
