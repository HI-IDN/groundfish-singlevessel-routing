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

/* Forward declaration - implementation in coastline_db.c */
extern double *load_island_bin(const char *fname, int *out_n);

static void die(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    exit(1);
}

static void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) die("OOM");
    return p;
}

static void *xcalloc(size_t n, size_t s) {
    void *p = calloc(n, s);
    if (!p) die("OOM");
    return p;
}


/* Build waypoint-aware distance and feasibility matrices */
void build_waypoint_dist(const location_data *ex,
                        const double *land, int n_land,
                        double **out_dist, int **out_fsb,
                        double **out_full_dist, int **out_full_fsb,
                        int *out_full_m) {
    (void)land;
    (void)n_land;

    int m = ex->SelectedSize;
    int M = 2 * ex->SelectedSize;
    int n = 2 * ex->Size;

    /* Allocate full matrices M x M */
    int *F = (int*)xcalloc((size_t)M * (size_t)M, sizeof(int));
    double *D = (double*)xcalloc((size_t)M * (size_t)M, sizeof(double));

    /* Prepare column-major LatLon arrays for distance_link */
    double *latlon_cols[4];
    for (int k = 0; k < 4; k++) {
        latlon_cols[k] = (double*)xmalloc((size_t)m * sizeof(double));
    }
    for (int i = 0; i < m; i++) {
        latlon_cols[0][i] = ex->LatLonRad[i*4 + 0];
        latlon_cols[1][i] = ex->LatLonRad[i*4 + 1];
        latlon_cols[2][i] = ex->LatLonRad[i*4 + 2];
        latlon_cols[3][i] = ex->LatLonRad[i*4 + 3];
    }

    /* Type array for main route (excludes waypoints) */
    int *type_main = (int*)xmalloc((size_t)ex->Size * sizeof(int));
    for (int i = 0; i < ex->Size; i++) {
        type_main[i] = ex->Type[i];
    }

    /* Start/end coordinates */
    double start_end[4] = {
        ex->LatLonRad[0], ex->LatLonRad[1],
        ex->LatLonRad[2], ex->LatLonRad[3]
    };

    /* Call internal distance_link function (Dijkstra routing with land obstacles) */
    if (distance_link(D, F, type_main, latlon_cols, start_end, ex->Size, ex->SelectedSize) != 0) {
        die("distance_link link failed");
    }

    for (int k = 0; k < 4; k++) free(latlon_cols[k]);
    free(type_main);

    /* Extract n x n submatrix for main route */
    double *dist = (double*)xmalloc((size_t)n * (size_t)n * sizeof(double));
    int *fsb = (int*)xmalloc((size_t)n * (size_t)n * sizeof(int));
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            dist[i*n + j] = D[i*M + j];
            fsb[i*n + j] = F[i*M + j];
        }
    }

    /* Output full matrices if requested */
    if (out_full_dist) *out_full_dist = D;
    else free(D);
    if (out_full_fsb) *out_full_fsb = F;
    else free(F);
    if (out_full_m) *out_full_m = M;

    *out_dist = dist;
    *out_fsb = fsb;
}

/* ===== MAP Structure for Island Data ===== */

/* MAP structure for land polygon data */
/* External MAP structure (defined and initialized in coastline_db.c) */
extern struct {
    int n;
    int N[10];
    double *LatDeg[10];
    double *LonDeg[10];
    double MINLAT, MAXLAT, MINLON, MAXLON;
} MAP[1];

static int iMAP = 0;


/*
 * Compute distance matrix for locations using waypoint-aware Dijkstra routing
 * ...existing code...
 */
