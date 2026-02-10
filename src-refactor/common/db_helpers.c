#include "../include/db_helpers.h"

sqlite3 *db_open(const char *path) {
    (void)path;
    return (sqlite3 *)0;
}

void db_close(sqlite3 *db) {
    (void)db;
}

int db_create_schema(sqlite3 *db) {
    (void)db;
    return 0;
}

int db_exec(sqlite3 *db, const char *sql, ...) {
    (void)db;
    (void)sql;
    return 0;
}

sqlite3_stmt *db_query(sqlite3 *db, const char *sql, ...) {
    (void)db;
    (void)sql;
    return (sqlite3_stmt *)0;
}

void db_finalize(sqlite3_stmt *stmt) {
    (void)stmt;
}

