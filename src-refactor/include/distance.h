#ifndef GSP_DISTANCE_H
#define GSP_DISTANCE_H

#include "exdata.h"

/* External DistanceLink function from libutils (built from src/utils.c) */
extern int DistanceLink(double *DistrMtrx, int *FsbleMtrx, int *Type,
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
void build_waypoint_dist(const ExData *ex,
                        const double *Land, int nLand,
                        double **out_dist, int **out_fsb,
                        double **out_full_dist, int **out_full_fsb,
                        int *out_full_m);

/**
 * Load island.bin file (land polygon data)
 */
double *load_island_bin(const char *fname, int *out_n);

#endif

