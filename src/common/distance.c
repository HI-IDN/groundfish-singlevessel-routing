/*
 * Distance Matrix Builder
 * Build distance and feasibility matrices with waypoint routing
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <geos_c.h>
#include "../include/distance.h"
#include "../include/constants.h"
#include "../include/geo_utils.h"
#include <stdbool.h>


static void die(const char* msg)
{
    fprintf(stderr, "%s\n", msg);
    exit(1);
}

static void* xmalloc(size_t n)
{
    void* p = malloc(n);
    if (!p) die("OOM");
    return p;
}

static void* xcalloc(size_t n, size_t s)
{
    void* p = calloc(n, s);
    if (!p) die("OOM");
    return p;
}


/* Build waypoint-aware distance and feasibility matrices */
void build_waypoint_dist(const location_data* ex,
                         const double* land, int n_land,
                         double** out_dist, int** out_fsb)
{
    (void)land;
    (void)n_land;

    int m = ex->Size;
    int M = 2 * ex->Size;

    /* Allocate full matrices M x M */
    int* F = (int*)xcalloc((size_t)M * (size_t)M, sizeof(int));
    double* D = (double*)xcalloc((size_t)M * (size_t)M, sizeof(double));

    /* Prepare column-major LatLon arrays for distance_link */
    double* latlon_cols[2];
    for (int k = 0; k < 2; k++)
    {
        latlon_cols[k] = (double*)xmalloc((size_t)m * sizeof(double));
    }
    for (int i = 0; i < m; i++)
    {
        latlon_cols[0][i] = ex->LatLonRad[i * 4 + 0];
        latlon_cols[1][i] = ex->LatLonRad[i * 4 + 1];
    }

    /* Type array for main route (excludes waypoints) */
    int* type_main = (int*)xmalloc((size_t)ex->Size * sizeof(int));
    for (int i = 0; i < ex->Size; i++)
    {
        type_main[i] = ex->Type[i];
    }

    /* Start/end coordinates */
    double start_end[4] = {
        ex->LatLonRad[0], ex->LatLonRad[1],
        ex->LatLonRad[2], ex->LatLonRad[3]
    };

    /* Call internal distance_link function (Dijkstra routing with land obstacles) */
    if (distance_link(D, F, type_main, latlon_cols, start_end, ex->Size) != 0)
    {
        die("distance_link link failed");
    }

    for (int k = 0; k < 2; k++) free(latlon_cols[k]);
    free(type_main);

    *out_dist = D;
    *out_fsb = F;
}
/* ===== MAP Structure for Island Data ===== */

/* MAP structure for land polygon data is declared in distance.h
 * External MAP structure (defined and initialized in coastline_db.c) */

static int iMAP = 0;


/*
 * Compute distance matrix for locations using waypoint-aware Dijkstra routing
 */
int compute_distance_matrix(int n_locs, double* latlon_rad[2], int* types,
                            double** out_dist, int** out_fsb)
{
    if (n_locs <= 0 || !latlon_rad || !types || !out_dist || !out_fsb)
    {
        fprintf(stderr, "Error: Invalid parameters to compute_distance_matrix\n");
        return -1;
    }

    printf("  → Computing %d×%d distance matrix\n", n_locs, n_locs);

    /* MAP structure should already be initialized by caller from imported coastline data. */
    if (MAP[0].N[0] <= 0 || !MAP[0].LatDeg[0] || !MAP[0].LonDeg[0])
    {
        fprintf(stderr, "  ⚠ Warning: MAP not initialized - land-crossing detection disabled\n");
    }

    /* Allocate distance and feasibility matrices */
    /* Simple n×n matrix - each location is a single point (not start/end pair) */
    int M = n_locs;
    size_t matrix_size = (size_t)M * (size_t)M;

    double* D = (double*)xcalloc(matrix_size, sizeof(double));
    int* F = (int*)xcalloc(matrix_size, sizeof(int));


    /* Start/end points: first and last locations */
    double start_end[4] = {
        latlon_rad[0][0], latlon_rad[1][0],
        latlon_rad[0][n_locs - 1], latlon_rad[1][n_locs - 1]
    };

    /* Call distance_link */
    printf("  → Calling distance_link (Dijkstra routing)...\n");
    int rc = distance_link(D, F, types, latlon_rad, start_end, n_locs);

    if (rc != 0)
    {
        fprintf(stderr, "  ✗ distance_link failed (error %d)\n", rc);
        free(D);
        free(F);
        return -1;
    }

    printf("  ✓ Distance matrix computed successfully\n");

    /* Caller owns outputs */
    *out_dist = D;
    *out_fsb = F;
    return 0;
}

