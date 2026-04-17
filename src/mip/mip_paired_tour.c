#include "include/mip_paired_tour.h"

#include <stdlib.h>
#include <string.h>

void mip_findsubtour_directed(int n, const double *sol, int *tourlen_out, int *tour_out) {
    int *unvisited = (int*)mip_xcalloc((size_t)n, sizeof(int));
    int *best = (int*)mip_xmalloc((size_t)n * sizeof(int));
    int *current = (int*)mip_xmalloc((size_t)n * sizeof(int));
    int best_len = n + 1;
    int remaining = n;

    for (int i = 0; i < n; i++) unvisited[i] = 1;

    while (remaining > 0) {
        int start = -1;
        int len = 0;
        int cursor;

        for (int i = 0; i < n; i++) {
            if (unvisited[i]) {
                start = i;
                break;
            }
        }
        if (start < 0) break;

        cursor = start;
        while (cursor >= 0 && unvisited[cursor]) {
            current[len++] = cursor;
            unvisited[cursor] = 0;
            remaining--;

            {
                int next = -1;
                for (int j = 0; j < n; j++) {
                    if (sol[cursor * n + j] > 0.5 && unvisited[j]) {
                        next = j;
                        break;
                    }
                }
                cursor = next;
            }
        }

        if (len < best_len) {
            best_len = len;
            memcpy(best, current, (size_t)len * sizeof(int));
        }
    }

    memcpy(tour_out, best, (size_t)best_len * sizeof(int));
    *tourlen_out = best_len;

    free(unvisited);
    free(best);
    free(current);
}

int *mip_node_tour_to_letour(const int *tour, int len, int size, int *out_len) {
    int *letour = (int*)mip_xmalloc((size_t)size * sizeof(int));
    int count = 0;

    for (int i = 0; i < len; i++) {
        int city = tour[i] / 2;
        int seen = 0;
        for (int j = 0; j < count; j++) {
            if (abs(letour[j]) == city) {
                seen = 1;
                break;
            }
        }
        if (!seen) letour[count++] = (tour[i] % 2 == 1) ? -city : city;
    }

    {
        int pos0 = -1;
        for (int i = 0; i < count; i++) {
            if (letour[i] == 0) {
                pos0 = i;
                break;
            }
        }
        if (pos0 > 0) {
            int *rotated = (int*)mip_xmalloc((size_t)count * sizeof(int));
            int idx = 0;
            for (int i = pos0; i < count; i++) rotated[idx++] = letour[i];
            for (int i = 0; i < pos0; i++) rotated[idx++] = letour[i];
            memcpy(letour, rotated, (size_t)count * sizeof(int));
            free(rotated);
        }
    }

    *out_len = count;
    return letour;
}

void mip_orient_node_tour(int *tour, int len) {
    if (!tour || len <= 1) return;

    {
        int pos0 = -1;
        for (int i = 0; i < len; i++) {
            if (tour[i] == 0) {
                pos0 = i;
                break;
            }
        }
        if (pos0 > 0) {
            int *rotated = (int*)mip_xmalloc((size_t)len * sizeof(int));
            int idx = 0;
            for (int i = pos0; i < len; i++) rotated[idx++] = tour[i];
            for (int i = 0; i < pos0; i++) rotated[idx++] = tour[i];
            memcpy(tour, rotated, (size_t)len * sizeof(int));
            free(rotated);
        }
    }

    if (len > 1 && tour[1] == 1) {
        for (int i = 1, j = len - 1; i < j; i++, j--) {
            int tmp = tour[i];
            tour[i] = tour[j];
            tour[j] = tmp;
        }
    }
}

int __stdcall mip_subtourelim_directed(GRBmodel *model, void *cbdata, int where, void *usrdata) {
    mip_callback_data_t *cb = (mip_callback_data_t*)usrdata;
    int error = 0;

    (void)model;

    if (where == GRB_CB_MIPSOL) {
        double *sol = (double*)mip_xmalloc((size_t)cb->numvars * sizeof(double));
        int *tour = (int*)mip_xmalloc((size_t)cb->n * sizeof(int));
        int len = 0;

        GRBcbget(cbdata, where, GRB_CB_MIPSOL_SOL, sol);
        mip_findsubtour_directed(cb->n, sol, &len, tour);

        if (len < cb->n) {
            int max_pairs = len * (len - 1) / 2;
            int nz = 2 * max_pairs;
            int *ind = (int*)mip_xmalloc((size_t)nz * sizeof(int));
            double *val = (double*)mip_xmalloc((size_t)nz * sizeof(double));
            int k = 0;

            for (int a = 0; a < len; a++) {
                for (int b = a + 1; b < len; b++) {
                    int i = tour[a];
                    int j = tour[b];
                    ind[k] = i * cb->n + j;
                    val[k] = 1.0;
                    k++;
                    ind[k] = j * cb->n + i;
                    val[k] = 1.0;
                    k++;
                }
            }
            error = GRBcblazy(cbdata, k, ind, val, GRB_LESS_EQUAL, (double)len - 1.0);
            free(ind);
            free(val);
        }

        free(sol);
        free(tour);
    }

    return error;
}
