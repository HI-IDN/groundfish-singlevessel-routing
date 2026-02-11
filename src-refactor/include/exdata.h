#ifndef GSP_EXDATA_H
#define GSP_EXDATA_H

#include "dat_parser.h"

/* ExData structure - expanded data for optimization */
typedef struct {
    int SelectedSize;    /* ship + stations + selected_ports + waypoints */
    int Size;            /* ship + stations + selected_ports (excludes waypoints) */
    int *Type;
    int *ItemIndex;
    double *Amount;
    double *LatLonRad;
    double *LatLonDegMin;
} ExData;

ExData build_exdata(const ItemVec *items);
ExData build_exdata_no_ports(const ItemVec *items);
void free_exdata(ExData *ex);

#endif