/* ===== distance_link Implementation ===== */

/* Helper macro for 2D array indexing of 1D array - COLUMN-MAJOR like original */
#define GRAPH_2D(graph, M, u, v) ((graph)[(u) + (M)*(v)])

/* PARAMS structure for distance computation */
typedef struct
{
    int* Type;
    double* StartEnd;
    int Size;
    double* DistMtrx;
    int* FsbleLink;
    double* Graph;
    double* LatLonRad[42];
} PARAMS;

/* Calculate great-circle distance in nautical miles. */
static double arc_distance(double lat1, double lon1, double lat2, double lon2)
{
    double angle = sin(lat1) * sin(lat2) + cos(lat1) * cos(lat2) * cos(lon1 - lon2);
    if (angle > 1.0) angle = 1.0;
    if (angle < -1.0) angle = -1.0;
    return 3437.905 * acos(angle);
}

/* GEOS context and coastline geometry (initialized once) */
static GEOSContextHandle_t geos_ctx = NULL;
static GEOSGeometry* coastline_polygon = NULL;
static GEOSGeometry* coastline_boundary = NULL; /* Exterior ring for intersection testing */

/* Forward declaration */
static void cleanup_geos(void);

/* Initialize GEOS and create coastline polygon from MAP data */
static void init_geos_coastline()
{
    if (geos_ctx != NULL) return; /* Already initialized */

    /* Initialize GEOS */
    geos_ctx = GEOS_init_r();
    if (!geos_ctx)
    {
        fprintf(stderr, "Error: Failed to initialize GEOS\n");
        return;
    }

    /* Build coastline polygon from MAP data */
    int n = MAP[iMAP].N[0];
    double* LatDeg = MAP[iMAP].LatDeg[0];
    double* LonDeg = MAP[iMAP].LonDeg[0];

    if (n < 3 || !LatDeg || !LonDeg)
    {
        fprintf(stderr, "Error: Invalid coastline data\n");
        return;
    }

    /* Create coordinate sequence - GEOS requires closed ring (first point = last point)
     * So we need n+1 points total
     * Data is loaded from DB with ORDER BY id, so it's already in correct order */
    GEOSCoordSequence* seq = GEOSCoordSeq_create_r(geos_ctx, n + 1, 2);
    for (int i = 0; i < n; i++)
    {
        GEOSCoordSeq_setX_r(geos_ctx, seq, i, LonDeg[i]); /* X = longitude */
        GEOSCoordSeq_setY_r(geos_ctx, seq, i, LatDeg[i]); /* Y = latitude */
    }
    /* Close the ring - last point = first point */
    GEOSCoordSeq_setX_r(geos_ctx, seq, n, LonDeg[0]);
    GEOSCoordSeq_setY_r(geos_ctx, seq, n, LatDeg[0]);

    /* Create linear ring and polygon */
    GEOSGeometry* ring = GEOSGeom_createLinearRing_r(geos_ctx, seq);
    if (!ring)
    {
        fprintf(stderr, "Error: Failed to create GEOS linear ring\n");
        GEOSCoordSeq_destroy_r(geos_ctx, seq);
        return;
    }

    coastline_polygon = GEOSGeom_createPolygon_r(geos_ctx, ring, NULL, 0);
    if (!coastline_polygon)
    {
        fprintf(stderr, "Error: Failed to create GEOS polygon\n");
        GEOSGeom_destroy_r(geos_ctx, ring);
        return;
    }

    /* Check if polygon is valid, and if not, try to fix it with buffer(0) */
    char is_valid = GEOSisValid_r(geos_ctx, coastline_polygon);
    if (!is_valid)
    {
        char* reason = GEOSisValidReason_r(geos_ctx, coastline_polygon);
        fprintf(stderr, "  ⚠ Warning: Coastline polygon invalid: %s\n", reason);
        fprintf(stderr, "  → Attempting to fix with buffer(0)...\n");
        GEOSFree_r(geos_ctx, reason);

        /* Buffer by 0 to fix invalid geometry */
        GEOSGeometry* fixed = GEOSBuffer_r(geos_ctx, coastline_polygon, 0.0, 8);
        if (fixed)
        {
            GEOSGeom_destroy_r(geos_ctx, coastline_polygon);
            coastline_polygon = fixed;
            is_valid = GEOSisValid_r(geos_ctx, coastline_polygon);
            fprintf(stderr, "  → Fixed polygon is now valid: %d\n", is_valid);
        }
    }

    /* Extract the boundary (exterior ring) for intersection testing
     * This matches the original algorithm which checks if routes cross coastline segments */
    coastline_boundary = GEOSBoundary_r(geos_ctx, coastline_polygon);
    if (!coastline_boundary)
    {
        fprintf(stderr, "Error: Failed to extract polygon boundary\n");
        GEOSGeom_destroy_r(geos_ctx, coastline_polygon);
        return;
    }

    /* Register cleanup function to be called at program exit */
    atexit(cleanup_geos);

    printf("  ✓ GEOS initialized with %d-point coastline polygon\n", n);
}

