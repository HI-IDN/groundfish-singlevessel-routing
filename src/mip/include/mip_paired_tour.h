#ifndef GSP_MIP_PAIRED_TOUR_H
#define GSP_MIP_PAIRED_TOUR_H

#include "mip_common.h"

#ifndef __stdcall
#define __stdcall
#endif

typedef struct {
    int n;
    int numvars;
} mip_callback_data_t;

void mip_findsubtour_directed(int n, const double *sol, int *tourlen_out, int *tour_out);
int *mip_node_tour_to_letour(const int *tour, int len, int size, int *out_len);
void mip_orient_node_tour(int *tour, int len);
int __stdcall mip_subtourelim_directed(GRBmodel *model, void *cbdata, int where, void *usrdata);

#endif
