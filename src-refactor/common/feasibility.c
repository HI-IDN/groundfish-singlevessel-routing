#include <stdlib.h>
#include "../include/feasibility.h"

int stations_have_no_duplicates(const int *station_ids, int n_station_ids)
{
    if (!station_ids || n_station_ids <= 0) return 1;

    int max_id = 0;
    for (int i = 0; i < n_station_ids; i++) {
        if (station_ids[i] > max_id) max_id = station_ids[i];
    }
    if (max_id <= 0) return 1;

    int *count = (int*)calloc((size_t)(max_id + 1), sizeof(int));
    if (!count) return 0;

    int ok = 1;
    for (int i = 0; i < n_station_ids; i++) {
        int id = station_ids[i];
        if (id <= 0 || id > max_id) continue;
        count[id]++;
        if (count[id] > 1) {
            ok = 0;
            break;
        }
    }

    free(count);
    return ok;
}

int segments_within_capacity(const int *segment_catches, int n_segments, double capacity)
{
    if (!segment_catches || n_segments <= 0) return 1;
    for (int i = 0; i < n_segments; i++) {
        if ((double)segment_catches[i] > capacity) return 0;
    }
    return 1;
}