/* Cleanup GEOS resources */
static void cleanup_geos()
{
    if (coastline_boundary)
    {
        GEOSGeom_destroy_r(geos_ctx, coastline_boundary);
        coastline_boundary = NULL;
    }
    if (coastline_polygon)
    {
        GEOSGeom_destroy_r(geos_ctx, coastline_polygon);
        coastline_polygon = NULL;
    }
    if (geos_ctx)
    {
        GEOS_finish_r(geos_ctx);
        geos_ctx = NULL;
    }
}

/* Check if line segment crosses land using GEOS
 *
 * Uses industry-standard GEOS library for robust geometric operations.
 * Checks if route intersects the coastline boundary (matching original algorithm).
 *
 * Returns: 1 if crosses land (infeasible), 0 if doesn't cross (feasible)
 */
int crosses_land(double lat1_deg, double lon1_deg, double lat2_deg, double lon2_deg,
                        const double* LatDeg, const double* LonDeg, int n)
{
    (void)LatDeg; /* Unused - we use the GEOS boundary instead */
    (void)LonDeg;
    (void)n;

    if (!geos_ctx || !coastline_boundary)
    {
        /* GEOS not initialized - shouldn't happen but handle gracefully */
        return 0;
    }

    /* Create line segment geometry */
    GEOSCoordSequence* line_seq = GEOSCoordSeq_create_r(geos_ctx, 2, 2);
    GEOSCoordSeq_setX_r(geos_ctx, line_seq, 0, lon1_deg);
    GEOSCoordSeq_setY_r(geos_ctx, line_seq, 0, lat1_deg);
    GEOSCoordSeq_setX_r(geos_ctx, line_seq, 1, lon2_deg);
    GEOSCoordSeq_setY_r(geos_ctx, line_seq, 1, lat2_deg);

    GEOSGeometry* line = GEOSGeom_createLineString_r(geos_ctx, line_seq);

    /* Check if line intersects coastline boundary */
    char intersects = GEOSIntersects_r(geos_ctx, line, coastline_boundary);

    /* Cleanup */
    GEOSGeom_destroy_r(geos_ctx, line);


    return (intersects == 1) ? 1 : 0;
}

