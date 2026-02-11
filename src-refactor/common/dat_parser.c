/*
 * DAT File Parser Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#ifndef PI
#define PI 3.14159265358979323846
#endif

/* Explicit declarations for math functions (workaround for some compilers) */
extern double fabs(double);
extern double floor(double);

#include "../include/dat_parser.h"

/* Utility helpers */
static void die(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    exit(1);
}

static void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) die("OOM");
    return p;
}

static char *xstrdup(const char *s) {
    size_t n = strlen(s);
    char *p = (char*)xmalloc(n + 1);
    memcpy(p, s, n + 1);
    return p;
}

/* ItemVec operations */
void item_vec_init(ItemVec *v) {
    v->n = 0;
    v->cap = 64;
    v->a = (Item*)xmalloc((size_t)v->cap * sizeof(Item));
}

void item_vec_push(ItemVec *v, Item it) {
    if (v->n == v->cap) {
        v->cap *= 2;
        v->a = (Item*)realloc(v->a, (size_t)v->cap * sizeof(Item));
        if (!v->a) die("OOM");
    }
    v->a[v->n++] = it;
}

void item_vec_free(ItemVec *v) {
    for (int i = 0; i < v->n; i++) {
        free(v->a[i].Name);
        free(v->a[i].RawLine);
        free(v->a[i].Comment);
    }
    free(v->a);
}

/* Tokenization */
int tokenize_line(const char *line, char ***tokens_out) {
    int cap = 16, cnt = 0;
    char **tok = (char**)xmalloc((size_t)cap * sizeof(char*));
    const char *p = line;

    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        const char *start = p, *end = NULL;
        if (*p == '"') {
            p++;
            while (*p && *p != '"') p++;
            if (*p == '"') p++;
            end = p;
        } else {
            while (*p && !isspace((unsigned char)*p)) p++;
            end = p;
        }

        size_t len = (size_t)(end - start);
        char *t = (char*)xmalloc(len + 1);
        memcpy(t, start, len);
        t[len] = '\0';

        if (cnt == cap) {
            cap *= 2;
            tok = (char**)realloc(tok, (size_t)cap * sizeof(char*));
            if (!tok) die("OOM");
        }
        tok[cnt++] = t;
    }

    *tokens_out = tok;
    return cnt;
}

void free_tokens(char **tok, int cnt) {
    for (int i = 0; i < cnt; i++) free(tok[i]);
    free(tok);
}

/* Coordinate conversion */
double degmin2rad(double degmin_in) {
    double degmin = degmin_in;
    if (fabs(degmin) < 10000.0) degmin *= 100.0;
    double m = (degmin / 100.0) - floor(degmin / 10000.0) * 100.0;
    double deg = (degmin + (200.0 / 3.0) * m) / 10000.0;
    return deg * PI / 180.0;
}

double degmin2deg(double degmin) {
    double m = (degmin / 100.0) - floor(degmin / 10000.0) * 100.0;
    return (degmin + (200.0 / 3.0) * m) / 10000.0;
}

