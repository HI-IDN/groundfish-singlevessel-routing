/*
 * SQL Utility Functions
 * Common wrappers for SQLite query operations
 */

#include <stdio.h>
#include <stdlib.h>
#include <sqlite3.h>

/*
 * Execute a SELECT query and count rows
 * Returns: number of rows, or -1 on error
 */
int sql_count_rows(sqlite3 *db, const char *query) {
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "    ERROR SQL prepare error: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        count++;
    }
    sqlite3_finalize(stmt);

    return count;
}

/*
 * Prepare a statement and return it
 * Returns: statement on success, NULL on error
 */
sqlite3_stmt *sql_prepare(sqlite3 *db, const char *query) {
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "    ERROR SQL prepare error: %s\n", sqlite3_errmsg(db));
        return NULL;
    }
    return stmt;
}

/*
 * Execute a query that doesn't return results (INSERT, UPDATE, DELETE, etc)
 * Returns: SQLITE_OK on success, error code on failure
 */
int sql_execute(sqlite3 *db, const char *query) {
    char *err_msg = NULL;
    int rc = sqlite3_exec(db, query, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "    ERROR SQL execution error: %s\n", err_msg);
        sqlite3_free(err_msg);
    }
    return rc;
}

/*
 * Begin transaction
 */
int sql_begin_transaction(sqlite3 *db) {
    return sql_execute(db, "BEGIN TRANSACTION;");
}

/*
 * Commit transaction
 */
int sql_commit(sqlite3 *db) {
    return sql_execute(db, "COMMIT;");
}

/*
 * Rollback transaction
 */
int sql_rollback(sqlite3 *db) {
    return sql_execute(db, "ROLLBACK;");
}

