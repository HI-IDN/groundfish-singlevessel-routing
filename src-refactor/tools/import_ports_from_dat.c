#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sqlite3.h>

#include "../include/dat_parser.h"
#include "../include/geo_utils.h"

static void usage(const char *argv0) {
    fprintf(stderr,
            "Usage: %s --db <gsp_data.db> --dat <datafile.dat>\n"
            "\n"
            "Imports PORT rows from DAT into locations + ports tables.\n",
            argv0);
}

static int ensure_schema(sqlite3 *db) {
    const char *sql =
        "CREATE TABLE IF NOT EXISTS locations ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  easting INT,"
        "  northing INT,"
        "  lat REAL,"
        "  lon REAL,"
        "  UNIQUE(easting, northing)"
        ");"
        "CREATE TABLE IF NOT EXISTS ports ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT,"
        "  location_id INTEGER,"
        "  UNIQUE(location_id),"
        "  FOREIGN KEY (location_id) REFERENCES locations(id)"
        ");";
    return sqlite3_exec(db, sql, NULL, NULL, NULL);
}

static char *strip_quotes_local(const char *name) {
    if (!name) {
        char *empty = (char*)malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }
    const char *start = name;
    const char *end = name + strlen(name);
    if (*start == '"') start++;
    if (end > start && *(end - 1) == '"') end--;
    size_t len = (size_t)(end - start);
    char *out = (char*)malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

static int insert_location_from_degmin(sqlite3_stmt *insert_stmt,
                                       sqlite3_stmt *select_stmt,
                                       int lat_degmin,
                                       int lon_degmin) {
    double lat_deg = degmin_to_deg(lat_degmin);
    double lon_deg = degmin_to_deg_lon(lon_degmin);

    sqlite3_bind_int(select_stmt, 1, lat_degmin);
    sqlite3_bind_int(select_stmt, 2, lon_degmin);
    if (sqlite3_step(select_stmt) == SQLITE_ROW) {
        int loc_id = sqlite3_column_int(select_stmt, 0);
        sqlite3_reset(select_stmt);
        sqlite3_clear_bindings(select_stmt);
        return loc_id;
    }
    sqlite3_reset(select_stmt);
    sqlite3_clear_bindings(select_stmt);

    sqlite3_bind_int(insert_stmt, 1, lat_degmin);
    sqlite3_bind_int(insert_stmt, 2, lon_degmin);
    sqlite3_bind_double(insert_stmt, 3, lat_deg);
    sqlite3_bind_double(insert_stmt, 4, lon_deg);
    if (sqlite3_step(insert_stmt) != SQLITE_DONE) {
        sqlite3_reset(insert_stmt);
        sqlite3_clear_bindings(insert_stmt);
        return -1;
    }
    sqlite3_reset(insert_stmt);
    sqlite3_clear_bindings(insert_stmt);
    return (int)sqlite3_last_insert_rowid(sqlite3_db_handle(insert_stmt));
}

int main(int argc, char **argv) {
    const char *db_path = NULL;
    const char *dat_path = NULL;
    sqlite3 *db = NULL;
    sqlite3_stmt *loc_insert_stmt = NULL;
    sqlite3_stmt *loc_select_stmt = NULL;
    sqlite3_stmt *port_stmt = NULL;
    ItemVec items = {0};
    int items_loaded = 0;
    int ports_seen = 0;
    int ports_inserted = 0;
    int rc = 1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--db") == 0 && i + 1 < argc) {
            db_path = argv[++i];
        } else if (strcmp(argv[i], "--dat") == 0 && i + 1 < argc) {
            dat_path = argv[++i];
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    if (!db_path || !dat_path) {
        usage(argv[0]);
        return 1;
    }

    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }

    if (ensure_schema(db) != SQLITE_OK) {
        fprintf(stderr, "Failed to ensure schema: %s\n", sqlite3_errmsg(db));
        goto cleanup;
    }

    item_vec_init(&items);
    items_loaded = 1;
    read_dat_file_all_boats(dat_path, &items, 0);

    if (sqlite3_prepare_v2(db,
                           "INSERT INTO locations (easting, northing, lat, lon) VALUES (?, ?, ?, ?);",
                           -1, &loc_insert_stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare location insert: %s\n", sqlite3_errmsg(db));
        goto cleanup;
    }

    if (sqlite3_prepare_v2(db,
                           "SELECT id FROM locations WHERE easting = ? AND northing = ?;",
                           -1, &loc_select_stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare location select: %s\n", sqlite3_errmsg(db));
        goto cleanup;
    }

    if (sqlite3_prepare_v2(db,
                           "INSERT OR IGNORE INTO ports (name, location_id) VALUES (?, ?);",
                           -1, &port_stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare port insert: %s\n", sqlite3_errmsg(db));
        goto cleanup;
    }

    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);

    for (int i = 0; i < items.n; i++) {
        if (items.a[i].Type != tPORT) continue;
        ports_seen++;

        int lat_degmin = (int)items.a[i].LatLonDegMin[0];
        int lon_degmin = (int)items.a[i].LatLonDegMin[1];
        int loc_id = insert_location_from_degmin(loc_insert_stmt, loc_select_stmt, lat_degmin, lon_degmin);
        if (loc_id <= 0) {
            fprintf(stderr, "Failed to upsert location for PORT row\n");
            sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
            goto cleanup;
        }

        char *clean_name = strip_quotes_local(items.a[i].Name);
        if (!clean_name) {
            fprintf(stderr, "Out of memory while processing port name\n");
            sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
            goto cleanup;
        }

        sqlite3_bind_text(port_stmt, 1, clean_name, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(port_stmt, 2, loc_id);
        if (sqlite3_step(port_stmt) != SQLITE_DONE) {
            free(clean_name);
            sqlite3_reset(port_stmt);
            sqlite3_clear_bindings(port_stmt);
            fprintf(stderr, "Failed to insert port: %s\n", sqlite3_errmsg(db));
            sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
            goto cleanup;
        }
        if (sqlite3_changes(db) > 0) ports_inserted++;
        sqlite3_reset(port_stmt);
        sqlite3_clear_bindings(port_stmt);
        free(clean_name);
    }

    if (ports_seen == 0) {
        fprintf(stderr, "No PORT rows were parsed from %s\n", dat_path);
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        goto cleanup;
    }

    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);

    printf("Imported ports from DAT\n");
    printf("  seen=%d\n", ports_seen);
    printf("  inserted=%d\n", ports_inserted);
    rc = 0;

cleanup:
    sqlite3_finalize(loc_insert_stmt);
    sqlite3_finalize(loc_select_stmt);
    sqlite3_finalize(port_stmt);
    if (db) sqlite3_close(db);
    if (items_loaded) item_vec_free(&items);
    return rc;
}


