/*
 * ExData Implementation
 * Build expanded data structures from ItemVec
 */

#include <stdio.h>
#include <stdlib.h>
#include "../include/exdata.h"

static void die(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    exit(1);
}

static void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) die("OOM");
    return p;
}

/* Helper: append items of a specific type to ExData */
static void append_type_ex(const ItemVec *items, int typ, ExData *ex, int *kptr) {
    int k = *kptr;
    for (int i = 0; i < items->n; i++) {
        if (items->a[i].Type != typ) continue;
        ex->Type[k] = items->a[i].Type;
        ex->ItemIndex[k] = i;
        ex->Amount[k] = items->a[i].Amount;
        for (int t = 0; t < 4; t++) {
            ex->LatLonRad[k*4 + t] = items->a[i].LatLonRad[t];
            ex->LatLonDegMin[k*4 + t] = items->a[i].LatLonDegMin[t];
        }
        k++;
    }
    *kptr = k;
}

/* Build ExData with selected ports included */
ExData build_exdata(const ItemVec *items) {
    int c_ship = 0, c_stat = 0, c_port = 0, c_wayp = 0;

    for (int i = 0; i < items->n; i++) {
        if (items->a[i].Type == tSHIP) c_ship++;
        else if (items->a[i].Type == tSTAT) c_stat++;
        else if (items->a[i].Type == tPORT && items->a[i].PortSelected) c_port++;
        else if (items->a[i].Type == tWAYP) c_wayp++;
    }

    ExData ex;
    ex.SelectedSize = c_ship + c_stat + c_port + c_wayp;
    ex.Size = c_ship + c_stat + c_port;
    ex.Type = (int*)xmalloc((size_t)ex.SelectedSize * sizeof(int));
    ex.ItemIndex = (int*)xmalloc((size_t)ex.SelectedSize * sizeof(int));
    ex.Amount = (double*)xmalloc((size_t)ex.SelectedSize * sizeof(double));
    ex.LatLonRad = (double*)xmalloc((size_t)ex.SelectedSize * 4 * sizeof(double));
    ex.LatLonDegMin = (double*)xmalloc((size_t)ex.SelectedSize * 4 * sizeof(double));

    int k = 0;
    append_type_ex(items, tSHIP, &ex, &k);
    append_type_ex(items, tSTAT, &ex, &k);

    /* ports participate in optimization only when selected */
    for (int i = 0; i < items->n; i++) {
        if (items->a[i].Type != tPORT) continue;
        if (!items->a[i].PortSelected) continue;
        ex.Type[k] = items->a[i].Type;
        ex.ItemIndex[k] = i;
        ex.Amount[k] = items->a[i].Amount;
        for (int t = 0; t < 4; t++) {
            ex.LatLonRad[k*4 + t] = items->a[i].LatLonRad[t];
            ex.LatLonDegMin[k*4 + t] = items->a[i].LatLonDegMin[t];
        }
        k++;
    }

    append_type_ex(items, tWAYP, &ex, &k);

    if (k != ex.SelectedSize) die("build_exdata mismatch");
    return ex;
}

/* Build ExData without any ports */
ExData build_exdata_no_ports(const ItemVec *items) {
    int c_ship = 0, c_stat = 0, c_wayp = 0;

    for (int i = 0; i < items->n; i++) {
        if (items->a[i].Type == tSHIP) c_ship++;
        else if (items->a[i].Type == tSTAT) c_stat++;
        else if (items->a[i].Type == tWAYP) c_wayp++;
    }

    ExData ex;
    ex.SelectedSize = c_ship + c_stat + c_wayp;
    ex.Size = c_ship + c_stat;
    ex.Type = (int*)xmalloc((size_t)ex.SelectedSize * sizeof(int));
    ex.ItemIndex = (int*)xmalloc((size_t)ex.SelectedSize * sizeof(int));
    ex.Amount = (double*)xmalloc((size_t)ex.SelectedSize * sizeof(double));
    ex.LatLonRad = (double*)xmalloc((size_t)ex.SelectedSize * 4 * sizeof(double));
    ex.LatLonDegMin = (double*)xmalloc((size_t)ex.SelectedSize * 4 * sizeof(double));

    int k = 0;
    append_type_ex(items, tSHIP, &ex, &k);
    append_type_ex(items, tSTAT, &ex, &k);
    append_type_ex(items, tWAYP, &ex, &k);

    if (k != ex.SelectedSize) die("build_exdata_no_ports mismatch");
    return ex;
}

/* Free ExData resources */
void free_exdata(ExData *ex) {
    free(ex->Type);
    free(ex->ItemIndex);
    free(ex->Amount);
    free(ex->LatLonRad);
    free(ex->LatLonDegMin);
}
