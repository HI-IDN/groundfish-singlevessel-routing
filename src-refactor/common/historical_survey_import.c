/*
 * Historical survey importer.
 *
 * Reads an ordered survey DAT file and rebuilds only the survey table by
 * resolving existing boat/station/port rows already present in the database.
 */

#include "../include/constants.h"
#include "../include/dat_parser.h"
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    int type;
    int easting;
    int northing;
    int selected;
} SurveyEntry;

typedef struct {
    SurveyEntry *entries;
    int n;
    int cap;
} SurveyEntryVec;

static void survey_entry_vec_init(SurveyEntryVec *vec) {
    vec->n = 0;
    vec->cap = 128;
    vec->entries = (SurveyEntry*)calloc((size_t)vec->cap, sizeof(SurveyEntry));
}

static void survey_entry_vec_free(SurveyEntryVec *vec) {
    free(vec->entries);
    vec->entries = NULL;
    vec->n = 0;
    vec->cap = 0;
}

static int survey_entry_vec_push(SurveyEntryVec *vec, SurveyEntry entry) {
    if (vec->n == vec->cap) {
        int new_cap = vec->cap * 2;
        SurveyEntry *tmp = (SurveyEntry*)realloc(vec->entries, (size_t)new_cap * sizeof(SurveyEntry));
        if (!tmp) return 0;
        vec->entries = tmp;
        vec->cap = new_cap;
    }
    vec->entries[vec->n++] = entry;
    return 1;
}

static int parse_survey_file(const char *dat_file, SurveyEntryVec *entries) {
    FILE *fp = fopen(dat_file, "rb");
    if (!fp) {
        perror("fopen");
        return 0;
    }

    char line[4096];
    while (fgets(line, sizeof(line), fp)) {
        char **tok = NULL;
        int nt = tokenize_line(line, &tok);
        if (nt <= 0) {
            free_tokens(tok, nt);
            continue;
        }

        SurveyEntry entry;
        memset(&entry, 0, sizeof(entry));

        if (strcmp(tok[0], GSP_DAT_TAG_BOAT) == 0 && nt >= 13) {
            entry.type = NODE_TYPE_BOAT;
            entry.easting = atoi(tok[1]);
            entry.northing = atoi(tok[2]);
        } else if (strcmp(tok[0], GSP_DAT_TAG_STAT) == 0 && nt >= 10) {
            if (atoi(tok[3]) == 5) {
                free_tokens(tok, nt);
                continue;
            }
            entry.type = NODE_TYPE_STATION;
            entry.easting = atoi(tok[4]);
            entry.northing = atoi(tok[5]);
        } else if (strcmp(tok[0], GSP_DAT_TAG_PORT) == 0 && nt >= 5) {
            entry.type = NODE_TYPE_PORT;
            entry.easting = atoi(tok[1]);
            entry.northing = atoi(tok[2]);
            entry.selected = atoi(tok[4]) != 0;
        } else {
            free_tokens(tok, nt);
            continue;
        }

        if (!survey_entry_vec_push(entries, entry)) {
            free_tokens(tok, nt);
            fclose(fp);
            return 0;
        }
        free_tokens(tok, nt);
    }

    fclose(fp);
    return 1;
}

static int lookup_boat_id(sqlite3 *db, int easting, int northing) {
    sqlite3_stmt *stmt = NULL;
    int boat_id = 0;
    const char *sql =
        "SELECT b.id FROM boats b "
        "JOIN locations l ON b.start_location_id = l.id "
        "WHERE l.easting = ? AND l.northing = ?;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, easting);
    sqlite3_bind_int(stmt, 2, northing);
    if (sqlite3_step(stmt) == SQLITE_ROW) boat_id = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return boat_id;
}

static int lookup_station_id(sqlite3 *db, int easting, int northing) {
    sqlite3_stmt *stmt = NULL;
    int station_id = 0;
    const char *sql =
        "SELECT s.id FROM stations s "
        "JOIN locations l ON s.start_location_id = l.id "
        "WHERE l.easting = ? AND l.northing = ?;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, easting);
    sqlite3_bind_int(stmt, 2, northing);
    if (sqlite3_step(stmt) == SQLITE_ROW) station_id = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return station_id;
}

static int lookup_port_id(sqlite3 *db, int easting, int northing) {
    sqlite3_stmt *stmt = NULL;
    int port_id = 0;
    const char *sql =
        "SELECT p.id FROM ports p "
        "JOIN locations l ON p.location_id = l.id "
        "WHERE l.easting = ? AND l.northing = ?;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, easting);
    sqlite3_bind_int(stmt, 2, northing);
    if (sqlite3_step(stmt) == SQLITE_ROW) port_id = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return port_id;
}

static int write_metadata(sqlite3 *db, const char *dat_file) {
    sqlite3_stmt *stmt = NULL;
    char timestamp[64];
    time_t now = time(NULL);

    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));

    if (sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO metadata(key, value) VALUES(?, ?);", -1, &stmt, NULL) != SQLITE_OK) {
        return SQLITE_ERROR;
    }

    sqlite3_bind_text(stmt, 1, "historical_survey_source_file", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, dat_file, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);

    sqlite3_bind_text(stmt, 1, "historical_survey_import_time", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, timestamp, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return SQLITE_OK;
}

