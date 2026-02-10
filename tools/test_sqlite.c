#include <stdio.h>
#include <stdlib.h>
#include <sqlite3.h>

int main(void) {
    sqlite3 *db = NULL;
    int rc = sqlite3_open(":memory:", &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQLITE_TEST: FAIL - cannot open memory DB (%d)\n", rc);
        return 2;
    }
    const char *create_sql = "CREATE TABLE t(x INTEGER);";
    char *errmsg = NULL;
    rc = sqlite3_exec(db, create_sql, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQLITE_TEST: FAIL - create failed: %s\n", errmsg ? errmsg : "(null)");
        sqlite3_free(errmsg);
        sqlite3_close(db);
        return 3;
    }
    rc = sqlite3_exec(db, "INSERT INTO t(x) VALUES(42);", NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQLITE_TEST: FAIL - insert failed: %s\n", errmsg ? errmsg : "(null)");
        sqlite3_free(errmsg);
        sqlite3_close(db);
        return 4;
    }
    int count = 0;
    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(db, "SELECT x FROM t;", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQLITE_TEST: FAIL - prepare failed (%d)\n", rc);
        sqlite3_close(db);
        return 5;
    }
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    } else {
        fprintf(stderr, "SQLITE_TEST: FAIL - select returned no rows\n");
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return 6;
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    if (count == 42) {
        printf("SQLITE_TEST: PASS\n");
        return 0;
    } else {
        fprintf(stderr, "SQLITE_TEST: FAIL - unexpected value %d\n", count);
        return 7;
    }
}

