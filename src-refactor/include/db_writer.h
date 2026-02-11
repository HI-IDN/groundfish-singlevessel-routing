#ifndef GSP_DB_WRITER_H
#define GSP_DB_WRITER_H

#include "dat_parser.h"
#include <sqlite3.h>

/**
 * Write parsed ItemVec data to SQLite database
 *
 * @param db_path      Path to SQLite database file (will be created if doesn't exist)
 * @param items        ItemVec containing all parsed items
 * @param dat_filename Original .dat filename for metadata
 * @return             0 on success, non-zero on error
 */
int write_to_database(const char *db_path, const ItemVec *items, const char *dat_filename);

/**
 * Create database schema (tables)
 *
 * @param db          SQLite database handle
 * @return            0 on success, non-zero on error
 */
int create_schema(sqlite3 *db);

/**
 * Write a single boat and its associated data to database
 *
 * @param db          SQLite database handle
 * @param items       ItemVec for this boat
 * @param boat_index  Index of boat
 * @return            0 on success, non-zero on error
 */
int write_boat_data(sqlite3 *db, const ItemVec *items, int boat_index);

#endif /* GSP_DB_WRITER_H */

