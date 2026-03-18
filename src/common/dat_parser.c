/*
 * DAT File Parser Implementation - Refactored with DataSet
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "../include/dat_parser.h"
#include "../include/geo_utils.h"

extern double fabs(double);
extern double floor(double);

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

static void *xrealloc(void *ptr, size_t n) {
    void *p = realloc(ptr, n);
    if (!p) die("OOM");
    return p;
}

static char *xstrdup(const char *s) {
    size_t n = strlen(s);
    char *p = (char*)xmalloc(n + 1);
    memcpy(p, s, n + 1);
    return p;
}

enum {
    DATASET_INITIAL_LOCATIONS = 1024,
    DATASET_INITIAL_BOATS = 8,
    DATASET_INITIAL_PORTS = 32,
    DATASET_INITIAL_STATIONS = 512,
    DATASET_INITIAL_WAYPOINTS = 64
};

/* Internal allocation tracking (not exposed in public API). */
static struct {
    int locations;
    int boats;
    int ports;
    int stations;
    int waypoints;
} alloc_sizes;

/* ---------- DataSet operations ---------- */

void dataset_init(DataSet *ds) {
    memset(ds, 0, sizeof(*ds));

    alloc_sizes.locations = DATASET_INITIAL_LOCATIONS;
    alloc_sizes.boats = DATASET_INITIAL_BOATS;
    alloc_sizes.ports = DATASET_INITIAL_PORTS;
    alloc_sizes.stations = DATASET_INITIAL_STATIONS;
    alloc_sizes.waypoints = DATASET_INITIAL_WAYPOINTS;

    ds->locations = (Location*)xmalloc(alloc_sizes.locations * sizeof(Location));
    ds->boats = (Boat*)xmalloc(alloc_sizes.boats * sizeof(Boat));
    ds->ports = (Port*)xmalloc(alloc_sizes.ports * sizeof(Port));
    ds->stations = (Station*)xmalloc(alloc_sizes.stations * sizeof(Station));
    ds->waypoints = (Waypoint*)xmalloc(alloc_sizes.waypoints * sizeof(Waypoint));
}

void dataset_free(DataSet *ds) {
    free(ds->locations);

    for (int i = 0; i < ds->n_boats; i++) {
        free(ds->boats[i].name);
    }
    free(ds->boats);

    for (int i = 0; i < ds->n_ports; i++) {
        free(ds->ports[i].name);
    }
    free(ds->ports);

    for (int i = 0; i < ds->n_stations; i++) {
        free(ds->stations[i].comment);
    }
    free(ds->stations);

    free(ds->waypoints);

    memset(ds, 0, sizeof(*ds));
}

/* Add or find existing location, returns location_id */
int dataset_add_location(DataSet *ds, int easting, int northing) {
    /* Check if location already exists */
    for (int i = 0; i < ds->n_locations; i++) {
        if (ds->locations[i].easting == easting &&
            ds->locations[i].northing == northing) {
            return i;
        }
    }

    /* Add new location */
    if (ds->n_locations == alloc_sizes.locations) {
        alloc_sizes.locations *= 2;
        ds->locations = (Location*)xrealloc(ds->locations,
            alloc_sizes.locations * sizeof(Location));
    }

    Location *loc = &ds->locations[ds->n_locations];
    loc->location_id = ds->n_locations;
    loc->easting = easting;
    loc->northing = northing;
    loc->lat_rad = degmin_to_rad((double)easting);
    loc->lon_rad = degmin_to_rad((double)northing);

    return ds->n_locations++;
}