/* Public helper for one-off feasibility checks from preprocessing/debug paths. */
int check_land_crossing_deg(double lat1_deg, double lon1_deg,
                            double lat2_deg, double lon2_deg)
{
    if (MAP[iMAP].N[0] <= 0 || !MAP[iMAP].LatDeg[0] || !MAP[iMAP].LonDeg[0])
    {
        return 0;
    }

    /* Ensure GEOS boundary geometry exists before crossing test. */
    init_geos_coastline();

    return crosses_land(lat1_deg, lon1_deg, lat2_deg, lon2_deg,
                        MAP[iMAP].LatDeg[0], MAP[iMAP].LonDeg[0], MAP[iMAP].N[0]);
}

/* Dijkstra's algorithm */
static int min_distance(double* dist, int* sptSet, int n)
{
    double min = DIJKSTRA_INFINITY;
    int min_index = 0, v;
    for (v = 0; v < n; v++)
        if (sptSet[v] == 0 && dist[v] <= min)
            min = dist[v], min_index = v;
    return min_index;
}

/* Dijkstra shortest path distance - works with 1D array representation
 * Also tracks the path taken through waypoints.
 *
 * In the refactored N×N graph, only waypoint nodes may be used as intermediates.
 * Non-waypoint nodes are allowed only as the source and destination.
 */
static double dijkstra_distance_with_path(double* graph, int M, const int *types,
                                          int src, int dest,
                                          int** out_path, int* out_path_len)
{
    double *dist, INFTY = DIJKSTRA_INFINITY;
    int *sptSet, *parent, i, u, v;

    dist = (double*)malloc((size_t)M * sizeof(double));
    sptSet = (int*)calloc((size_t)M, sizeof(int));
    parent = (int*)malloc((size_t)M * sizeof(int));

    if (!dist || !sptSet || !parent) {
        free(dist);
        free(sptSet);
        free(parent);
        if (out_path) *out_path = NULL;
        if (out_path_len) *out_path_len = 0;
        return DIJKSTRA_INFINITY;
    }

    for (i = 0; i < M; i++) {
        dist[i] = INFTY;
        parent[i] = -1;
    }
    dist[src] = 0.0;

    for (i = 0; i < M; i++)
    {
        u = min_distance(dist, sptSet, M);
        if (sptSet[u] != 0 || dist[u] >= INFTY / 2.0) break;
        sptSet[u] = 1;

        if (u == dest) break;

        /* Only the source and waypoint nodes may expand onward. */
        if (u != src && types[u] != NODE_TYPE_WAYPOINT) {
            continue;
        }

        for (v = 0; v < M; v++)
        {
            double edge = GRAPH_2D(graph, M, u, v);
            if (sptSet[v] != 0 || edge <= 0.0) continue;

            /* Only waypoint nodes or the final destination may be reached. */
            if (v != dest && types[v] != NODE_TYPE_WAYPOINT) continue;

            if (dist[u] + edge < dist[v])
            {
                dist[v] = dist[u] + edge;
                parent[v] = u;
            }
        }
    }

    double result = dist[dest];

    if (result < INFTY / 2.0 && out_path && out_path_len) {
        int path_len = 0;
        int node = dest;
        while (node != -1) {
            path_len++;
            node = parent[node];
        }

        *out_path = (int*)malloc((size_t)path_len * sizeof(int));
        if (!*out_path) {
            *out_path_len = 0;
            result = DIJKSTRA_INFINITY;
        } else {
            *out_path_len = path_len;
            node = dest;
            for (int k = path_len - 1; k >= 0; k--) {
                (*out_path)[k] = node;
                node = parent[node];
            }
        }
    } else if (out_path && out_path_len) {
        *out_path = NULL;
        *out_path_len = 0;
    }

    free(dist);
    free(sptSet);
    free(parent);
    return result;
}

