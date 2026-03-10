#ifndef GSP_DISTANCE_H
#define GSP_DISTANCE_H

/* MAP structure for land polygon data - defined in distance.c, initialized in coastline_db.c */
extern struct
{
    int n;
    int N[10];
    double* LatDeg[10];
    double* LonDeg[10];
    double MINLAT, MAXLAT, MINLON, MAXLON;
} MAP[1];

/* location_data structure - holds location and waypoint information for routing */
typedef struct {
    int *Type;
    int *ItemIndex;
    double *Amount;
    double *LatLonRad;
    double *LatLonDegMin;
    int Size;
} location_data;

/**
 * Compute distance and feasibility matrices using Dijkstra waypoint routing
 * Accounts for land obstacles from island.bin
 */
int distance_link(double *DistrMtrx, int *FsbleMtrx, int *Type,
                 double *LatLon[2], double *StartEnd,
                 int Size);

/**
 * Check if a line segment crosses land
 *
 * @param lat1_deg       Start latitude (decimal degrees)
 * @param lon1_deg       Start longitude (decimal degrees)
 * @param lat2_deg       End latitude (decimal degrees)
 * @param lon2_deg       End longitude (decimal degrees)
 * @param LatDeg         Coastline latitude array (unused - uses GEOS geometry)
 * @param LonDeg         Coastline longitude array (unused - uses GEOS geometry)
 * @param n              Number of coastline points (unused - uses GEOS geometry)
 * @return               1 if line crosses land, 0 if not
 */
int crosses_land(double lat1_deg, double lon1_deg, double lat2_deg, double lon2_deg,
                 const double* LatDeg, const double* LonDeg, int n);

/**
 * Safe direct land-crossing check using decimal-degree endpoints.
 * Ensures coastline/GEOS state is initialized before intersection testing.
 * Returns 1 if segment crosses land, 0 otherwise.
 */
int check_land_crossing_deg(double lat1_deg, double lon1_deg,
                            double lat2_deg, double lon2_deg);

/**
 * Build distance and feasibility matrices using waypoint routing.
 *
 * @param ex             Location data structure
 * @param land           Land polygon data (optional, can be NULL)
 * @param n_land         Number of land points
 * @param out_dist       Output distance matrix [Size][Size]
 * @param out_fsb        Output feasibility matrix [Size][Size]
 */
void build_waypoint_dist(const location_data *ex,
                        const double *land, int n_land,
                        double **out_dist, int **out_fsb);

/**
 * Compute distance matrix for locations using waypoint-aware Dijkstra routing
 * Note: MAP structure must be initialized first (via load_coastline_from_db or load_island_bin)
 *
 * @param n_locs          Number of locations (including waypoints)
 * @param latlon_rad      Array of lat/lon in radians [2][n_locs] (lat, lon)
 * @param types           Array of location types
 * @param out_dist        Output distance matrix [n_locs * n_locs] (caller must free)
 * @param out_fsb         Output feasibility matrix [n_locs * n_locs] (caller must free)
 */
int compute_distance_matrix(int n_locs, double *latlon_rad[2], int *types,
                            double **out_dist, int **out_fsb);

/**
 * Get Dijkstra waypoint path for a specific location pair
 *
 * @param i              Source location index
 * @param j              Destination location index
 * @param out_length     Output: number of nodes in the path (including src and dest)
 * @return               Pointer to path array (array of node indices), or NULL if no Dijkstra path exists
 *                       The returned pointer is owned by the distance module - do not free it
 */
int* get_dijkstra_path(int i, int j, int* out_length);

/**
 * Free global distance matrices and cleanup
 * Should be called when distance computation is complete
 */
void cleanup_distance_matrices(void);

#endif
