#ifndef GSP_COASTLINE_DB_H
#define GSP_COASTLINE_DB_H

#include <sqlite3.h>

typedef struct {
	double *lat;
	double *lon;
	int n;
} CoastlinePoints;

typedef enum {
	GSP_SEED_MODE_NONE = 0,
	GSP_SEED_MODE_PRESERVE_ALL = 1,
	GSP_SEED_MODE_HINTS_ONLY = 2
} GspSeedMode;

typedef struct {
	int min_points;
	int max_points;
	int target_points;
	int use_dat_waypoints;
	GspSeedMode seed_mode;
} WaypointGenerationOptions;

/**
 * Load island.bin file (land polygon data) - initializes global MAP structure
 *
 * @param fname - Path to island.bin file
 * @param out_n - Output: number of polygon points
 * @return Array of doubles [lat1..latN, lon1..lonN], caller must free
 */
double *load_island_bin(const char *fname, int *out_n);

int load_repaired_coastline_from_bin(const char *island_bin_path, CoastlinePoints *out);

void free_coastline_points(CoastlinePoints *pts);

/**
 * Import island.bin polygon data into database coastline table
 *
 * @param db - SQLite database handle
 * @param island_bin_path - Path to island.bin file
 * @return SQLITE_OK on success, error code otherwise
 */
int import_coastline_to_db(sqlite3 *db, const char *island_bin_path);

int replace_coastline_in_db(sqlite3 *db, const CoastlinePoints *coastline);

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