/* DAT file reading */
void read_dat_file(const char *fname, const char *ship_name_plain,
                   ItemVec *out_items, double *out_shipCap, int skip_ports) {
    char ship_token[256];
    snprintf(ship_token, sizeof(ship_token), "\"%s\"", ship_name_plain);

    FILE *fp = fopen(fname, "rb");
    if (!fp) {
        perror("fopen");
        exit(1);
    }

    int found_ship = 0;
    double ShipCap = 0.0;
    int tag = 0; /* 0 ALL, 1 WAYPONLY */

    char line[4096];
    while (fgets(line, sizeof(line), fp)) {
        char **tok = NULL;
        int nt = tokenize_line(line, &tok);
        if (nt <= 1) {
            free_tokens(tok, nt);
            break;
        }

        const char *se = "IGNORE";
        if (tag == 0) se = tok[0];
        else if (tag == 1 && strcmp(tok[0], "WAYP") == 0) se = tok[0];

        if (strcmp(se, "BOAT") == 0) {
            if (found_ship) {
                tag = 1;
            } else if (nt >= 13 && strcmp(tok[12], ship_token) == 0) {
                double data[11];
                for (int i = 0; i < 11; i++) data[i] = atof(tok[1 + i]);
                found_ship = 1;
                ShipCap = data[4];

                Item it;
                memset(&it, 0, sizeof(it));
                it.Type = tSHIP;
                it.Name = xstrdup(ship_token);
                it.Amount = 0;
                it.ExtraTime = 0;
                it.RawLine = xstrdup(line);

                char *hash = strchr(line, '#');
                if (hash) {
                    it.Comment = xstrdup(hash);
                    size_t clen = strlen(it.Comment);
                    while (clen > 0 && (it.Comment[clen-1] == '\n' || it.Comment[clen-1] == '\r')) {
                        it.Comment[--clen] = '\0';
                    }
                }

                it.BoatDataLen = 11;
                for (int i = 0; i < 11; i++) it.BoatData[i] = data[i];
                it.LatLonDegMin[0] = data[0];
                it.LatLonDegMin[1] = data[1];
                it.LatLonDegMin[2] = data[2];
                it.LatLonDegMin[3] = data[3];
                for (int k = 0; k < 4; k++) {
                    it.LatLonRad[k] = degmin2rad(it.LatLonDegMin[k]);
                }

                item_vec_push(out_items, it);
            }
        } else if (strcmp(se, "STAT") == 0 && found_ship) {
            if (nt >= 10) {
                int datai[9];
                for (int i = 0; i < 9; i++) datai[i] = atoi(tok[1 + i]);

                if (datai[2] != 5) {
                    Item it;
                    memset(&it, 0, sizeof(it));
                    it.Type = tSTAT;
                    it.Fixed = (datai[2] == 2);
                    it.Rotated = (datai[2] == 1);
                    it.Amount = (double)datai[7];
                    it.ExtraTime = (double)datai[8];
                    it.Reitur = abs(datai[0]);
                    it.Tog = abs(datai[1]);
                    it.RawLine = xstrdup(line);

                    char *hash = strchr(line, '#');
                    if (hash) {
                        it.Comment = xstrdup(hash);
                        size_t clen = strlen(it.Comment);
                        while (clen > 0 && (it.Comment[clen-1] == '\n' || it.Comment[clen-1] == '\r')) {
                            it.Comment[--clen] = '\0';
                        }
                    }

                    char nm[64];
                    snprintf(nm, sizeof(nm), "%d %d", abs(datai[0]), abs(datai[1]));
                    it.Name = xstrdup(nm);

                    it.LatLonDegMin[0] = (double)datai[3];
                    it.LatLonDegMin[1] = (double)datai[4];
                    it.LatLonDegMin[2] = (double)datai[5];
                    it.LatLonDegMin[3] = (double)datai[6];

                    for (int k = 0; k < 4; k++) {
                        it.LatLonRad[k] = degmin2rad(it.LatLonDegMin[k]);
                    }

                    item_vec_push(out_items, it);
                }
            }
        } else if (strcmp(se, "PORT") == 0 && found_ship) {
            if (nt >= 5) {
                Item it;
                memset(&it, 0, sizeof(it));
                it.Type = tPORT;
                it.Name = xstrdup(tok[3]);
                it.Amount = 0;
                it.ExtraTime = 0;
                it.PortSelected = atoi(tok[4]);
                if (skip_ports) it.PortSelected = 0;
                it.RawLine = xstrdup(line);

                char *hash = strchr(line, '#');
                if (hash) {
                    it.Comment = xstrdup(hash);
                    size_t clen = strlen(it.Comment);
                    while (clen > 0 && (it.Comment[clen-1] == '\n' || it.Comment[clen-1] == '\r')) {
                        it.Comment[--clen] = '\0';
                    }
                }

                it.LatLonDegMin[0] = atof(tok[1]);
                it.LatLonDegMin[1] = atof(tok[2]);
                it.LatLonDegMin[2] = it.LatLonDegMin[0];
                it.LatLonDegMin[3] = it.LatLonDegMin[1];

                for (int k = 0; k < 4; k++) {
                    it.LatLonRad[k] = degmin2rad(it.LatLonDegMin[k]);
                }

                item_vec_push(out_items, it);
            }
        } else if (strcmp(se, "WAYP") == 0) {
            if (nt >= 4 && strcmp(tok[3], "-1") != 0) {
                Item it;
                memset(&it, 0, sizeof(it));
                it.Type = tWAYP;
                it.Name = xstrdup("Wayp");
                it.Amount = 0;
                it.ExtraTime = 0;
                it.RawLine = xstrdup(line);

                char *hash = strchr(line, '#');
                if (hash) {
                    it.Comment = xstrdup(hash);
                    size_t clen = strlen(it.Comment);
                    while (clen > 0 && (it.Comment[clen-1] == '\n' || it.Comment[clen-1] == '\r')) {
                        it.Comment[--clen] = '\0';
                    }
                }

                it.LatLonDegMin[0] = atof(tok[1]);
                it.LatLonDegMin[1] = atof(tok[2]);
                it.LatLonDegMin[2] = it.LatLonDegMin[0];
                it.LatLonDegMin[3] = it.LatLonDegMin[1];

                for (int k = 0; k < 4; k++) {
                    it.LatLonRad[k] = degmin2rad(it.LatLonDegMin[k]);
                }

                item_vec_push(out_items, it);
            }
        }

        free_tokens(tok, nt);
    }

    fclose(fp);
    if (!found_ship) die("Ship not found in file (name mismatch?)");
    *out_shipCap = ShipCap;
}

