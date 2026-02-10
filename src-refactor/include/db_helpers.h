#ifndef GSP_DB_HELPERS_H
#define GSP_DB_HELPERS_H

#include <sqlite3.h>

sqlite3 *db_open(const char *path);
void db_close(sqlite3 *db);
int db_create_schema(sqlite3 *db);
int db_exec(sqlite3 *db, const char *sql, ...);
sqlite3_stmt *db_query(sqlite3 *db, const char *sql, ...);
void db_finalize(sqlite3_stmt *stmt);

#endif