static int import_historical_survey(sqlite3 *db, const SurveyEntryVec *entries) {
    sqlite3_stmt *insert_stmt = NULL;
    int current_boat_id = 0;
    int segment = 0;
    int survey_count = 0;

    if (sqlite3_prepare_v2(db,
                           "INSERT INTO survey (boat_id, table_type, table_id, segment) VALUES (?, ?, ?, ?);",
                           -1, &insert_stmt, NULL) != SQLITE_OK) {
        return SQLITE_ERROR;
    }

    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
    sqlite3_exec(db, "DELETE FROM survey;", NULL, NULL, NULL);

    for (int i = 0; i < entries->n; i++) {
        const SurveyEntry *entry = &entries->entries[i];

        if (entry->type == NODE_TYPE_BOAT) {
            current_boat_id = lookup_boat_id(db, entry->easting, entry->northing);
            if (current_boat_id <= 0) {
                fprintf(stderr, "Could not resolve boat at %d %d\n", entry->easting, entry->northing);
                sqlite3_finalize(insert_stmt);
                sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
                return SQLITE_ERROR;
            }

            segment = 1;
            sqlite3_bind_int(insert_stmt, 1, current_boat_id);
            sqlite3_bind_int(insert_stmt, 2, NODE_TYPE_BOAT);
            sqlite3_bind_int(insert_stmt, 3, current_boat_id);
            sqlite3_bind_int(insert_stmt, 4, segment);
            sqlite3_step(insert_stmt);
            sqlite3_reset(insert_stmt);
            sqlite3_clear_bindings(insert_stmt);
            survey_count++;
        } else if (entry->type == NODE_TYPE_STATION && current_boat_id > 0) {
            int station_id = lookup_station_id(db, entry->easting, entry->northing);
            if (station_id <= 0) {
                fprintf(stderr, "Could not resolve station at %d %d\n", entry->easting, entry->northing);
                sqlite3_finalize(insert_stmt);
                sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
                return SQLITE_ERROR;
            }

            sqlite3_bind_int(insert_stmt, 1, current_boat_id);
            sqlite3_bind_int(insert_stmt, 2, NODE_TYPE_STATION);
            sqlite3_bind_int(insert_stmt, 3, station_id);
            sqlite3_bind_int(insert_stmt, 4, segment);
            sqlite3_step(insert_stmt);
            sqlite3_reset(insert_stmt);
            sqlite3_clear_bindings(insert_stmt);
            survey_count++;
        } else if (entry->type == NODE_TYPE_PORT && current_boat_id > 0 && entry->selected) {
            int port_id = lookup_port_id(db, entry->easting, entry->northing);
            if (port_id <= 0) {
                fprintf(stderr, "Could not resolve port at %d %d\n", entry->easting, entry->northing);
                sqlite3_finalize(insert_stmt);
                sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
                return SQLITE_ERROR;
            }

            sqlite3_bind_int(insert_stmt, 1, current_boat_id);
            sqlite3_bind_int(insert_stmt, 2, NODE_TYPE_PORT);
            sqlite3_bind_int(insert_stmt, 3, port_id);
            sqlite3_bind_int(insert_stmt, 4, segment);
            sqlite3_step(insert_stmt);
            sqlite3_reset(insert_stmt);
            sqlite3_clear_bindings(insert_stmt);
            survey_count++;

            segment++;
            sqlite3_bind_int(insert_stmt, 1, current_boat_id);
            sqlite3_bind_int(insert_stmt, 2, NODE_TYPE_PORT);
            sqlite3_bind_int(insert_stmt, 3, port_id);
            sqlite3_bind_int(insert_stmt, 4, segment);
            sqlite3_step(insert_stmt);
            sqlite3_reset(insert_stmt);
            sqlite3_clear_bindings(insert_stmt);
            survey_count++;
        }
    }

    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
    sqlite3_finalize(insert_stmt);
    printf("Imported historical survey assignments: %d rows\n", survey_count);
    return SQLITE_OK;
}

int main(int argc, char **argv) {
    const char *dat_file = NULL;
    const char *db_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 1;
        } else if (!dat_file) {
            dat_file = argv[i];
        } else if (!db_path) {
            db_path = argv[i];
        }
    }

    if (!dat_file || !db_path) {
        fprintf(stderr, "Usage: %s <survey2023spring.dat> <gsp_data.db>\n", argv[0]);
        return 1;
    }

    SurveyEntryVec entries;
    survey_entry_vec_init(&entries);
    if (!parse_survey_file(dat_file, &entries)) {
        survey_entry_vec_free(&entries);
        return 1;
    }

    sqlite3 *db = NULL;
    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        survey_entry_vec_free(&entries);
        return 1;
    }

    if (import_historical_survey(db, &entries) != SQLITE_OK) {
        sqlite3_close(db);
        survey_entry_vec_free(&entries);
        return 1;
    }

    write_metadata(db, dat_file);
    printf("  source:   %s\n", dat_file);
    printf("  database: %s\n", db_path);

    sqlite3_close(db);
    survey_entry_vec_free(&entries);
    return 0;
}
