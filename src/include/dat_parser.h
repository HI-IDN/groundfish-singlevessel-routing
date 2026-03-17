#ifndef GSP_DAT_PARSER_H
#define GSP_DAT_PARSER_H

#include <stdio.h>
#include "constants.h"

/* ---------- Item structure (single record from .dat, used during parsing) ---------- */
typedef struct {
    int Type;
    double LatLonRad[4];
    double LatLonDegMin[4];
    char *Name;
    char *RawLine;
    char *Comment;
    double BoatData[11];
    int BoatDataLen;
    double StationData[9];
    int StationDataLen;
} Item;

/* --- Structures for parsed data --- */

/* Location: represents a unique geographic position */
typedef struct
{
    int location_id;
    int easting;      /* ISN93 easting (degmin format) */
    int northing;     /* ISN93 northing (degmin format) */
    double lat_rad;   /* Converted latitude in radians */
    double lon_rad;   /* Converted longitude in radians */
} Location;

/* Boat: vessel information */
typedef struct
{
    int boat_id;
    char *name;
    int capacity;
    int start_location_id;
    int end_location_id;
} Boat;

/* Station: sampling location with catch data */
typedef struct
{
    int station_id;
    int amount;
    char *comment;
    int external_id;
    int start_location_id;
    int end_location_id;
} Station;

/* Port: named harbor location */
typedef struct
{
    int port_id;
    char *name;
    int location_id;
} Port;

/* Waypoint: navigable position */
typedef struct
{
    int waypoint_id;
    int location_id;
} Waypoint;

/* ---------- DataSet (complete parsed dataset) ---------- */
typedef struct {
    Location *locations;
    int n_locations;

    Boat *boats;
    int n_boats;

    Port *ports;
    int n_ports;

    Station *stations;
    int n_stations;

    Waypoint *waypoints;
    int n_waypoints;
} DataSet;

/* ---------- DataSet operations ---------- */
void dataset_init(DataSet *ds);
void dataset_free(DataSet *ds);
int dataset_add_location(DataSet *ds, int easting, int northing);
void dataset_add_boat(DataSet *ds, const Item *item);
void dataset_add_port(DataSet *ds, const Item *item);
void dataset_add_station(DataSet *ds, const Item *item);
void dataset_add_waypoint(DataSet *ds, const Item *item);

/* ---------- Tokenization ---------- */
int tokenize_line(const char *line, char ***tokens_out);
void free_tokens(char **tok, int cnt);

/* ---------- DAT file reading ---------- */
enum {
    GSP_DAT_SELECT_BOATS = 1 << 0,
    GSP_DAT_SELECT_STATS = 1 << 1,
    GSP_DAT_SELECT_PORTS = 1 << 2,
    GSP_DAT_SELECT_WAYPS = 1 << 3,
    GSP_DAT_SELECT_ALL = GSP_DAT_SELECT_BOATS | GSP_DAT_SELECT_STATS |
                         GSP_DAT_SELECT_PORTS | GSP_DAT_SELECT_WAYPS
};

void read_dat_file_selected(const char *fname, DataSet *out_dataset, int select_mask);
void read_dat_file_all(const char *fname, DataSet *out_dataset);
void read_dat_file_boats(const char *fname, DataSet *out_dataset);
void read_dat_file_stations(const char *fname, DataSet *out_dataset);
void read_dat_file_ports(const char *fname, DataSet *out_dataset);
void read_dat_file_waypoints(const char *fname, DataSet *out_dataset);

#endif /* GSP_DAT_PARSER_H */
