#ifndef GSP_COASTLINE_DB_H
#define GSP_COASTLINE_DB_H

#include <sqlite3.h>

/**
 * Load island.bin file (land polygon data) - initializes global MAP structure
 *
 * @param fname - Path to island.bin file
 * @param out_n - Output: number of polygon points
 * @return Array of doubles [lat1..latN, lon1..lonN], caller must free
 */
double *load_island_bin(const char *fname, int *out_n);

/**
 * Import island.bin polygon data into database coastline table
 *
 * @param db - SQLite database handle
 * @param island_bin_path - Path to island.bin file
 * @return SQLITE_OK on success, error code otherwise
 */
int import_coastline_to_db(sqlite3 *db, const char *island_bin_path);

/**
 * Load island polygon data from database coastline table
 * Initializes global MAP structure used by distance computation
 *
 * @param db - SQLite database handle
 * @param out_n - Output: number of polygon points
 * @return Array of doubles [lat1..latN, lon1..lonN], caller must free
 */
double *load_coastline_from_db(sqlite3 *db, int *out_n);

#endif


