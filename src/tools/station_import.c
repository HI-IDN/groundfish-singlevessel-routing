/*
 * Station import using DataSet.
 */

#include "../include/dat_parser.h"
#include "../include/geo_utils.h"
#include "../include/station_import.h"
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void parse_station_comment(const char *raw_comment,
                                  int *depth_thrown,
                                  int *depth_haul,
                                  char **clean_comment) {
    *depth_thrown = 0;
    *depth_haul = 0;
    *clean_comment = NULL;

    if (!raw_comment) return;

    char *comment_copy = strdup(raw_comment);
    if (!comment_copy) return;

    char *kastad_pos = strstr(comment_copy, "botndypi_kastad=");
    if (kastad_pos) *depth_thrown = atoi(kastad_pos + 16);

    char *hift_pos = strstr(comment_copy, "botndypi_hift=");
    if (hift_pos) *depth_haul = atoi(hift_pos + 14);

    char *backslash = strstr(comment_copy, "\\\\");
    if (backslash) {
        *backslash = '\0';
        char *end = backslash - 1;
        while (end >= comment_copy && (*end == ' ' || *end == '\t')) {
            *end = '\0';
            end--;
        }
    }

    char *start = comment_copy;
    if (*start == '#') start++;
    while (*start == ' ' || *start == '\t') start++;
    if (*start != '\0') *clean_comment = strdup(start);

    free(comment_copy);
}

static int ensure_station_schema(sqlite3 *db) {
    const char *sql =
        "CREATE TABLE IF NOT EXISTS locations ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  easting INT,"
        "  northing INT,"
        "  lat REAL,"
        "  lon REAL,"
        "  UNIQUE(easting, northing)"
        ");"
        "CREATE TABLE IF NOT EXISTS stations ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  ext_id INTEGER,"
        "  start_location_id INTEGER,"
        "  end_location_id INTEGER,"
        "  amount INTEGER,"
        "  depth_thrown INTEGER,"
        "  depth_haul INTEGER,"
        "  comment TEXT,"
        "  FOREIGN KEY (start_location_id) REFERENCES locations(id),"
        "  FOREIGN KEY (end_location_id) REFERENCES locations(id)"
        ");"
        "CREATE TABLE IF NOT EXISTS survey ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  boat_id INTEGER NOT NULL,"
        "  table_type INTEGER,"
        "  table_id INTEGER NOT NULL,"
        "  segment INTEGER"
        ");"
        "CREATE TABLE IF NOT EXISTS distances ("
        "  id INTEGER PRIMARY KEY,"
        "  from_location_id INTEGER REFERENCES locations(id),"
        "  to_location_id INTEGER REFERENCES locations(id),"
        "  distance_nm REAL,"
        "  crosses_land INTEGER DEFAULT 0,"
        "  waypoint_path TEXT"
        ");"
        "CREATE TABLE IF NOT EXISTS metadata ("
        "  key TEXT PRIMARY KEY,"
        "  value TEXT"
        ");";
    return sqlite3_exec(db, sql, NULL, NULL, NULL);
}

static int ensure_country_prereqs(sqlite3 *db) {
    sqlite3_stmt *stmt = NULL;
    int coastline_count = 0;

    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM coastline;", -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) coastline_count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }

    if (coastline_count == 0) {
        fprintf(stderr, "Country bootstrap must run first: missing coastline in database\n");
        return SQLITE_ERROR;
    }
    return SQLITE_OK;
}

static int insert_location(sqlite3_stmt *insert_stmt,
                           sqlite3_stmt *select_stmt,
                           int easting,
                           int northing) {
    sqlite3_bind_int(select_stmt, 1, easting);
    sqlite3_bind_int(select_stmt, 2, northing);
    if (sqlite3_step(select_stmt) == SQLITE_ROW) {
        int loc_id = sqlite3_column_int(select_stmt, 0);
        sqlite3_reset(select_stmt);
        sqlite3_clear_bindings(select_stmt);
        return loc_id;
    }
    sqlite3_reset(select_stmt);
    sqlite3_clear_bindings(select_stmt);

    sqlite3_bind_int(insert_stmt, 1, easting);
    sqlite3_bind_int(insert_stmt, 2, northing);
    sqlite3_bind_double(insert_stmt, 3, degmin_to_deg(easting));
    sqlite3_bind_double(insert_stmt, 4, degmin_to_deg_lon(northing));
    if (sqlite3_step(insert_stmt) != SQLITE_DONE) {
        sqlite3_reset(insert_stmt);
        sqlite3_clear_bindings(insert_stmt);
        return -1;
    }
    sqlite3_reset(insert_stmt);
    sqlite3_clear_bindings(insert_stmt);
    return (int)sqlite3_last_insert_rowid(sqlite3_db_handle(insert_stmt));
}