void dataset_add_boat(DataSet *ds, const Item *item) {
    if (ds->n_boats == alloc_sizes.boats) {
        alloc_sizes.boats *= 2;
        ds->boats = (Boat*)xrealloc(ds->boats, alloc_sizes.boats * sizeof(Boat));
    }

    Boat *boat = &ds->boats[ds->n_boats];
    boat->boat_id = ds->n_boats;
    boat->name = xstrdup(item->Name);

    /* BoatData: [0-1]=docked location, [2]=capacity */
    int east = (int)item->BoatData[0];
    int north = (int)item->BoatData[1];

    boat->location_id = dataset_add_location(ds, east, north);
    boat->capacity = (int)item->BoatData[2];

    ds->n_boats++;
}

void dataset_add_port(DataSet *ds, const Item *item) {
    if (ds->n_ports == alloc_sizes.ports) {
        alloc_sizes.ports *= 2;
        ds->ports = (Port*)xrealloc(ds->ports, alloc_sizes.ports * sizeof(Port));
    }

    Port *port = &ds->ports[ds->n_ports];
    port->port_id = ds->n_ports;
    port->name = xstrdup(item->Name);

    int easting = (int)item->LatLonDegMin[0];
    int northing = (int)item->LatLonDegMin[1];
    port->location_id = dataset_add_location(ds, easting, northing);

    ds->n_ports++;
}

void dataset_add_station(DataSet *ds, const Item *item) {
    if (ds->n_stations == alloc_sizes.stations) {
        alloc_sizes.stations *= 2;
        ds->stations = (Station*)xrealloc(ds->stations,
            alloc_sizes.stations * sizeof(Station));
    }

    Station *station = &ds->stations[ds->n_stations];
    station->station_id = ds->n_stations;

    /* StationData: [0-1]=external_id, [2]=type, [3-6]=location, [7]=amount */
    int ext_id_1 = (int)item->StationData[0];
    int ext_id_2 = (int)item->StationData[1];
    station->external_id = abs(ext_id_1) * 10000 + abs(ext_id_2);

    int start_east = (int)item->StationData[3];
    int start_north = (int)item->StationData[4];
    int end_east = (int)item->StationData[5];
    int end_north = (int)item->StationData[6];

    station->start_location_id = dataset_add_location(ds, start_east, start_north);
    station->end_location_id = dataset_add_location(ds, end_east, end_north);
    station->amount = (int)item->StationData[7];

    station->comment = item->Comment ? xstrdup(item->Comment) : NULL;

    ds->n_stations++;
}

void dataset_add_waypoint(DataSet *ds, const Item *item) {
    if (ds->n_waypoints == alloc_sizes.waypoints) {
        alloc_sizes.waypoints *= 2;
        ds->waypoints = (Waypoint*)xrealloc(ds->waypoints,
            alloc_sizes.waypoints * sizeof(Waypoint));
    }

    Waypoint *waypoint = &ds->waypoints[ds->n_waypoints];
    waypoint->waypoint_id = ds->n_waypoints;

    int easting = (int)item->LatLonDegMin[0];
    int northing = (int)item->LatLonDegMin[1];
    waypoint->location_id = dataset_add_location(ds, easting, northing);

    ds->n_waypoints++;
}

/* ---------- Tokenization ---------- */