int compute_distance_matrix(int n_locs, double *latlon_rad[4], int *types,
                            double **out_dist) {
    if (n_locs <= 0 || !latlon_rad || !types || !out_dist) {
        fprintf(stderr, "Error: Invalid parameters to compute_distance_matrix\n");
        return -1;
    }

    printf("  → Computing %d×%d distance matrix\n", n_locs, n_locs);

    /* MAP structure should already be initialized by caller (via load_coastline_from_db or load_island_bin) */
    if (MAP[0].N[0] <= 0 || !MAP[0].LatDeg[0] || !MAP[0].LonDeg[0]) {
        fprintf(stderr, "  ⚠ Warning: MAP not initialized - land-crossing detection disabled\n");
    }

    /* Allocate distance and feasibility matrices */
    /* Simple n×n matrix - each location is a single point (not start/end pair) */
    int M = n_locs;
    size_t matrix_size = (size_t)M * (size_t)M;

    double *D = (double*)xcalloc(matrix_size, sizeof(double));
    int *F = (int*)xcalloc(matrix_size, sizeof(int));


    /* Start/end points: first and last locations */
    double start_end[4] = {
        latlon_rad[0][0], latlon_rad[1][0],
        latlon_rad[0][n_locs-1], latlon_rad[1][n_locs-1]
    };

    /* Call distance_link */
    printf("  → Calling distance_link (Dijkstra routing)...\n");
    int rc = distance_link(D, F, types, latlon_rad, start_end, n_locs, n_locs);

    if (rc != 0) {
        fprintf(stderr, "  ✗ distance_link failed (error %d)\n", rc);
        free(D);
        free(F);
        return -1;
    }

    printf("  ✓ Distance matrix computed successfully\n");

    /* Cleanup and return */
    free(F);
    /* Note: MAP structure is global and managed by caller, don't free here */

    *out_dist = D;  /* Caller must free */
    return 0;
}

/* ===== distance_link Implementation ===== */

/* Helper macro for 2D array indexing of 1D array - COLUMN-MAJOR like original */
#define GRAPH_2D(graph, M, u, v) ((graph)[(u) + (M)*(v)])

/* PARAMS structure for distance computation */
typedef struct {
    int SelectedSize;
    int *Type;
    double *StartEnd;
    int Size;
    double *DistMtrx;
    int *FsbleLink;
    double *Graph;
    double *LatLonRad[4];
} PARAMS;

/* Calculate great-circle distance */
static double arc_distance(double lat1, double lon1, double lat2, double lon2) {
    double dLat = lat2 - lat1;
    double dLon = lon2 - lon1;
    double a = sin(dLat/2)*sin(dLat/2) + cos(lat1)*cos(lat2)*sin(dLon/2)*sin(dLon/2);
    double c = 2*atan2(sqrt(a), sqrt(1-a));
    return 6371.0 * c;  /* Earth radius in km */
}

/* GEOS context and coastline geometry (initialized once) */
static GEOSContextHandle_t geos_ctx = NULL;
static GEOSGeometry *coastline_polygon = NULL;
static GEOSGeometry *coastline_boundary = NULL;  /* Exterior ring for intersection testing */

/* Forward declaration */
static void cleanup_geos(void);

/* Initialize GEOS and create coastline polygon from MAP data */
static void init_geos_coastline() {
    if (geos_ctx != NULL) return;  /* Already initialized */

    /* Initialize GEOS */
    geos_ctx = GEOS_init_r();
    if (!geos_ctx) {
        fprintf(stderr, "Error: Failed to initialize GEOS\n");
        return;
    }

    /* Build coastline polygon from MAP data */
    int n = MAP[iMAP].N[0];
    double *LatDeg = MAP[iMAP].LatDeg[0];
    double *LonDeg = MAP[iMAP].LonDeg[0];

    if (n < 3 || !LatDeg || !LonDeg) {
        fprintf(stderr, "Error: Invalid coastline data\n");
        return;
    }

    /* Create coordinate sequence - GEOS requires closed ring (first point = last point)
     * So we need n+1 points total
     * Data is loaded from DB with ORDER BY id, so it's already in correct order */
    GEOSCoordSequence *seq = GEOSCoordSeq_create_r(geos_ctx, n + 1, 2);
    for (int i = 0; i < n; i++) {
        GEOSCoordSeq_setX_r(geos_ctx, seq, i, LonDeg[i]);  /* X = longitude */
        GEOSCoordSeq_setY_r(geos_ctx, seq, i, LatDeg[i]);  /* Y = latitude */
    }
    /* Close the ring - last point = first point */
    GEOSCoordSeq_setX_r(geos_ctx, seq, n, LonDeg[0]);
    GEOSCoordSeq_setY_r(geos_ctx, seq, n, LatDeg[0]);

    /* Create linear ring and polygon */
    GEOSGeometry *ring = GEOSGeom_createLinearRing_r(geos_ctx, seq);
    if (!ring) {
        fprintf(stderr, "Error: Failed to create GEOS linear ring\n");
        GEOSCoordSeq_destroy_r(geos_ctx, seq);
        return;
    }

    coastline_polygon = GEOSGeom_createPolygon_r(geos_ctx, ring, NULL, 0);
    if (!coastline_polygon) {
        fprintf(stderr, "Error: Failed to create GEOS polygon\n");
        GEOSGeom_destroy_r(geos_ctx, ring);
        return;
    }

    /* Check if polygon is valid, and if not, try to fix it with buffer(0) */
    char is_valid = GEOSisValid_r(geos_ctx, coastline_polygon);
    if (!is_valid) {
        char *reason = GEOSisValidReason_r(geos_ctx, coastline_polygon);
        fprintf(stderr, "  ⚠ Warning: Coastline polygon invalid: %s\n", reason);
        fprintf(stderr, "  → Attempting to fix with buffer(0)...\n");
        GEOSFree_r(geos_ctx, reason);

        /* Buffer by 0 to fix invalid geometry */
        GEOSGeometry *fixed = GEOSBuffer_r(geos_ctx, coastline_polygon, 0.0, 8);
        if (fixed) {
            GEOSGeom_destroy_r(geos_ctx, coastline_polygon);
            coastline_polygon = fixed;
            is_valid = GEOSisValid_r(geos_ctx, coastline_polygon);
            fprintf(stderr, "  → Fixed polygon is now valid: %d\n", is_valid);
        }
    }

    /* Extract the boundary (exterior ring) for intersection testing
     * This matches the original algorithm which checks if routes cross coastline segments */
    coastline_boundary = GEOSBoundary_r(geos_ctx, coastline_polygon);
    if (!coastline_boundary) {
        fprintf(stderr, "Error: Failed to extract polygon boundary\n");
        GEOSGeom_destroy_r(geos_ctx, coastline_polygon);
        return;
    }

    /* Register cleanup function to be called at program exit */
    atexit(cleanup_geos);

    printf("  ✓ GEOS initialized with %d-point coastline polygon\n", n);
}

