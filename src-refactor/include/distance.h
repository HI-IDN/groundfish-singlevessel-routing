#ifndef GSP_DISTANCE_H
#define GSP_DISTANCE_H

/* location_data structure - expanded data for optimization */
typedef struct {
    int *Type;
    int *ItemIndex;
    double *Amount;
    double *LatLonRad;
    double *LatLonDegMin;
    int SelectedSize;
    int Size;
} location_data;

/**
 * Compute distance and feasibility matrices using Dijkstra waypoint routing
 * Accounts for land obstacles from island.bin
 * Internal implementation - not external
 */
int distance_link(double *DistrMtrx, int *FsbleMtrx, int *Type,
                 double *LatLon[4], double *StartEnd,
                 int Size, int SelectedSize);

/**
 * Build distance and feasibility matrices using waypoint routing.
 *
 * @param ex          ExData structure
 * @param Land        Land polygon data (optional, can be NULL)
 * @param nLand       Number of land points
 * @param out_dist    Output distance matrix [2*Size][2*Size]
 * @param out_fsb     Output feasibility matrix [2*Size][2*Size]
 * @param out_full_dist  Output full distance matrix [2*SelectedSize][2*SelectedSize] (optional)
 * @param out_full_fsb   Output full feasibility matrix [2*SelectedSize][2*SelectedSize] (optional)
 * @param out_full_m     Output full matrix dimension (optional)
 */
void build_waypoint_dist(const location_data *ex,
                        const double *Land, int nLand,
                        double **out_dist, int **out_fsb,
                        double **out_full_dist, int **out_full_fsb,
                        int *out_full_m);

/**
 * Load island.bin file (land polygon data)
 */
double *load_island_bin(const char *fname, int *out_n);

/**
 * Compute distance matrix for locations using waypoint-aware Dijkstra routing
 * Pure computation function - no database operations
 *
 * @param n_locs - Number of locations (excluding waypoints)
 * @param latlon_rad - Array of lat/lon in radians [4][n_locs]
 * @param types - Array of location types
 * @param island_bin_path - Path to island.bin file
 * @param out_dist - Output distance matrix [n_locs * n_locs] (caller must free)
 * @return 0 on success, -1 on error
 */
int compute_distance_matrix(int n_locs, double *latlon_rad[4], int *types,
                            const char *island_bin_path, double **out_dist);

#endif