int station_import_run(int argc, char **argv) {
    const char *dat_file = NULL;
    const char *db_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 1;
        } else if (dat_file == NULL) {
            dat_file = argv[i];
        } else if (db_path == NULL) {
            db_path = argv[i];
        }
    }

    if (!dat_file || !db_path) {
        fprintf(stderr, "Usage: %s <stations.dat> <gsp_data.db>\n", argv[0]);
        return 1;
    }

    DataSet dataset;
    dataset_init(&dataset);
    read_dat_file_stations(dat_file, &dataset);

    sqlite3 *db = NULL;
    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        dataset_free(&dataset);
        return 1;
    }

    if (ensure_station_schema(db) != SQLITE_OK || ensure_country_prereqs(db) != SQLITE_OK) {
        sqlite3_close(db);
        dataset_free(&dataset);
        return 1;
    }

    sqlite3_stmt *loc_insert_stmt = NULL;
    sqlite3_stmt *loc_select_stmt = NULL;
    sqlite3_stmt *station_stmt = NULL;

    sqlite3_prepare_v2(db,
        "INSERT INTO locations (easting, northing, lat, lon) VALUES (?, ?, ?, ?);",
        -1, &loc_insert_stmt, NULL);
    sqlite3_prepare_v2(db,
        "SELECT id FROM locations WHERE easting = ? AND northing = ?;",
        -1, &loc_select_stmt, NULL);
    sqlite3_prepare_v2(db,
        "INSERT INTO stations (ext_id, start_location_id, end_location_id, amount, depth_thrown, depth_haul, comment) "
        "VALUES (?, ?, ?, ?, ?, ?, ?);",
        -1, &station_stmt, NULL);

    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
    sqlite3_exec(db, "DELETE FROM distances;", NULL, NULL, NULL);
    sqlite3_exec(db, "DELETE FROM survey;", NULL, NULL, NULL);
    sqlite3_exec(db, "DELETE FROM stations;", NULL, NULL, NULL);

    for (int i = 0; i < dataset.n_stations; i++) {
        const Station *station = &dataset.stations[i];
        const Location *start_loc = &dataset.locations[station->start_location_id];
        const Location *end_loc = &dataset.locations[station->end_location_id];
        int start_loc_id = insert_location(loc_insert_stmt, loc_select_stmt, start_loc->easting, start_loc->northing);
        int end_loc_id = insert_location(loc_insert_stmt, loc_select_stmt, end_loc->easting, end_loc->northing);
        int depth_thrown = 0;
        int depth_haul = 0;
        char *clean_comment = NULL;

        if (start_loc_id <= 0 || end_loc_id <= 0) {
            fprintf(stderr, "Failed to insert station locations\n");
            sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
            sqlite3_close(db);
            dataset_free(&dataset);
            return 1;
        }

        parse_station_comment(station->comment, &depth_thrown, &depth_haul, &clean_comment);
        sqlite3_bind_int(station_stmt, 1, station->external_id);
        sqlite3_bind_int(station_stmt, 2, start_loc_id);
        sqlite3_bind_int(station_stmt, 3, end_loc_id);
        sqlite3_bind_int(station_stmt, 4, station->amount);
        sqlite3_bind_int(station_stmt, 5, depth_thrown);
        sqlite3_bind_int(station_stmt, 6, depth_haul);
        if (clean_comment) sqlite3_bind_text(station_stmt, 7, clean_comment, -1, SQLITE_TRANSIENT);
        else sqlite3_bind_null(station_stmt, 7);

        if (sqlite3_step(station_stmt) != SQLITE_DONE) {
            fprintf(stderr, "Failed to insert station: %s\n", sqlite3_errmsg(db));
            free(clean_comment);
            sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
            sqlite3_close(db);
            dataset_free(&dataset);
            return 1;
        }
        sqlite3_reset(station_stmt);
        sqlite3_clear_bindings(station_stmt);
        free(clean_comment);
    }

    {
        char timestamp[64];
        time_t now = time(NULL);
        sqlite3_stmt *meta_stmt = NULL;
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
        sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO metadata(key, value) VALUES(?, ?);", -1, &meta_stmt, NULL);
        sqlite3_bind_text(meta_stmt, 1, "stations_source_file", -1, SQLITE_STATIC);
        sqlite3_bind_text(meta_stmt, 2, dat_file, -1, SQLITE_TRANSIENT);
        sqlite3_step(meta_stmt);
        sqlite3_reset(meta_stmt);
        sqlite3_clear_bindings(meta_stmt);
        sqlite3_bind_text(meta_stmt, 1, "stations_import_time", -1, SQLITE_STATIC);
        sqlite3_bind_text(meta_stmt, 2, timestamp, -1, SQLITE_TRANSIENT);
        sqlite3_step(meta_stmt);
        sqlite3_finalize(meta_stmt);
    }

    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);

    printf("Imported stations\n");
    printf("  source:   %s\n", dat_file);
    printf("  database: %s\n", db_path);
    printf("  stations: %d\n", dataset.n_stations);
    sqlite3_finalize(loc_insert_stmt);
    sqlite3_finalize(loc_select_stmt);
    sqlite3_finalize(station_stmt);
    sqlite3_close(db);
    dataset_free(&dataset);
    return 0;
}

#ifndef GSP_LIBRARY_ONLY
int main(int argc, char **argv) {
    return station_import_run(argc, argv);
}
#endif