/* Cleanup GEOS resources */
static void cleanup_geos() {
    if (coastline_boundary) {
        GEOSGeom_destroy_r(geos_ctx, coastline_boundary);
        coastline_boundary = NULL;
    }
    if (coastline_polygon) {
        GEOSGeom_destroy_r(geos_ctx, coastline_polygon);
        coastline_polygon = NULL;
    }
    if (geos_ctx) {
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
static int crosses_land(double lat1_deg, double lon1_deg, double lat2_deg, double lon2_deg,
                        const double *LatDeg, const double *LonDeg, int n) {
    (void)LatDeg;  /* Unused - we use the GEOS boundary instead */
    (void)LonDeg;
    (void)n;

    if (!geos_ctx || !coastline_boundary) {
        /* GEOS not initialized - shouldn't happen but handle gracefully */
        return 0;
    }

    /* Create line segment geometry */
    GEOSCoordSequence *line_seq = GEOSCoordSeq_create_r(geos_ctx, 2, 2);
    GEOSCoordSeq_setX_r(geos_ctx, line_seq, 0, lon1_deg);
    GEOSCoordSeq_setY_r(geos_ctx, line_seq, 0, lat1_deg);
    GEOSCoordSeq_setX_r(geos_ctx, line_seq, 1, lon2_deg);
    GEOSCoordSeq_setY_r(geos_ctx, line_seq, 1, lat2_deg);

    GEOSGeometry *line = GEOSGeom_createLineString_r(geos_ctx, line_seq);

    /* Check if line intersects coastline boundary */
    char intersects = GEOSIntersects_r(geos_ctx, line, coastline_boundary);

    /* Cleanup */
    GEOSGeom_destroy_r(geos_ctx, line);


    return (intersects == 1) ? 1 : 0;
}

/* Dijkstra's algorithm */
static int min_distance(double *dist, int *sptSet, int n) {
    double min = DIJKSTRA_INFINITY;
    int min_index = 0, v;
    for (v = 0; v < n; v++)
        if (sptSet[v]==0 && dist[v] <= min)
            min = dist[v], min_index = v;
    return min_index;
}

/* Dijkstra shortest path distance - works with 1D array representation */
static double dijkstra_distance(double *graph, int M, int src, int dest) {
    double *dist, INFTY = DIJKSTRA_INFINITY;
    int *sptSet, i, count, u, v;

    dist = (double *) malloc(M*sizeof(double));
    sptSet = (int *) calloc(M, sizeof(int));

    for (i = 0; i < M; i++)
        dist[i] = INFTY;
    dist[src] = 0.0;

    for (count = 0; count < M - 1; count++) {
        u = min_distance(dist, sptSet, M);
        sptSet[u] = 1;
        for (v = 0; v < M; v++) {
            /* Use macro for readable 2D-style indexing: graph[u][v] */
            if ((sptSet[v]==0) && (GRAPH_2D(graph, M, u, v) > 0) && (dist[u] < INFTY) &&
                (dist[u] + GRAPH_2D(graph, M, u, v) < dist[v])) {
                dist[v] = dist[u] + GRAPH_2D(graph, M, u, v);
            }
        }
    }

    double result = dist[dest];
    free(dist);
    free(sptSet);
    return result;
}

/* Create feasibility matrix - check for land crossings */
static int create_feasibility_matrix(PARAMS params) {
    int i, j, k;
    int m = params.SelectedSize, M = params.SelectedSize;  /* M = m now (not 2*m) */
    double x1, y1, x2, y2;
    int *F = params.FsbleLink;
    int n = MAP[iMAP].N[0];
    double *LatDeg = MAP[iMAP].LatDeg[0];
    double *LonDeg = MAP[iMAP].LonDeg[0];

    printf("  → Checking land crossings for %d locations...\n", m);
    fflush(stdout);

    /* Set diagonal to 1 (feasible) - COLUMN-MAJOR indexing */
    for (i = 0; i < M; i++) {
        F[i + M*i] = 1;
    }

    /* Precompute degree coordinates for each location */
    double *lat = (double*)xmalloc((size_t)m * sizeof(double));
    double *lon = (double*)xmalloc((size_t)m * sizeof(double));

    for (i = 0; i < m; i++) {
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
    for (i = 0; i < m; i++) {
        for (j = i + 1; j < m; j++) {
            /* Single pair: location i to location j */
            x1 = lat[i]; y1 = lon[i];
            x2 = lat[j]; y2 = lon[j];
            k = crosses_land(x1, y1, x2, y2, LatDeg, LonDeg, n);
            if (k) land_crossings++;
            F[i + M*j] = !k;  /* COLUMN-MAJOR */
            F[j + M*i] = !k;  /* Symmetric */

            pairs_checked++;

            /* Pair-based progress every 50k checks */
            if (pairs_checked % 50000 == 0 || pairs_checked == total_pairs) {
                double pct = (100.0 * pairs_checked) / total_pairs;
                printf("    Progress: %d/%d pairs (%.1f%%) - %d crossings found\n",
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
static void create_distance_matrix(PARAMS params) {
    int i;
    int waypoint_count = 0;

    printf("  → Computing haversine distances...\n");

    /* Set diagonal to 0 - COLUMN-MAJOR indexing */
    for (i = 0; i < params.SelectedSize; i++)
        params.DistMtrx[i + params.SelectedSize*i] = 0.0;

    int infeasible_links = 0;
    int feasible_links = 0;
    int total_pairs = params.SelectedSize * (params.SelectedSize - 1) / 2;

    /* Calculate distances between all location pairs (upper triangle only) */
    for (i = 0; i < params.SelectedSize; i++) {
        for (int j = i + 1; j < params.SelectedSize; j++) {
            /* Each location has one position (lat, lon) */
            double x1 = params.LatLonRad[0][i];
            double y1 = params.LatLonRad[1][i];
            double x2 = params.LatLonRad[0][j];
            double y2 = params.LatLonRad[1][j];
            double d = arc_distance(x1, y1, x2, y2);

            if (params.FsbleLink[i + params.SelectedSize*j] == 0) {  /* Crosses land - COLUMN-MAJOR */
                d += INFEASIBLE_LINK_PENALTY;
                infeasible_links++;
            } else {
                feasible_links++;
            }

            params.DistMtrx[i + params.SelectedSize*j] = d;        /* COLUMN-MAJOR */
            params.DistMtrx[j + params.SelectedSize*i] = d;        /* Symmetric matrix */
        }
    }

    printf("  → Haversine computed for %d feasible pairs (of %d total pairs)\n",
           feasible_links, total_pairs);

    printf("  → Infeasible links flagged for Dijkstra: %d (%.1f%% of checked pairs)\n",
           infeasible_links, (100.0 * infeasible_links) / (params.SelectedSize * (params.SelectedSize-1) / 2.0));
    fflush(stdout);

    if (infeasible_links == 0) {
        printf("  ✓ No infeasible links; skipping Dijkstra.\n");
        fflush(stdout);
        return;
    }

    /* Build Dijkstra graph from feasible (non-land-crossing) haversine edges only */
    for (i = 0; i < params.SelectedSize; i++) {
        for (int j = i + 1; j < params.SelectedSize; j++) {
            if (params.FsbleLink[i + params.SelectedSize*j] != 0) {
                params.Graph[i + params.SelectedSize*j] = params.DistMtrx[i + params.SelectedSize*j];
                params.Graph[j + params.SelectedSize*i] = params.DistMtrx[j + params.SelectedSize*i];
            }
        }
    }

    /* Count waypoints available in this distance matrix */
    for (i = 0; i < params.Size; i++) {
        if (params.Type[i] == NODE_TYPE_WAYPOINT) {
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

    for (i = 0; i < params.SelectedSize; i++) {
        /* Progress bar every 50 locations */
        if (i > 0 && i % 50 == 0) {
            int percent = (100 * dijkstra_pairs_checked) / infeasible_links;
            if (percent > 100) percent = 100;
            printf("    [");
            for (int p = 0; p < 50; p++) {
                if (p < percent / 2) printf("=");
                else printf(" ");
            }
            printf("] %d%% (%d/%d pairs, %d routes via Dijkstra)\n",
                   percent, dijkstra_pairs_checked, infeasible_links, dijkstra_routes);
            fflush(stdout);
        }

        for (int j = i + 1; j < params.SelectedSize; j++) {
            if (params.FsbleLink[i + params.SelectedSize*j] == 0) {  /* Crosses land - COLUMN-MAJOR */
                double d = dijkstra_distance(params.Graph, params.SelectedSize, i, j);
                if (d >= DIJKSTRA_INFINITY / 2.0) {
                    /* No waypoint route found: mark infeasible */
                    d = INFEASIBLE_LINK_PENALTY;
                    dijkstra_failed++;
                } else {
                    dijkstra_success++;
                }
                params.DistMtrx[i + params.SelectedSize*j] = d;      /* COLUMN-MAJOR */
                params.DistMtrx[j + params.SelectedSize*i] = d;
                dijkstra_routes++;
            }
        }
    }

    printf("  ✓ Distance matrix complete: %d waypoint routes via Dijkstra\n", dijkstra_routes);
    printf("  → Dijkstra routed %d infeasible pairs through %d waypoint nodes\n",
           dijkstra_pairs_checked, waypoint_count);
    printf("  → Dijkstra path check: %d routable\n",
           dijkstra_success);
    if (dijkstra_failed > 0) {
        printf("  ⚠ Dijkstra failed to route %d pairs; remain INFEASIBLE\n", dijkstra_failed);
    }
}

/* Main distance_link function */
int distance_link(double *DistrMtrx, int *FsbleMtrx, int *Type,
                        double *LatLon[4], double *StartEnd,
                        int Size, int SelectedSize) {
    int i;
    int M = SelectedSize;  /* Matrix dimension = number of locations */

    /* Validate inputs */
    if (!DistrMtrx || !FsbleMtrx || !Type || !LatLon || !StartEnd) {
        fprintf(stderr, "Error: NULL pointer passed to distance_link\n");
        return -1;
    }

    if (Size <= 0 || SelectedSize <= 0) {
        fprintf(stderr, "Error: Invalid Size=%d or SelectedSize=%d\n", Size, SelectedSize);
        return -1;
    }

    PARAMS params;
    params.SelectedSize = SelectedSize;
    params.Type = Type;
    params.StartEnd = StartEnd;
    params.Size = Size;
    params.DistMtrx = DistrMtrx;
    params.FsbleLink = FsbleMtrx;
    params.Graph = (double*)calloc((size_t)M * (size_t)M, sizeof(double));

    if (!params.Graph) {
        fprintf(stderr, "Error: Memory allocation failed for Graph (size=%zu)\n", (size_t)M*M);
        return -1;
    }

    /* Note: MAP structure must be initialized by load_island_bin() before calling distance_link */
    if (MAP[0].n == 0 || MAP[0].LatDeg[0] == NULL) {
        fprintf(stderr, "Error: MAP not initialized - call load_island_bin() first\n");
        free(params.Graph);
        return -1;
    }

    /* Initialize GEOS and coastline polygon (once) */
    init_geos_coastline();

    for (i=0; i<4; i++)
        params.LatLonRad[i] = LatLon[i];

    create_feasibility_matrix(params);
    create_distance_matrix(params);

    free(params.Graph);

    /* Note: cleanup_geos() should be called at program exit, not here */
    return 0;
}

