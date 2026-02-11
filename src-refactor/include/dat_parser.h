#ifndef GSP_DAT_PARSER_H
#define GSP_DAT_PARSER_H

#include <stdio.h>

/* ---------- Type constants ---------- */
enum {
    tSHIP = 1,
    tSTAT = 2,
    tWAYP = 3,
    tENDP = 4,
    tPORT = 5
};

/* ---------- Item structure (single record from .dat) ---------- */
typedef struct {
    int Type;
    int Fixed;
    int Rotated;
    double LatLonRad[4];      /* radians */
    double LatLonDegMin[4];   /* original degmin numbers (needed for crossesland) */
    char *Name;
    char *RawLine;
    char *Comment;
    int Reitur;
    int Tog;
    int PortSelected;
    double BoatData[11];
    int BoatDataLen;
    double Amount;
    double ExtraTime;
} Item;

/* ---------- ItemVec (dynamic array of Items) ---------- */
typedef struct {
    Item *a;
    int n;
    int cap;
} ItemVec;

/* ---------- ItemVec operations ---------- */
void item_vec_init(ItemVec *v);
void item_vec_push(ItemVec *v, Item it);
void item_vec_free(ItemVec *v);

/* ---------- Tokenization ---------- */
int tokenize_line(const char *line, char ***tokens_out);
void free_tokens(char **tok, int cnt);

/* ---------- Coordinate conversion ---------- */
double degmin2rad(double degmin_in);
double degmin2deg(double degmin);

/* ---------- DAT file reading ---------- */
/**
 * Read .dat file and populate ItemVec with ALL boats and their associated data.
 * Each boat will have its own STAT, PORT, WAYP records following it.
 *
 * @param fname        Path to .dat file
 * @param out_items    Output ItemVec to populate (contains all boats and their data)
 * @param skip_ports   If non-zero, deselect all ports
 */
void read_dat_file_all_boats(const char *fname,
                              ItemVec *out_items,
                              int skip_ports);

/**
 * Read .dat file for a SPECIFIC boat and populate ItemVec.
 * Legacy function - use read_dat_file_all_boats() for new code.
 *
 * @param fname        Path to .dat file
 * @param ship_name    Name of ship to match (plain text, without quotes)
 * @param out_items    Output ItemVec to populate
 * @param out_shipCap  Output ship capacity
 * @param skip_ports   If non-zero, deselect all ports
 */
void read_dat_file(const char *fname,
                   const char *ship_name,
                   ItemVec *out_items,
                   double *out_shipCap,
                   int skip_ports);

/**
 * Get number of boats in ItemVec
 */
int count_boats(const ItemVec *items);

/**
 * Get boat names from ItemVec
 * @param items        ItemVec to search
 * @param out_names    Output array of boat names (caller must free each string)
 * @return             Number of boats found
 */
int get_boat_names(const ItemVec *items, char ***out_names);

/**
 * Filter ItemVec to contain only data for a specific boat (by index)
 * @param items        Input ItemVec with all boats
 * @param boat_index   Index of boat to keep (0-based)
 * @param out_items    Output ItemVec with only selected boat's data
 * @param out_shipCap  Output ship capacity
 */
void filter_items_by_boat(const ItemVec *items, int boat_index,
                         ItemVec *out_items, double *out_shipCap);

#endif /* GSP_DAT_PARSER_H */