/* Read ALL boats from .dat file */
void read_dat_file_all_boats(const char *fname, ItemVec *out_items, int skip_ports) {
    FILE *fp = fopen(fname, "rb");
    if (!fp) {
        perror("fopen");
        exit(1);
    }

    int tag = 0; /* 0 ALL, 1 WAYPONLY */
    int boat_count = 0;

    char line[4096];
    while (fgets(line, sizeof(line), fp)) {
        char **tok = NULL;
        int nt = tokenize_line(line, &tok);
        if (nt <= 1) {
            free_tokens(tok, nt);
            break;
        }

        const char *se = "IGNORE";
        if (tag == 0) se = tok[0];
        else if (tag == 1 && strcmp(tok[0], "WAYP") == 0) se = tok[0];

        if (strcmp(se, "BOAT") == 0) {
            if (nt >= 13) {
                double data[11];
                for (int i = 0; i < 11; i++) data[i] = atof(tok[1 + i]);

                Item it;
                memset(&it, 0, sizeof(it));
                it.Type = tSHIP;
                it.Name = xstrdup(tok[12]);
                it.Amount = 0;
                it.ExtraTime = 0;
                it.RawLine = xstrdup(line);

                char *hash = strchr(line, '#');
                if (hash) {
                    it.Comment = xstrdup(hash);
                    size_t clen = strlen(it.Comment);
                    while (clen > 0 && (it.Comment[clen-1] == '\n' || it.Comment[clen-1] == '\r')) {
                        it.Comment[--clen] = '\0';
                    }
                }

                it.BoatDataLen = 11;
                for (int i = 0; i < 11; i++) it.BoatData[i] = data[i];
                it.LatLonDegMin[0] = data[0];
                it.LatLonDegMin[1] = data[1];
                it.LatLonDegMin[2] = data[2];
                it.LatLonDegMin[3] = data[3];
                for (int k = 0; k < 4; k++) {
                    it.LatLonRad[k] = degmin2rad(it.LatLonDegMin[k]);
                }

                item_vec_push(out_items, it);
                boat_count++;
            }
        } else if (strcmp(se, "STAT") == 0 && boat_count > 0) {
            if (nt >= 10) {
                int datai[9];
                for (int i = 0; i < 9; i++) datai[i] = atoi(tok[1 + i]);

                if (datai[2] != 5) {
                    Item it;
                    memset(&it, 0, sizeof(it));
                    it.Type = tSTAT;
                    it.Fixed = (datai[2] == 2);
                    it.Rotated = (datai[2] == 1);
                    it.Amount = (double)datai[7];
                    it.ExtraTime = (double)datai[8];
                    it.Reitur = abs(datai[0]);
                    it.Tog = abs(datai[1]);
                    it.RawLine = xstrdup(line);

                    char *hash = strchr(line, '#');
                    if (hash) {
                        it.Comment = xstrdup(hash);
                        size_t clen = strlen(it.Comment);
                        while (clen > 0 && (it.Comment[clen-1] == '\n' || it.Comment[clen-1] == '\r')) {
                            it.Comment[--clen] = '\0';
                        }
                    }

                    char nm[64];
                    snprintf(nm, sizeof(nm), "%d %d", abs(datai[0]), abs(datai[1]));
                    it.Name = xstrdup(nm);

                    it.LatLonDegMin[0] = (double)datai[3];
                    it.LatLonDegMin[1] = (double)datai[4];
                    it.LatLonDegMin[2] = (double)datai[5];
                    it.LatLonDegMin[3] = (double)datai[6];

                    for (int k = 0; k < 4; k++) {
                        it.LatLonRad[k] = degmin2rad(it.LatLonDegMin[k]);
                    }

                    item_vec_push(out_items, it);
                }
            }
        } else if (strcmp(se, "PORT") == 0 && boat_count > 0) {
            if (nt >= 5) {
                Item it;
                memset(&it, 0, sizeof(it));
                it.Type = tPORT;
                it.Name = xstrdup(tok[3]);
                it.Amount = 0;
                it.ExtraTime = 0;
                it.PortSelected = atoi(tok[4]);
                if (skip_ports) it.PortSelected = 0;
                it.RawLine = xstrdup(line);

                char *hash = strchr(line, '#');
                if (hash) {
                    it.Comment = xstrdup(hash);
                    size_t clen = strlen(it.Comment);
                    while (clen > 0 && (it.Comment[clen-1] == '\n' || it.Comment[clen-1] == '\r')) {
                        it.Comment[--clen] = '\0';
                    }
                }

                it.LatLonDegMin[0] = atof(tok[1]);
                it.LatLonDegMin[1] = atof(tok[2]);
                it.LatLonDegMin[2] = it.LatLonDegMin[0];
                it.LatLonDegMin[3] = it.LatLonDegMin[1];

                for (int k = 0; k < 4; k++) {
                    it.LatLonRad[k] = degmin2rad(it.LatLonDegMin[k]);
                }

                item_vec_push(out_items, it);
            }
        } else if (strcmp(se, "WAYP") == 0) {
            if (nt >= 4 && strcmp(tok[3], "-1") != 0) {
                Item it;
                memset(&it, 0, sizeof(it));
                it.Type = tWAYP;
                it.Name = xstrdup("Wayp");
                it.Amount = 0;
                it.ExtraTime = 0;
                it.RawLine = xstrdup(line);

                char *hash = strchr(line, '#');
                if (hash) {
                    it.Comment = xstrdup(hash);
                    size_t clen = strlen(it.Comment);
                    while (clen > 0 && (it.Comment[clen-1] == '\n' || it.Comment[clen-1] == '\r')) {
                        it.Comment[--clen] = '\0';
                    }
                }

                it.LatLonDegMin[0] = atof(tok[1]);
                it.LatLonDegMin[1] = atof(tok[2]);
                it.LatLonDegMin[2] = it.LatLonDegMin[0];
                it.LatLonDegMin[3] = it.LatLonDegMin[1];

                for (int k = 0; k < 4; k++) {
                    it.LatLonRad[k] = degmin2rad(it.LatLonDegMin[k]);
                }

                item_vec_push(out_items, it);
            }
        }

        free_tokens(tok, nt);
    }

    fclose(fp);
    if (boat_count == 0) die("No boats found in file");
}

