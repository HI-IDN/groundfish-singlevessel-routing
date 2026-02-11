/*
 * Distance Matrix Builder
 * Build distance and feasibility matrices with waypoint routing
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
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
                            const char *island_bin_path, double **out_dist) {
    if (n_locs <= 0 || !latlon_rad || !types || !out_dist) {
        fprintf(stderr, "Error: Invalid parameters to compute_distance_matrix\n");
        return -1;
    }

    printf("  → Computing %d×%d distance matrix\n", n_locs, n_locs);

    /* Load island.bin for land contours */
    int n_land = 0;
    double *land = NULL;
    if (island_bin_path) {
        land = load_island_bin(island_bin_path, &n_land);
        if (!land) {
            fprintf(stderr, "  ✗ Failed to load island.bin\n");
            return -1;
        }
        printf("  ✓ Loaded island.bin: %d land polygon points\n", n_land);
    }

    /* Allocate distance and feasibility matrices */
    /* distance_link uses M = 2*SelectedSize internally, so we need M*M allocation */
    int M = 2 * n_locs;
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
        if (land) free(land);
        return -1;
    }

    printf("  ✓ Distance matrix computed successfully\n");

    /* Cleanup and return */
    free(F);
    if (land) free(land);

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


/* Check if line segment crosses land polygon */
static int crosses_land(double x1, double y1, double x2, double y2,
                       double *LatDeg __attribute__((unused)),
                       double *LonDeg __attribute__((unused)),
                       int n __attribute__((unused))) {
    /* Simple check: if entirely within Iceland bounds, check for intersection */
    if (x1 > MAP[0].MAXLAT || x2 > MAP[0].MAXLAT) return 1;
    if (x1 < MAP[0].MINLAT || x2 < MAP[0].MINLAT) return 1;
    if (y1 > MAP[0].MAXLON || y2 > MAP[0].MAXLON) return 1;
    if (y1 < MAP[0].MINLON || y2 < MAP[0].MINLON) return 1;
    return 1;  /* Assume no crossing for now - simplified */
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
static double dijkstra_dist_(double *graph, int M, int src, int dest) {
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
    int m = params.SelectedSize, M = 2*params.SelectedSize;
    double x1, y1, x2, y2;
    int *F = params.FsbleLink;
    int n = MAP[iMAP].N[0];
    double *LatDeg = MAP[iMAP].LatDeg[0];
    double *LonDeg = MAP[iMAP].LonDeg[0];

    /* Set diagonal to 1 (feasible) - COLUMN-MAJOR: F[i + M*i] */
    for (i = 0; i < M; i++) {
        F[i + M*i] = 1;
    }

    for (i = 0; i < m; i++) {
        for (j = i + 1; j < m; j++) {

            x1 = rad_to_deg(params.LatLonRad[0][i]);
            y1 = rad_to_deg(params.LatLonRad[1][i]);
            x2 = rad_to_deg(params.LatLonRad[0][j]);
            y2 = rad_to_deg(params.LatLonRad[1][j]);
            k = crosses_land(x1, y1, x2, y2, LatDeg, LonDeg, n);
            F[(2*i)+M*(2*j)] = k; F[(2*j)+M*(2*i)] = k;

            x1 = rad_to_deg(params.LatLonRad[0][i]);
            y1 = rad_to_deg(params.LatLonRad[1][i]);
            x2 = rad_to_deg(params.LatLonRad[2][j]);
            y2 = rad_to_deg(params.LatLonRad[3][j]);
            k = crosses_land(x1, y1, x2, y2, LatDeg, LonDeg, n);
            F[(2*i)+M*(2*j+1)] = k; F[(2*j+1)+M*(2*i)] = k;

            x1 = rad_to_deg(params.LatLonRad[2][i]);
            y1 = rad_to_deg(params.LatLonRad[3][i]);
            x2 = rad_to_deg(params.LatLonRad[0][j]);
            y2 = rad_to_deg(params.LatLonRad[1][j]);
            k = crosses_land(x1, y1, x2, y2, LatDeg, LonDeg, n);
            F[(2*i+1)+M*(2*j)] = k; F[(2*j)+M*(2*i+1)] = k;

            x1 = rad_to_deg(params.LatLonRad[2][i]);
            y1 = rad_to_deg(params.LatLonRad[3][i]);
            x2 = rad_to_deg(params.LatLonRad[2][j]);
            y2 = rad_to_deg(params.LatLonRad[3][j]);
            k = crosses_land(x1, y1, x2, y2, LatDeg, LonDeg, n);
            F[(2*i+1)+M*(2*j+1)] = k; F[(2*j+1)+M*(2*i+1)] = k;
        }
    }
    return 0;
}

/* Create distance matrix */
static void create_distance_matrix(PARAMS params) {
    int i, j;
    int m = params.SelectedSize, M = 2*params.SelectedSize;
    double x1, y1, x2, y2, d;
    int *F = params.FsbleLink;
    double* D = params.DistMtrx;
    double* G = params.Graph;

    /* Set diagonal to 0 - COLUMN-MAJOR: D[i + M*i] */
    for (i = 0; i < M; i++)
        D[i+M*i] = 0.0;

    /* Calculate distances between all location pairs */
    for (i = 0; i < m; i++) {
        for (j = i + 1; j < m; j++) {
            x1 = params.LatLonRad[0][i];
            y1 = params.LatLonRad[1][i];
            x2 = params.LatLonRad[0][j];
            y2 = params.LatLonRad[1][j];
            d = arc_distance(x1, y1, x2, y2);
            if (F[(2*i)+(2*j)*M] == 0)
                d += INFEASIBLE_LINK_PENALTY;
            D[(2*i)+(2*j)*M] = d;
            D[(2*j)+(2*i)*M] = d;

            x1 = params.LatLonRad[0][i];
            y1 = params.LatLonRad[1][i];
            x2 = params.LatLonRad[2][j];
            y2 = params.LatLonRad[3][j];
            d = arc_distance(x1, y1, x2, y2);
            if (F[(2*i)+(2*j+1)*M] == 0)
                d += INFEASIBLE_LINK_PENALTY;
            D[(2*i)+(2*j+1)*M] = d;
            D[(2*j+1)+(2*i)*M] = d;

            x1 = params.LatLonRad[2][i];
            y1 = params.LatLonRad[3][i];
            x2 = params.LatLonRad[0][j];
            y2 = params.LatLonRad[1][j];
            d = arc_distance(x1, y1, x2, y2);
            if (F[(2*i+1)+(2*j)*M] == 0)
                d += INFEASIBLE_LINK_PENALTY;
            D[(2*i+1)+(2*j)*M] = d;
            D[(2*j)+(2*i+1)*M] = d;

            x1 = params.LatLonRad[2][i];
            y1 = params.LatLonRad[3][i];
            x2 = params.LatLonRad[2][j];
            y2 = params.LatLonRad[3][j];
            d = arc_distance(x1, y1, x2, y2);
            if (F[(2*i+1)+(2*j+1)*M] == 0)
                d += INFEASIBLE_LINK_PENALTY;
            D[(2*i+1)+(2*j+1)*M] = d;
            D[(2*j+1)+(2*i+1)*M] = d;
        }
    }

    memcpy(G, D, M*M*sizeof(double));

    /* Apply Dijkstra routing for infeasible links */
    for (i = 0; i < m; i++) {
        for (j = i + 1; j < m; j++) {
            if (F[(2*i)+(2*j)*M] == 0) {
                d = dijkstra_dist_(G, M, 2*i, 2*j);
                D[(2*i)+(2*j)*M] = d;
                D[(2*j)+(2*i)*M] = d;
            }
            if (F[(2*i)+(2*j+1)*M] == 0) {
                d = dijkstra_dist_(G, M, 2*i, 2*j+1);
                D[(2*i)+(2*j+1)*M] = d;
                D[(2*j+1)+(2*i)*M] = d;
            }
            if (F[(2*i+1)+(2*j)*M] == 0) {
                d = dijkstra_dist_(G, M, 2*i+1, 2*j);
                D[(2*i+1)+(2*j)*M] = d;
                D[(2*j)+(2*i+1)*M] = d;
            }
            if (F[(2*i+1)+(2*j+1)*M] == 0) {
                d = dijkstra_dist_(G, M, 2*i+1, 2*j+1);
                D[(2*i+1)+(2*j+1)*M] = d;
                D[(2*j+1)+(2*i+1)*M] = d;
            }
        }
    }
}

/* Main distance_link function */
int distance_link(double *DistrMtrx, int *FsbleMtrx, int *Type,
                        double *LatLon[4], double *StartEnd,
                        int Size, int SelectedSize) {
    int i;
    int M = 2 * SelectedSize;  /* Full matrix dimension */

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

    for (i=0; i<4; i++)
        params.LatLonRad[i] = LatLon[i];

    create_feasibility_matrix(params);
    create_distance_matrix(params);

    free(params.Graph);
    return 0;
}