/* ===== GLOBAL MATRICES - Single 1D representation (N*N) ===== */
/* These matrices provide a unified global view of distances and routing */
static double *global_distance_matrix = NULL;    // size N*N - stores final distances
static bool *global_feasible_matrix = NULL;      // size N*N - True if direct route feasible
static int **global_dijkstra_paths = NULL;       // size N*N - sparse array of waypoint paths
static int *global_dijkstra_path_lengths = NULL; // size N*N - length of each path (0 if none)
static int global_matrix_size = 0;               // Current N for allocated matrices

/* Forward declarations for global matrix management */
static void allocate_global_matrices(int N);
static void free_global_matrices(void);

/* Create feasibility matrix - check for land crossings */
static int create_feasibility_matrix(PARAMS params)
{
    int i, j, k;
    int m = params.Size;
    double x1, y1, x2, y2;
    int* F = params.FsbleLink;
    int n = MAP[iMAP].N[0];
    double* LatDeg = MAP[iMAP].LatDeg[0];
    double* LonDeg = MAP[iMAP].LonDeg[0];

    printf("  → Checking land crossings for %d locations...\n", m);
    fflush(stdout);

    /* Set diagonal to 1 (feasible) - COLUMN-MAJOR indexing */
    for (i = 0; i < m; i++)
    {
        F[i + m * i] = 1;
    }

    /* Precompute degree coordinates for each location */
    double* lat = (double*)xmalloc((size_t)m * sizeof(double));
    double* lon = (double*)xmalloc((size_t)m * sizeof(double));

    // print m and size of params.LatLonRad[0][i]
    for (i = 0; i < m; i++)
    {
        lat[i] = rad_to_deg(params.LatLonRad[0][i]);
        lon[i] = rad_to_deg(params.LatLonRad[1][i]);
    }

    int pairs_checked = 0;
    int land_crossings = 0;

    printf("  → Starting land-crossing checks (this may take several minutes)...\n");
    fflush(stdout);

    int total_pairs = m * (m - 1) / 2;

    /* Upper-triangle only: j starts at i+1 to avoid duplicate work.
     * We set both (i,j) and (j,i) for symmetry on each update. */
    for (i = 0; i < m; i++)
    {
        for (j = i + 1; j < m; j++)
        {
            /* Single pair: location i to location j */
            x1 = lat[i];
            y1 = lon[i];
            x2 = lat[j];
            y2 = lon[j];
            k = crosses_land(x1, y1, x2, y2, LatDeg, LonDeg, n);
            if (k) land_crossings++;
            F[i + m * j] = !k; /* COLUMN-MAJOR */
            F[j + m * i] = !k; /* Symmetric */

            pairs_checked++;

            /* Pair-based progress every 100k checks */
            if (pairs_checked % 100000 == 0 || pairs_checked == total_pairs)
            {
                double pct = (100.0 * pairs_checked) / total_pairs;
                printf("    Land-crossing progress: %d/%d (%.1f%%) - %d crossings found\n",
                       pairs_checked, total_pairs, pct, land_crossings);
                fflush(stdout);
            }
        }
    }

    free(lat);
    free(lon);

    printf("  ✓ Land-crossing check: %d crossings detected (%.1f%% of %d route pairs)\n",
           land_crossings, (100.0 * land_crossings) / pairs_checked, pairs_checked);
    fflush(stdout);

    return 0;
}