/* Count boats in ItemVec */
int count_boats(const ItemVec *items) {
    int count = 0;
    for (int i = 0; i < items->n; i++) {
        if (items->a[i].Type == tSHIP) count++;
    }
    return count;
}

/* Get boat names */
int get_boat_names(const ItemVec *items, char ***out_names) {
    int n_boats = count_boats(items);
    if (n_boats == 0) return 0;

    char **names = (char**)xmalloc((size_t)n_boats * sizeof(char*));
    int idx = 0;
    for (int i = 0; i < items->n; i++) {
        if (items->a[i].Type == tSHIP) {
            names[idx++] = xstrdup(items->a[i].Name);
        }
    }

    *out_names = names;
    return n_boats;
}

/* Filter items by boat index */
void filter_items_by_boat(const ItemVec *items, int boat_index,
                         ItemVec *out_items, double *out_shipCap) {
    /* Find the boat at boat_index */
    int boat_count = 0;
    int boat_item_idx = -1;
    for (int i = 0; i < items->n; i++) {
        if (items->a[i].Type == tSHIP) {
            if (boat_count == boat_index) {
                boat_item_idx = i;
                break;
            }
            boat_count++;
        }
    }

    if (boat_item_idx < 0) die("Invalid boat index");

    /* Get ship capacity */
    if (out_shipCap) {
        *out_shipCap = items->a[boat_item_idx].BoatData[4];
    }

    /* Deep copy the boat */
    Item boat_copy = items->a[boat_item_idx];
    boat_copy.Name = xstrdup(items->a[boat_item_idx].Name ? items->a[boat_item_idx].Name : "");
    boat_copy.RawLine = xstrdup(items->a[boat_item_idx].RawLine ? items->a[boat_item_idx].RawLine : "");
    boat_copy.Comment = items->a[boat_item_idx].Comment ? xstrdup(items->a[boat_item_idx].Comment) : NULL;
    item_vec_push(out_items, boat_copy);

    /* Find the range of items for this boat */
    int start_idx = boat_item_idx + 1;
    int end_idx = items->n;

    /* Find the next boat (or end of file) */
    for (int i = start_idx; i < items->n; i++) {
        if (items->a[i].Type == tSHIP) {
            end_idx = i;
            break;
        }
    }

    /* Deep copy all STAT and PORT items for this boat */
    for (int i = start_idx; i < end_idx; i++) {
        if (items->a[i].Type == tSTAT || items->a[i].Type == tPORT) {
            Item copy = items->a[i];
            copy.Name = xstrdup(items->a[i].Name ? items->a[i].Name : "");
            copy.RawLine = xstrdup(items->a[i].RawLine ? items->a[i].RawLine : "");
            copy.Comment = items->a[i].Comment ? xstrdup(items->a[i].Comment) : NULL;
            item_vec_push(out_items, copy);
        }
    }

    /* Deep copy all WAYP items (they're shared by all boats) */
    for (int i = 0; i < items->n; i++) {
        if (items->a[i].Type == tWAYP) {
            Item copy = items->a[i];
            copy.Name = xstrdup(items->a[i].Name ? items->a[i].Name : "");
            copy.RawLine = xstrdup(items->a[i].RawLine ? items->a[i].RawLine : "");
            copy.Comment = items->a[i].Comment ? xstrdup(items->a[i].Comment) : NULL;
            item_vec_push(out_items, copy);
        }
    }
}
