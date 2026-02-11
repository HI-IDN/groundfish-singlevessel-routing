/*
 * Distance Matrix Builder
 * Build distance and feasibility matrices with waypoint routing
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/distance.h"

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
void build_waypoint_dist(const ExData *ex,
                        const double *Land, int nLand,
                        double **out_dist, int **out_fsb,
                        double **out_full_dist, int **out_full_fsb,
                        int *out_full_m) {
    (void)Land;
    (void)nLand;

    int m = ex->SelectedSize;
    int M = 2 * ex->SelectedSize;
    int n = 2 * ex->Size;

    /* Allocate full matrices M x M */
    int *F = (int*)xcalloc((size_t)M * (size_t)M, sizeof(int));
    double *D = (double*)xcalloc((size_t)M * (size_t)M, sizeof(double));

    /* Prepare column-major LatLon arrays for DistanceLink */
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

    /* Call external DistanceLink function */
    if (DistanceLink(D, F, type_main, latlon_cols, start_end, ex->Size, ex->SelectedSize) != 0) {
        die("DistanceLink failed (could not read map?)");
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

/* Load island.bin file (land polygon data) */
double *load_island_bin(const char *fname, int *out_n) {
    FILE *fp = fopen(fname, "rb");
    if (!fp) {
        perror("fopen island.bin");
        exit(1);
    }

    fseek(fp, 0, SEEK_END);
    long bytes = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (bytes <= 0 || (bytes % 4) != 0) die("island.bin size invalid");
    long nfloat = bytes / 4;
    if ((nfloat % 2) != 0) die("island.bin float count must be even");
    int n = (int)(nfloat / 2);

    float *buf = (float*)xmalloc((size_t)nfloat * sizeof(float));
    if (fread(buf, sizeof(float), (size_t)nfloat, fp) != (size_t)nfloat) {
        die("Failed to read island.bin");
    }
    fclose(fp);

    double *Land = (double*)xmalloc((size_t)nfloat * sizeof(double));
    for (long i = 0; i < nfloat; i++) {
        Land[i] = (double)buf[i];
    }
    free(buf);

    *out_n = n;
    return Land;
}