/* Create distance matrix */
static void create_distance_matrix(PARAMS params)
{
    int i;
    int waypoint_count = 0;

    printf("  → Computing haversine distances...\n");

    /* Allocate global matrices if not already done */
    if (global_matrix_size != params.Size) {
        allocate_global_matrices(params.Size);
    }

    /* Set diagonal to 0 - COLUMN-MAJOR indexing */
    for (i = 0; i < params.Size; i++) {
        params.DistMtrx[i + params.Size * i] = 0.0;
        if (global_distance_matrix) {
            global_distance_matrix[i + params.Size * i] = 0.0;
        }
        if (global_feasible_matrix) {
            global_feasible_matrix[i + params.Size * i] = true; /* diagonal always feasible */
        }
    }

    int infeasible_links = 0;
    int feasible_links = 0;
    int total_pairs = params.Size * (params.Size - 1) / 2;

    /* Calculate distances between all location pairs (upper triangle only) */
    for (i = 0; i < params.Size; i++)
    {
        for (int j = i + 1; j < params.Size; j++)
        {
            /* Each location has one position (lat, lon) */
            double x1 = params.LatLonRad[0][i];
            double y1 = params.LatLonRad[1][i];
            double x2 = params.LatLonRad[0][j];
            double y2 = params.LatLonRad[1][j];
            double d = arc_distance(x1, y1, x2, y2);

            int idx_ij = i + params.Size * j; /* COLUMN-MAJOR */
            int idx_ji = j + params.Size * i;

            if (params.FsbleLink[idx_ij] == 0)
            {
                /* Crosses land */
                d += INFEASIBLE_LINK_PENALTY;
                infeasible_links++;

                /* Mark as infeasible in global matrix */
                if (global_feasible_matrix) {
                    global_feasible_matrix[idx_ij] = false;
                    global_feasible_matrix[idx_ji] = false;
                }
            }
            else
            {
                feasible_links++;

                /* Mark as feasible in global matrix */
                if (global_feasible_matrix) {
                    global_feasible_matrix[idx_ij] = true;
                    global_feasible_matrix[idx_ji] = true;
                }
            }

            params.DistMtrx[idx_ij] = d; /* COLUMN-MAJOR */
            params.DistMtrx[idx_ji] = d; /* Symmetric matrix */

            /* Store haversine distance in global matrix */
            if (global_distance_matrix) {
                global_distance_matrix[idx_ij] = d;
                global_distance_matrix[idx_ji] = d;
            }
        }
    }

    printf("  → Haversine computed for %d feasible pairs (of %d total pairs)\n",
           feasible_links, total_pairs);

    printf("  → Infeasible links flagged for Dijkstra: %d (%.1f%% of checked pairs)\n",
           infeasible_links,
           (100.0 * infeasible_links) / (params.Size * (params.Size - 1) / 2.0));
    fflush(stdout);

    if (infeasible_links == 0)
    {
        printf("  ✓ No infeasible links; skipping Dijkstra.\n");
        fflush(stdout);
        return;
    }

    /* Build Dijkstra graph from feasible (non-land-crossing) haversine edges only */
    for (i = 0; i < params.Size; i++)
    {
        for (int j = i + 1; j < params.Size; j++)
        {
            if (params.FsbleLink[i + params.Size * j] != 0)
            {
                params.Graph[i + params.Size * j] = params.DistMtrx[i + params.Size
                    * j];
                params.Graph[j + params.Size * i] = params.DistMtrx[j + params.Size
                    * i];
            }
        }
    }

    /* Count waypoints available in this distance matrix */
    for (i = 0; i < params.Size; i++)
    {
        if (params.Type[i] == NODE_TYPE_WAYPOINT)
        {
            waypoint_count++;
        }
    }

    printf("  → Computing Dijkstra waypoint routes (this may take a while)...\n");
    fflush(stdout);

    /* Apply Dijkstra routing for infeasible links (slow - needs progress) */
    int dijkstra_pairs_checked = 0;
    int dijkstra_routes = 0;
    int dijkstra_failed = 0;
    int dijkstra_success = 0;

    /* Allocate global matrices if needed */
    if (global_matrix_size != params.Size) {
        allocate_global_matrices(params.Size);
    }

    for (i = 0; i < params.Size; i++)
    {
        for (int j = i + 1; j < params.Size; j++)
        {
            if (params.FsbleLink[i + params.Size * j] == 0)
            {
                /* Crosses land - COLUMN-MAJOR */

                /* Skip Dijkstra for waypoint pairs - only route non-waypoint to non-waypoint */
                if (params.Type[i] == NODE_TYPE_WAYPOINT || params.Type[j] == NODE_TYPE_WAYPOINT)
                {
                    /* Waypoint involved: keep infeasible penalty, no Dijkstra */
                    dijkstra_pairs_checked++;
                    dijkstra_failed++;
                    continue;
                }

                /* Compute Dijkstra path and distance for non-waypoint pairs only */
                int* path = NULL;
                int path_len = 0;
                double d = dijkstra_distance_with_path(params.Graph, params.Size, params.Type,
                                                      i, j, &path, &path_len);

                if (d >= DIJKSTRA_INFINITY / 2.0)
                {
                    /* No waypoint route found: mark infeasible */
                    d = INFEASIBLE_LINK_PENALTY;
                    dijkstra_failed++;
                }
                else
                {
                    dijkstra_success++;

                    /* Store path in global matrix (both directions) */
                    int idx_ij = i + params.Size * j; /* COLUMN-MAJOR */
                    int idx_ji = j + params.Size * i;

                    if (path && path_len > 0) {
                        /* Forward direction (i->j) */
                        global_dijkstra_paths[idx_ij] = path;
                        global_dijkstra_path_lengths[idx_ij] = path_len;

                        /* Reverse direction (j->i) - allocate and reverse the path */
                        global_dijkstra_paths[idx_ji] = (int*)malloc(path_len * sizeof(int));
                        global_dijkstra_path_lengths[idx_ji] = path_len;
                        for (int k = 0; k < path_len; k++) {
                            global_dijkstra_paths[idx_ji][k] = path[path_len - 1 - k];
                        }
                    }
                }

                /* Update distance matrix */
                params.DistMtrx[i + params.Size * j] = d;
                params.DistMtrx[j + params.Size * i] = d;

                /* Store in global matrices */
                if (global_distance_matrix) {
                    global_distance_matrix[i + params.Size * j] = d;
                    global_distance_matrix[j + params.Size * i] = d;
                }
                if (global_feasible_matrix) {
                    global_feasible_matrix[i + params.Size * j] = false;
                    global_feasible_matrix[j + params.Size * i] = false;
                }

                dijkstra_routes++;
                dijkstra_pairs_checked++;

                if ((dijkstra_pairs_checked % 50000) == 0)
                {
                    int percent = (100 * dijkstra_pairs_checked) / infeasible_links;
                    if (percent > 100) percent = 100;
                    printf("    Dijkstra progress: %d/%d (%.1f%%)\n",
                           dijkstra_pairs_checked, infeasible_links, (double)percent);
                    fflush(stdout);
                }
            }
        }
    }

    printf("  ✓ Distance matrix complete: %d waypoint routes via Dijkstra\n", dijkstra_routes);
    printf("  → Dijkstra routed %d infeasible pairs through %d waypoint nodes\n",
           dijkstra_pairs_checked, waypoint_count);
    printf("  → Dijkstra path check: %d routable\n",
           dijkstra_success);
    if (dijkstra_failed > 0)
    {
        printf("  ⚠ Dijkstra failed to route %d pairs; remain INFEASIBLE\n", dijkstra_failed);
    }
}