int tokenize_line(const char *line, char ***tokens_out) {
    int cap = 16, cnt = 0;
    char **tok = (char**)xmalloc(cap * sizeof(char*));
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
            tok = (char**)xrealloc(tok, cap * sizeof(char*));
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

/* ---------- Helper functions for parsing Items ---------- */

static void parse_boat_item(const char **tok, const char *line, Item *it) {
    memset(it, 0, sizeof(*it));
    it->Type = NODE_TYPE_BOAT;
    it->Name = xstrdup(tok[10]);
    it->RawLine = xstrdup(line);

    char *hash = strchr(line, '#');
    if (hash) {
        it->Comment = xstrdup(hash);
        size_t clen = strlen(it->Comment);
        while (clen > 0 && (it->Comment[clen-1] == '\n' || it->Comment[clen-1] == '\r')) {
            it->Comment[--clen] = '\0';
        }
    }

    it->BoatDataLen = 9;
    for (int i = 0; i < 9; i++) it->BoatData[i] = atof(tok[1 + i]);

    it->LatLonDegMin[0] = it->BoatData[0];
    it->LatLonDegMin[1] = it->BoatData[1];
    it->LatLonDegMin[2] = it->BoatData[0];
    it->LatLonDegMin[3] = it->BoatData[1];
    for (int k = 0; k < 4; k++) {
        it->LatLonRad[k] = degmin_to_rad(it->LatLonDegMin[k]);
    }
}

static void parse_station_item(const char **tok, const char *line, Item *it) {
    int datai[9];
    for (int i = 0; i < 9; i++) datai[i] = atoi(tok[1 + i]);

    if (datai[2] == 5) {
        it->Type = -1;  /* Mark as invalid */
        return;
    }

    memset(it, 0, sizeof(*it));
    it->Type = NODE_TYPE_STATION;
    it->RawLine = xstrdup(line);

    it->StationDataLen = 9;
    for (int s = 0; s < 9; s++) it->StationData[s] = (double)datai[s];

    char *hash = strchr(line, '#');
    if (hash) {
        it->Comment = xstrdup(hash);
        size_t clen = strlen(it->Comment);
        while (clen > 0 && (it->Comment[clen-1] == '\n' || it->Comment[clen-1] == '\r')) {
            it->Comment[--clen] = '\0';
        }
    }

    char nm[64];
    snprintf(nm, sizeof(nm), "%d %d", abs(datai[0]), abs(datai[1]));
    it->Name = xstrdup(nm);

    it->LatLonDegMin[0] = (double)datai[3];
    it->LatLonDegMin[1] = (double)datai[4];
    it->LatLonDegMin[2] = (double)datai[5];
    it->LatLonDegMin[3] = (double)datai[6];

    for (int k = 0; k < 4; k++) {
        it->LatLonRad[k] = degmin_to_rad(it->LatLonDegMin[k]);
    }
}

static void parse_port_item(const char **tok, const char *line, Item *it) {
    memset(it, 0, sizeof(*it));
    it->Type = NODE_TYPE_PORT;
    it->Name = xstrdup(tok[3]);
    it->RawLine = xstrdup(line);

    char *hash = strchr(line, '#');
    if (hash) {
        it->Comment = xstrdup(hash);
        size_t clen = strlen(it->Comment);
        while (clen > 0 && (it->Comment[clen-1] == '\n' || it->Comment[clen-1] == '\r')) {
            it->Comment[--clen] = '\0';
        }
    }

    it->LatLonDegMin[0] = atof(tok[1]);
    it->LatLonDegMin[1] = atof(tok[2]);
    it->LatLonDegMin[2] = it->LatLonDegMin[0];
    it->LatLonDegMin[3] = it->LatLonDegMin[1];

    for (int k = 0; k < 4; k++) {
        it->LatLonRad[k] = degmin_to_rad(it->LatLonDegMin[k]);
    }
}

static void parse_waypoint_item(const char **tok, const char *line, Item *it) {
    memset(it, 0, sizeof(*it));
    it->Type = NODE_TYPE_WAYPOINT;
    it->Name = xstrdup(GSP_DAT_TAG_WAYP);
    it->RawLine = xstrdup(line);

    char *hash = strchr(line, '#');
    if (hash) {
        it->Comment = xstrdup(hash);
        size_t clen = strlen(it->Comment);
        while (clen > 0 && (it->Comment[clen-1] == '\n' || it->Comment[clen-1] == '\r')) {
            it->Comment[--clen] = '\0';
        }
    }

    it->LatLonDegMin[0] = atof(tok[1]);
    it->LatLonDegMin[1] = atof(tok[2]);
    it->LatLonDegMin[2] = it->LatLonDegMin[0];
    it->LatLonDegMin[3] = it->LatLonDegMin[1];

    for (int k = 0; k < 4; k++) {
        it->LatLonRad[k] = degmin_to_rad(it->LatLonDegMin[k]);
    }
}

static void free_item(Item *it) {
    free(it->Name);
    free(it->RawLine);
    free(it->Comment);
}

static int should_keep_type(int type, int select_mask) {
    if (type == NODE_TYPE_BOAT) return (select_mask & GSP_DAT_SELECT_BOATS) != 0;
    if (type == NODE_TYPE_STATION) return (select_mask & GSP_DAT_SELECT_STATS) != 0;
    if (type == NODE_TYPE_PORT) return (select_mask & GSP_DAT_SELECT_PORTS) != 0;
    if (type == NODE_TYPE_WAYPOINT) return (select_mask & GSP_DAT_SELECT_WAYPS) != 0;
    return 0;
}

/* ---------- DAT file reading ---------- */

void read_dat_file_selected(const char *fname, DataSet *out_dataset, int select_mask) {
    FILE *fp = fopen(fname, "rb");
    if (!fp) {
        perror("fopen");
        exit(1);
    }

    int boat_count = 0;
    char line[4096];

    while (fgets(line, sizeof(line), fp)) {
        char **tok = NULL;
        int nt = tokenize_line(line, &tok);
        if (nt <= 1) {
            free_tokens(tok, nt);
            continue;
        }

        Item it;
        int valid = 0;

        if (strcmp(tok[0], GSP_DAT_TAG_BOAT) == 0 && nt >= 11) {
            parse_boat_item((const char**)tok, line, &it);
            valid = 1;
            boat_count++;
        } else if (strcmp(tok[0], GSP_DAT_TAG_STAT) == 0 && nt >= 10) {
            parse_station_item((const char**)tok, line, &it);
            valid = (it.Type == NODE_TYPE_STATION);
        } else if (strcmp(tok[0], GSP_DAT_TAG_PORT) == 0 && nt >= 4) {
            parse_port_item((const char**)tok, line, &it);
            valid = 1;
        } else if (strcmp(tok[0], GSP_DAT_TAG_WAYP) == 0 && nt >= 3) {
            parse_waypoint_item((const char**)tok, line, &it);
            valid = 1;
        }

        if (valid && should_keep_type(it.Type, select_mask)) {
            if (it.Type == NODE_TYPE_BOAT) {
                dataset_add_boat(out_dataset, &it);
            } else if (it.Type == NODE_TYPE_STATION) {
                dataset_add_station(out_dataset, &it);
            } else if (it.Type == NODE_TYPE_PORT) {
                dataset_add_port(out_dataset, &it);
            } else if (it.Type == NODE_TYPE_WAYPOINT) {
                dataset_add_waypoint(out_dataset, &it);
            }
            free_item(&it);
        } else if (valid) {
            free_item(&it);
        }

        free_tokens(tok, nt);
    }

    fclose(fp);
    if ((select_mask & GSP_DAT_SELECT_BOATS) != 0 && boat_count == 0) {
        die("No boats found in file");
    }
}

void read_dat_file_all(const char *fname, DataSet *out_dataset) {
    read_dat_file_selected(fname, out_dataset, GSP_DAT_SELECT_ALL);
}

void read_dat_file_boats(const char *fname, DataSet *out_dataset) {
    read_dat_file_selected(fname, out_dataset, GSP_DAT_SELECT_BOATS);
}

void read_dat_file_stations(const char *fname, DataSet *out_dataset) {
    read_dat_file_selected(fname, out_dataset, GSP_DAT_SELECT_STATS);
}

void read_dat_file_ports(const char *fname, DataSet *out_dataset) {
    read_dat_file_selected(fname, out_dataset, GSP_DAT_SELECT_PORTS);
}

void read_dat_file_waypoints(const char *fname, DataSet *out_dataset) {
    read_dat_file_selected(fname, out_dataset, GSP_DAT_SELECT_WAYPS);
}
