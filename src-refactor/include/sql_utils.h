#ifndef GSP_SQL_UTILS_H
#define GSP_SQL_UTILS_H

#include <sqlite3.h>

/*
 * Count rows returned by a SELECT query
 * Returns: number of rows, or -1 on error
 */
int sql_count_rows(sqlite3 *db, const char *query);

/*
 * Prepare a SQL statement
 * Returns: compiled statement on success, NULL on error
 * Caller must finalize the statement when done
 */
sqlite3_stmt *sql_prepare(sqlite3 *db, const char *query);

/*
 * Execute a non-SELECT query (INSERT, UPDATE, DELETE, etc)
 * Returns: SQLITE_OK on success, error code on failure
 */
int sql_execute(sqlite3 *db, const char *query);

/*
 * Begin a transaction
 */
int sql_begin_transaction(sqlite3 *db);

/*
 * Commit a transaction
 */
int sql_commit(sqlite3 *db);

/*
 * Rollback a transaction
 */
int sql_rollback(sqlite3 *db);

#endif /* GSP_SQL_UTILS_H */