/* ===== Global Matrix Management Functions ===== */

/* Allocate global matrices for N locations */
static void allocate_global_matrices(int N) {
    /* Free existing matrices if any */
    if (global_distance_matrix) free(global_distance_matrix);
    if (global_feasible_matrix) free(global_feasible_matrix);
    if (global_dijkstra_path_lengths) free(global_dijkstra_path_lengths);
    if (global_dijkstra_paths) {
        for (int i = 0; i < global_matrix_size * global_matrix_size; ++i) {
            if (global_dijkstra_paths[i]) free(global_dijkstra_paths[i]);
        }
        free(global_dijkstra_paths);
    }

    /* Allocate new matrices */
    size_t matrix_size = (size_t)N * (size_t)N;
    global_distance_matrix = (double*)calloc(matrix_size, sizeof(double));
    global_feasible_matrix = (bool*)calloc(matrix_size, sizeof(bool));
    global_dijkstra_paths = (int**)calloc(matrix_size, sizeof(int*));
    global_dijkstra_path_lengths = (int*)calloc(matrix_size, sizeof(int));
    global_matrix_size = N;

    if (!global_distance_matrix || !global_feasible_matrix ||
        !global_dijkstra_paths || !global_dijkstra_path_lengths) {
        fprintf(stderr, "Error: Failed to allocate global matrices for N=%d\n", N);
        exit(1);
    }
}

/* Free global matrices */
static void free_global_matrices(void) {
    if (global_distance_matrix) {
        free(global_distance_matrix);
        global_distance_matrix = NULL;
    }
    if (global_feasible_matrix) {
        free(global_feasible_matrix);
        global_feasible_matrix = NULL;
    }
    if (global_dijkstra_path_lengths) {
        free(global_dijkstra_path_lengths);
        global_dijkstra_path_lengths = NULL;
    }
    if (global_dijkstra_paths) {
        for (int i = 0; i < global_matrix_size * global_matrix_size; ++i) {
            if (global_dijkstra_paths[i]) free(global_dijkstra_paths[i]);
        }
        free(global_dijkstra_paths);
        global_dijkstra_paths = NULL;
    }
    global_matrix_size = 0;
}

/* Get Dijkstra path for a given pair (i, j)
 * Returns: pointer to path array, or NULL if no Dijkstra path exists
 * out_length: receives the path length (number of nodes in path)
 */
int* get_dijkstra_path(int i, int j, int* out_length) {
    if (!global_dijkstra_paths || i < 0 || j < 0 ||
        i >= global_matrix_size || j >= global_matrix_size) {
        if (out_length) *out_length = 0;
        return NULL;
    }

    int idx = i + global_matrix_size * j; /* COLUMN-MAJOR indexing */
    if (out_length) *out_length = global_dijkstra_path_lengths[idx];
    return global_dijkstra_paths[idx];
}

/* Main distance_link function */
int distance_link(double* DistrMtrx, int* FsbleMtrx, int* Type,
                  double* LatLon[2], double* StartEnd,
                  int Size)
{
    int i;
    int M = Size; /* Matrix dimension = number of locations */

    /* Validate inputs */
    if (!DistrMtrx || !FsbleMtrx || !Type || !LatLon || !StartEnd)
    {
        fprintf(stderr, "Error: NULL pointer passed to distance_link\n");
        return -1;
    }

    if (Size <= 0)
    {
        fprintf(stderr, "Error: Invalid Size=%d\n", Size);
        return -1;
    }

    PARAMS params;
    params.Type = Type;
    params.StartEnd = StartEnd;
    params.Size = Size;
    params.DistMtrx = DistrMtrx;
    params.FsbleLink = FsbleMtrx;
    params.Graph = (double*)calloc((size_t)M * (size_t)M, sizeof(double));

    if (!params.Graph)
    {
        fprintf(stderr, "Error: Memory allocation failed for Graph (size=%zu)\n", (size_t)M * M);
        return -1;
    }

    /* Note: MAP structure must be initialized from coastline data before calling distance_link. */
    if (MAP[0].n == 0 || MAP[0].LatDeg[0] == NULL)
    {
        fprintf(stderr, "Error: MAP not initialized - load coastline data first\n");
        free(params.Graph);
        return -1;
    }

    /* Initialize GEOS and coastline polygon (once) */
    init_geos_coastline();

    for (i = 0; i < 2; i++)
        params.LatLonRad[i] = LatLon[i];

    create_feasibility_matrix(params);
    create_distance_matrix(params);

    free(params.Graph);

    /* Note: cleanup_geos() should be called at program exit, not here */
    return 0;
}

/* Public function to cleanup global matrices */
void cleanup_distance_matrices(void) {
    free_global_matrices();
}

