/*
 * Export survey data to JSON format compatible with solution plotting
 * Usage: historical_survey <database.db> <output_prefix> [boat_id]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <time.h>
#include <locale.h>
#include <math.h>
#include <wchar.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <sys/types.h>
#include <ctype.h>
#endif

#include "../include/constants.h"
#include "../include/dat_parser.h"
#include "../include/feasibility.h"
#include "../include/init_types.h"
#include "../include/json_utils.h"


static void die(const char *msg) {
    fprintf(stderr, "Error: %s\n", msg);
    exit(1);
}

/* ---- Types for multivessel output ---- */

typedef struct {
    int num_stations;
    int segment_count;
    double total_transit_nm;
    double total_distance_nm;
    int *segment_catch;        /* owned, [segment_count] */
    int *segment_end_dock_ids; /* owned, [segment_count] - end dock location per segment */
    char *boat_name;           /* owned - boat name from database */
    int boat_start_loc_id;     /* home dock location id */
    int boat_id;               /* boats.id from database */
} boat_stats_t;

static void free_boat_stats(boat_stats_t *s) {
    if (!s) return;
    free(s->segment_catch);
    free(s->segment_end_dock_ids);
    free(s->boat_name);
    memset(s, 0, sizeof(*s));
}

typedef struct { int location_id; int port_id; char *name; int visit_count; } port_info_mv_t;

static void free_port_info_mv(port_info_mv_t *ports, int count) {
    if (!ports) return;
    for (int i = 0; i < count; i++) free(ports[i].name);
    free(ports);
}

static char *dup_mv(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static int read_fixedport_min_catch_kg(const char *yaml_path) {
    FILE *fp;
    char line[1024];
    int in_init = 0, threshold = 1000;
    const char *key = "fixedport_min_segment_catch_kg:";
    size_t key_len = strlen(key);
    if (!yaml_path || !(fp = fopen(yaml_path, "r"))) return threshold;
    while (fgets(line, sizeof(line), fp)) {
        char *t = line;
        while (*t && isspace((unsigned char)*t)) t++;
        if (*t == '#' || !*t || *t == '\n') continue;
        if (!isspace((unsigned char)line[0])) { in_init = (strncmp(t, "init:", 5) == 0); continue; }
        if (!in_init) continue;
        if (strncmp(t, key, key_len) == 0) {
            threshold = atoi(t + key_len);
            if (threshold < 0) threshold = 0;
            break;
        }
    }
    fclose(fp);
    return threshold;
}

static int load_port_lookup_mv(sqlite3 *db, port_info_mv_t **out, int *out_count) {
    sqlite3_stmt *stmt = NULL;
    port_info_mv_t *ports = NULL;
    int count = 0, cap = 0;
    if (out) *out = NULL;
    if (out_count) *out_count = 0;
    if (!db) return 0;
    if (sqlite3_prepare_v2(db,
            "SELECT p.location_id, p.id, p.name FROM ports p ORDER BY p.location_id;",
            -1, &stmt, NULL) != SQLITE_OK) return 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (count >= cap) {
            int nc = (cap > 0) ? (cap * 2) : 16;
            port_info_mv_t *tmp = (port_info_mv_t *)realloc(ports, (size_t)nc * sizeof(*ports));
            if (!tmp) { sqlite3_finalize(stmt); free_port_info_mv(ports, count); return 0; }
            ports = tmp; cap = nc;
        }
        ports[count].location_id = sqlite3_column_int(stmt, 0);
        ports[count].port_id     = sqlite3_column_int(stmt, 1);
        ports[count].name        = dup_mv((const char *)sqlite3_column_text(stmt, 2));
        ports[count].visit_count = 0;
        count++;
    }
    sqlite3_finalize(stmt);
    if (out) *out = ports; else free_port_info_mv(ports, count);
    if (out_count) *out_count = count;
    return 1;
}

static char *dup_cstr(const char *s) {
    size_t n = strlen(s) + 1;
    char *out = (char*)malloc(n);
    if (!out) return NULL;
    memcpy(out, s, n);
    return out;
}

static char *dup_unquoted_cstr(const char *s) {
    size_t len;
    const char *start = s;
    if (!s) return NULL;
    len = strlen(s);
    if (len >= 2 && s[0] == '"' && s[len - 1] == '"') {
        start = s + 1;
        len -= 2;
    }
    char *out = (char*)malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

typedef struct {
    int type;
    int easting;
    int northing;
    char *name;
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
    for (int i = 0; i < vec->n; i++) {
        free(vec->entries[i].name);
    }
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

        if (strcmp(tok[0], GSP_DAT_TAG_BOAT) == 0 && nt >= 11) {
            entry.type = NODE_TYPE_BOAT;
            entry.easting = atoi(tok[1]);
            entry.northing = atoi(tok[2]);
            entry.name = dup_unquoted_cstr(tok[10]);
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

static int lookup_boat_id(sqlite3 *db, const char *name) {
    sqlite3_stmt *stmt = NULL;
    int boat_id = 0;
    const char *sql = "SELECT id FROM boats WHERE name = ?;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
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

static int append_int(int **arr, int *n, int *cap, int v) {
    if (!arr || !n || !cap) return 0;
    if (*n >= *cap) {
        int new_cap = (*cap == 0) ? 16 : (*cap * 2);
        int *tmp = (int*)realloc(*arr, (size_t)new_cap * sizeof(int));
        if (!tmp) return 0;
        *arr = tmp;
        *cap = new_cap;
    }
    (*arr)[(*n)++] = v;
    return 1;
}

static int append_unique_int(int **arr, int *n, int *cap, int v) {
    if (!arr || !n || !cap) return 0;
    for (int i = 0; i < *n; i++) {
        if ((*arr)[i] == v) return 1;
    }
    return append_int(arr, n, cap, v);
}

/* Resolve segment start/end boundary location using nearest BOAT/PORT around the segment. */
static int resolve_segment_boundary_loc(
    int is_start,
    int seg_idx,
    int num_nodes,
    const int *types,
    const int *resolved_loc_ids,
    int boat_start_loc_id,
    int boat_end_loc_id) {

    int i;
    if (is_start) {
        for (i = seg_idx; i >= 0; i--) {
            if (types[i] == NODE_TYPE_PORT && resolved_loc_ids[i] > 0) return resolved_loc_ids[i];
            if (types[i] == NODE_TYPE_BOAT) return boat_start_loc_id;
        }
        for (i = seg_idx; i < num_nodes; i++) {
            if (types[i] == NODE_TYPE_PORT && resolved_loc_ids[i] > 0) return resolved_loc_ids[i];
            if (types[i] == NODE_TYPE_BOAT) return boat_start_loc_id;
        }
        return boat_start_loc_id;
    }

    for (i = seg_idx; i < num_nodes; i++) {
        if (types[i] == NODE_TYPE_PORT && resolved_loc_ids[i] > 0) return resolved_loc_ids[i];
        if (types[i] == NODE_TYPE_BOAT) return boat_end_loc_id;
    }
    for (i = seg_idx; i >= 0; i--) {
        if (types[i] == NODE_TYPE_PORT && resolved_loc_ids[i] > 0) return resolved_loc_ids[i];
        if (types[i] == NODE_TYPE_BOAT) return boat_end_loc_id;
    }
    return boat_end_loc_id;
}

/* Resolve boundary node type using the same neighborhood search policy as location resolution. */
static int resolve_segment_boundary_type(
    int is_start,
    int seg_idx,
    int num_nodes,
    const int *types,
    const int *resolved_loc_ids) {

    int i;
    if (is_start) {
        for (i = seg_idx; i >= 0; i--) {
            if (types[i] == NODE_TYPE_PORT && resolved_loc_ids[i] > 0) return NODE_TYPE_PORT;
            if (types[i] == NODE_TYPE_BOAT) return NODE_TYPE_BOAT;
        }
        for (i = seg_idx; i < num_nodes; i++) {
            if (types[i] == NODE_TYPE_PORT && resolved_loc_ids[i] > 0) return NODE_TYPE_PORT;
            if (types[i] == NODE_TYPE_BOAT) return NODE_TYPE_BOAT;
        }
        return -1;
    }

    for (i = seg_idx; i < num_nodes; i++) {
        if (types[i] == NODE_TYPE_PORT && resolved_loc_ids[i] > 0) return NODE_TYPE_PORT;
        if (types[i] == NODE_TYPE_BOAT) return NODE_TYPE_BOAT;
    }
    for (i = seg_idx; i >= 0; i--) {
        if (types[i] == NODE_TYPE_PORT && resolved_loc_ids[i] > 0) return NODE_TYPE_PORT;
        if (types[i] == NODE_TYPE_BOAT) return NODE_TYPE_BOAT;
    }
    return -1;
}

/* Return 1 if [a,b] contains at least one station row. */
static int segment_has_station(const int *types, int a, int b) {
    if (!types || a > b) return 0;
    for (int i = a; i <= b; i++) {
        if (types[i] == NODE_TYPE_STATION) return 1;
    }
    return 0;
}

/* Lookup distance for a location pair; fallback to reverse direction if needed. */
static double lookup_distance_nm(sqlite3 *db, int from_loc_id, int to_loc_id) {
    static const char *dist_sql =
        "SELECT distance_nm FROM distances WHERE from_location_id = ? AND to_location_id = ?;";
    sqlite3_stmt *stmt = NULL;
    double d = -1.0;

    if (from_loc_id <= 0 || to_loc_id <= 0) return -1.0;

    if (sqlite3_prepare_v2(db, dist_sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, from_loc_id);
        sqlite3_bind_int(stmt, 2, to_loc_id);
        if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
            d = sqlite3_column_double(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    if (d >= 0.0) return d;

    if (sqlite3_prepare_v2(db, dist_sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, to_loc_id);
        sqlite3_bind_int(stmt, 2, from_loc_id);
        if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
            d = sqlite3_column_double(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    return d;
}

/* Cached variant: reuses a pre-prepared statement — avoids prepare/finalize overhead in hot loops. */
static double lookup_distance_nm_cached(sqlite3_stmt *stmt, int from_loc_id, int to_loc_id) {
    double d = -1.0;
    if (!stmt || from_loc_id <= 0 || to_loc_id <= 0) return -1.0;

    sqlite3_reset(stmt);
    sqlite3_bind_int(stmt, 1, from_loc_id);
    sqlite3_bind_int(stmt, 2, to_loc_id);
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL)
        d = sqlite3_column_double(stmt, 0);
    if (d >= 0.0) return d;

    /* Fallback: reverse direction */
    sqlite3_reset(stmt);
    sqlite3_bind_int(stmt, 1, to_loc_id);
    sqlite3_bind_int(stmt, 2, from_loc_id);
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL)
        d = sqlite3_column_double(stmt, 0);
    return d;
}

static void accumulate_distance_breakdown(gsp_distance_breakdown_t *breakdown,
                                          double distance_nm,
                                          int is_haul) {
    if (!breakdown || distance_nm <= 0.0) return;
    if (is_haul) {
        breakdown->haul_distance_nm += distance_nm;
    } else {
        breakdown->transit_distance_nm += distance_nm;
    }
    breakdown->total_distance_nm += distance_nm;
}

/* Parse JSON waypoint_path like [1200,1199] into int array. */
static int parse_waypoint_path_json(const char *json_text, int **out_ids) {
    int *ids = NULL;
    int count = 0;
    const char *p;

    if (out_ids) *out_ids = NULL;
    if (!json_text || !out_ids) return 0;

    p = json_text;
    while (*p) {
        char *endptr;
        long val;

        while (*p && !((*p >= '0' && *p <= '9') || *p == '-')) p++;
        if (!*p) break;

        val = strtol(p, &endptr, 10);
        if (endptr == p) break;

        {
            int *tmp = (int*)realloc(ids, (size_t)(count + 1) * sizeof(int));
            if (!tmp) {
                free(ids);
                *out_ids = NULL;
                return 0;
            }
            ids = tmp;
            ids[count++] = (int)val;
        }
        p = endptr;
    }

    *out_ids = ids;
    return count;
}

/* Lookup waypoint_path for from->to; fallback to reverse row if needed. */
static int lookup_waypoint_path(sqlite3 *db, int from_loc_id, int to_loc_id, int **out_ids) {
    static const char *sql =
        "SELECT waypoint_path FROM distances WHERE from_location_id = ? AND to_location_id = ?;";
    sqlite3_stmt *stmt = NULL;
    int count = 0;

    if (out_ids) *out_ids = NULL;
    if (!db || !out_ids || from_loc_id <= 0 || to_loc_id <= 0) return 0;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, from_loc_id);
        sqlite3_bind_int(stmt, 2, to_loc_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *txt = sqlite3_column_text(stmt, 0);
            if (txt) count = parse_waypoint_path_json((const char*)txt, out_ids);
            sqlite3_finalize(stmt);
            return count;
        }
        sqlite3_finalize(stmt);
    }

    /* Fallback: reverse direction (reverse parsed waypoint order). */
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, to_loc_id);
        sqlite3_bind_int(stmt, 2, from_loc_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *txt = sqlite3_column_text(stmt, 0);
            if (txt) {
                count = parse_waypoint_path_json((const char*)txt, out_ids);
                for (int i = 0; i < count / 2; i++) {
                    int tmp = (*out_ids)[i];
                    (*out_ids)[i] = (*out_ids)[count - 1 - i];
                    (*out_ids)[count - 1 - i] = tmp;
                }
            }
        }
        sqlite3_finalize(stmt);
    }

    return count;
}

/* Cached variant: reuses a pre-prepared statement — avoids prepare/finalize overhead in hot loops. */
static int lookup_waypoint_path_cached(sqlite3_stmt *stmt, int from_loc_id, int to_loc_id, int **out_ids) {
    int count = 0;
    if (out_ids) *out_ids = NULL;
    if (!stmt || !out_ids || from_loc_id <= 0 || to_loc_id <= 0) return 0;

    sqlite3_reset(stmt);
    sqlite3_bind_int(stmt, 1, from_loc_id);
    sqlite3_bind_int(stmt, 2, to_loc_id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *txt = sqlite3_column_text(stmt, 0);
        if (txt) count = parse_waypoint_path_json((const char*)txt, out_ids);
        return count;
    }

    /* Fallback: reverse direction */
    sqlite3_reset(stmt);
    sqlite3_bind_int(stmt, 1, to_loc_id);
    sqlite3_bind_int(stmt, 2, from_loc_id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *txt = sqlite3_column_text(stmt, 0);
        if (txt) {
            count = parse_waypoint_path_json((const char*)txt, out_ids);
            for (int i = 0; i < count / 2; i++) {
                int tmp = (*out_ids)[i];
                (*out_ids)[i] = (*out_ids)[count - 1 - i];
                (*out_ids)[count - 1 - i] = tmp;
            }
        }
    }
    return count;
}

/* Export a single boat's route to JSON */
static int export_boat_json_fp(sqlite3 *db, const SurveyEntryVec *entries, int boat_id, FILE *out, boat_stats_t *stats_out) {
    printf("Building survey route for boat_id=%d\n", boat_id);

    /* Query boat info including start/end location IDs for proximity checks. */
    const char *boat_sql =
        "SELECT b.name, b.capacity, b.location_id, l1.lat, l1.lon "
        "FROM boats b "
        "JOIN locations l1 ON b.location_id = l1.id "
        "WHERE b.id = ?;";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, boat_sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    sqlite3_bind_int(stmt, 1, boat_id);

    char boat_name[256] = "Unknown";
    int capacity = 0;
    int boat_start_loc_id = 0;
    int boat_end_loc_id = 0;
    double dock_lat = 0.0, dock_lon = 0.0;

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *name = (const char*)sqlite3_column_text(stmt, 0);
        if (name) {
            strncpy(boat_name, name, sizeof(boat_name) - 1);
            boat_name[sizeof(boat_name) - 1] = '\0';
        }
        capacity = sqlite3_column_int(stmt, 1);
        boat_start_loc_id = sqlite3_column_int(stmt, 2);
        boat_end_loc_id = boat_start_loc_id;
        dock_lat = sqlite3_column_double(stmt, 3);
        dock_lon = sqlite3_column_double(stmt, 4);
    } else {
        fprintf(stderr, "Boat %d not found in database\n", boat_id);
        sqlite3_finalize(stmt);
        return 1;
    }
    sqlite3_finalize(stmt);

    int num_nodes = 0;
    int current_boat_id = 0;
    for (int i = 0; i < entries->n; i++) {
        const SurveyEntry *entry = &entries->entries[i];
        if (entry->type == NODE_TYPE_BOAT) {
            current_boat_id = lookup_boat_id(db, entry->name);
            if (current_boat_id <= 0) {
                fprintf(stderr, "Could not resolve boat named '%s'\n", entry->name ? entry->name : "(null)");
                return 1;
            }
        }
        if (current_boat_id == boat_id && (entry->type == NODE_TYPE_BOAT || entry->type == NODE_TYPE_STATION ||
            (entry->type == NODE_TYPE_PORT && entry->selected))) {
            num_nodes += (entry->type == NODE_TYPE_PORT && entry->selected) ? 2 : 1;
        }
    }

    if (num_nodes == 0) {
        fprintf(stderr, "No survey data found for boat_id=%d\n", boat_id);
        return 1;
    }

    int *types = (int*)malloc((size_t)num_nodes * sizeof(int));
    int *table_ids = (int*)malloc((size_t)num_nodes * sizeof(int));  /* boats.id, stations.id, or ports.id */
    int *segments = (int*)malloc((size_t)num_nodes * sizeof(int));
    int *resolved_loc_ids = (int*)malloc((size_t)num_nodes * sizeof(int));
    int *station_end_loc_ids = (int*)malloc((size_t)num_nodes * sizeof(int));
    int *catch_amounts = (int*)malloc((size_t)num_nodes * sizeof(int));

    int idx = 0;
    current_boat_id = 0;
    int segment = 0;

    /* Prepare statements once for the node-building loop to avoid per-iteration prepare overhead. */
    sqlite3_stmt *stmt_station = NULL, *stmt_port_loc = NULL;
    sqlite3_prepare_v2(db,
        "SELECT s.id, s.start_location_id, s.end_location_id, s.amount "
        "FROM stations s JOIN locations l ON s.start_location_id = l.id "
        "WHERE l.easting = ? AND l.northing = ?;",
        -1, &stmt_station, NULL);
    sqlite3_prepare_v2(db, "SELECT location_id FROM ports WHERE id = ?;",
        -1, &stmt_port_loc, NULL);

    for (int i = 0; i < entries->n; i++) {
        const SurveyEntry *entry = &entries->entries[i];
        if (entry->type == NODE_TYPE_BOAT) {
            current_boat_id = lookup_boat_id(db, entry->name);
            if (current_boat_id <= 0) {
                fprintf(stderr, "Could not resolve boat named '%s'\n", entry->name ? entry->name : "(null)");
                sqlite3_finalize(stmt_station); sqlite3_finalize(stmt_port_loc);
                free(types); free(table_ids); free(segments); free(resolved_loc_ids); free(station_end_loc_ids); free(catch_amounts);
                return 1;
            }
            segment = 1;
            if (current_boat_id == boat_id) {
                types[idx] = NODE_TYPE_BOAT;
                table_ids[idx] = boat_id;
                segments[idx] = segment;
                resolved_loc_ids[idx] = boat_start_loc_id;
                station_end_loc_ids[idx] = 0;
                catch_amounts[idx] = 0;
                idx++;
            }
            continue;
        }

        if (current_boat_id != boat_id) continue;

        if (entry->type == NODE_TYPE_STATION) {
            if (!stmt_station) {
                fprintf(stderr, "SQL error: station statement not prepared\n");
                sqlite3_finalize(stmt_port_loc);
                free(types); free(table_ids); free(segments); free(resolved_loc_ids); free(station_end_loc_ids); free(catch_amounts);
                return 1;
            }
            sqlite3_reset(stmt_station);
            sqlite3_bind_int(stmt_station, 1, entry->easting);
            sqlite3_bind_int(stmt_station, 2, entry->northing);
            if (sqlite3_step(stmt_station) != SQLITE_ROW) {
                fprintf(stderr, "Could not resolve station at %d %d\n", entry->easting, entry->northing);
                sqlite3_finalize(stmt_station); sqlite3_finalize(stmt_port_loc);
                free(types); free(table_ids); free(segments); free(resolved_loc_ids); free(station_end_loc_ids); free(catch_amounts);
                return 1;
            }
            types[idx] = NODE_TYPE_STATION;
            table_ids[idx] = sqlite3_column_int(stmt_station, 0);
            segments[idx] = segment;
            resolved_loc_ids[idx] = sqlite3_column_int(stmt_station, 1);
            station_end_loc_ids[idx] = sqlite3_column_int(stmt_station, 2);
            catch_amounts[idx] = sqlite3_column_int(stmt_station, 3);
            idx++;
        } else if (entry->type == NODE_TYPE_PORT && entry->selected) {
            int port_id = lookup_port_id(db, entry->easting, entry->northing);
            if (port_id <= 0 || !stmt_port_loc) {
                fprintf(stderr, "Could not resolve port at %d %d\n", entry->easting, entry->northing);
                sqlite3_finalize(stmt_station); sqlite3_finalize(stmt_port_loc);
                free(types); free(table_ids); free(segments); free(resolved_loc_ids); free(station_end_loc_ids); free(catch_amounts);
                return 1;
            }
            sqlite3_reset(stmt_port_loc);
            sqlite3_bind_int(stmt_port_loc, 1, port_id);
            if (sqlite3_step(stmt_port_loc) != SQLITE_ROW) {
                fprintf(stderr, "Could not load port location for port %d\n", port_id);
                sqlite3_finalize(stmt_station); sqlite3_finalize(stmt_port_loc);
                free(types); free(table_ids); free(segments); free(resolved_loc_ids); free(station_end_loc_ids); free(catch_amounts);
                return 1;
            }
            int port_loc_id = sqlite3_column_int(stmt_port_loc, 0);

            types[idx] = NODE_TYPE_PORT;
            table_ids[idx] = port_id;
            segments[idx] = segment;
            resolved_loc_ids[idx] = port_loc_id;
            station_end_loc_ids[idx] = 0;
            catch_amounts[idx] = 0;
            idx++;

            segment++;
            types[idx] = NODE_TYPE_PORT;
            table_ids[idx] = port_id;
            segments[idx] = segment;
            resolved_loc_ids[idx] = port_loc_id;
            station_end_loc_ids[idx] = 0;
            catch_amounts[idx] = 0;
            idx++;
        }
    }
    /* Done with the node-building loop — release the pre-compiled statements. */
    sqlite3_finalize(stmt_station); stmt_station = NULL;
    sqlite3_finalize(stmt_port_loc); stmt_port_loc = NULL;

    int num_stations = 0;
    for (int i = 0; i < num_nodes; i++) {
        if (types[i] == NODE_TYPE_STATION) num_stations++;
    }

    /* Build segment boundaries for tour_segments output. */
    int segment_count = 1;
    for (int i = 1; i < num_nodes; i++) {
        if (segments[i] != segments[i - 1]) segment_count++;
    }
    int *seg_start = (int*)malloc((size_t)segment_count * sizeof(int));
    int *seg_end = (int*)malloc((size_t)segment_count * sizeof(int));
    int *segment_length = (int*)malloc((size_t)segment_count * sizeof(int));
    int *segment_catch = (int*)malloc((size_t)segment_count * sizeof(int));
    int *force_append_boat_end_station = (int*)calloc((size_t)segment_count, sizeof(int));

    int sidx = 0;
    seg_start[sidx] = 0;
    for (int i = 1; i < num_nodes; i++) {
        if (segments[i] != segments[i - 1]) {
            seg_end[sidx] = i - 1;
            sidx++;
            seg_start[sidx] = i;
        }
    }
    seg_end[sidx] = num_nodes - 1;

    for (int s = 0; s < segment_count; s++) {
        segment_length[s] = seg_end[s] - seg_start[s] + 1;
        int sum = 0;
        for (int i = seg_start[s]; i <= seg_end[s]; i++) {
            if (types[i] == NODE_TYPE_STATION) sum += catch_amounts[i];
        }
        segment_catch[s] = sum;
    }

    /* Remove synthetic/empty segments (typically trailing duplicated port segment). */
    {
        int write_s = 0;
        for (int s = 0; s < segment_count; s++) {
            if (!segment_has_station(types, seg_start[s], seg_end[s])) {
                continue;
            }
            if (write_s != s) {
                seg_start[write_s] = seg_start[s];
                seg_end[write_s] = seg_end[s];
                segment_length[write_s] = segment_length[s];
                segment_catch[write_s] = segment_catch[s];
            }
            write_s++;
        }
        segment_count = write_s;
    }

    for (int s = 0; s < segment_count; s++) {
        force_append_boat_end_station[s] = 0;
    }

    /* Check feasibility: each station visited exactly once and capacity constraints satisfied */
    int is_feasible = 1;

    int *station_ids = NULL;
    int station_n = 0;
    if (num_stations > 0) {
        station_ids = (int*)malloc((size_t)num_stations * sizeof(int));
        if (!station_ids) {
            fprintf(stderr, "Memory allocation failed for station_ids\n");
            free(types); free(table_ids); free(segments); free(resolved_loc_ids); free(station_end_loc_ids);
            free(catch_amounts); free(seg_start); free(seg_end); free(segment_length); free(segment_catch);
            free(force_append_boat_end_station);
            return 1;
        }
        for (int i = 0; i < num_nodes; i++) {
            if (types[i] == NODE_TYPE_STATION) {
                station_ids[station_n++] = table_ids[i];
            }
        }
    }

    if (!stations_have_no_duplicates(station_ids, station_n)) {
        is_feasible = 0;
    }

    if (!segments_within_capacity(segment_catch, segment_count, (double)capacity)) {
        is_feasible = 0;
    }

    free(station_ids);

    /* Compute segment and total distance along adjacent pairs implied by exported segment location lists. */
    gsp_distance_breakdown_t *segment_breakdowns =
        (gsp_distance_breakdown_t*)calloc((size_t)segment_count, sizeof(gsp_distance_breakdown_t));
    double *segment_distance_nm = (double*)calloc((size_t)segment_count, sizeof(double));
    gsp_distance_breakdown_t total_breakdown;
    double total_distance = 0.0;

    memset(&total_breakdown, 0, sizeof(total_breakdown));

    /* Dock annotations are built from the ordered segment end docks so all
     * repeated boundary visits are preserved consistently across exporters.
     */
    int *dock_location_ids = NULL;
    int dock_n = 0;
    int *segment_end_location_ids = (int*)malloc((size_t)segment_count * sizeof(int));
    if (!segment_end_location_ids) {
        free(types); free(table_ids); free(segments); free(resolved_loc_ids); free(station_end_loc_ids);
        free(catch_amounts); free(seg_start); free(seg_end); free(segment_length); free(segment_catch);
        free(force_append_boat_end_station); free(segment_distance_nm);
        return 1;
    }

    /* Prepare distance statement once for the entire distance computation loop. */
    sqlite3_stmt *stmt_dist = NULL;
    sqlite3_prepare_v2(db,
        "SELECT distance_nm FROM distances WHERE from_location_id = ? AND to_location_id = ?;",
        -1, &stmt_dist, NULL);

    for (int s = 0; s < segment_count; s++) {
        int a = seg_start[s], b = seg_end[s];
        int start_loc = resolve_segment_boundary_loc(
            1, a, num_nodes, types, resolved_loc_ids, boat_start_loc_id, boat_end_loc_id);
        int end_loc = resolve_segment_boundary_loc(
            0, b, num_nodes, types, resolved_loc_ids, boat_start_loc_id, boat_end_loc_id);
        (void)start_loc;
        segment_end_location_ids[s] = end_loc;

        int prev = start_loc;
        for (int i = a; i <= b; i++) {
            if (types[i] == NODE_TYPE_STATION) {
                int s_start = resolved_loc_ids[i];
                int s_end = station_end_loc_ids[i];
                double d1 = lookup_distance_nm_cached(stmt_dist, prev, s_start);
                accumulate_distance_breakdown(&segment_breakdowns[s], d1, 0);
                double d2 = lookup_distance_nm_cached(stmt_dist, s_start, s_end);
                accumulate_distance_breakdown(&segment_breakdowns[s], d2, 1);
                prev = s_end;
            }
        }

        {
            double d3 = lookup_distance_nm_cached(stmt_dist, prev, end_loc);
            accumulate_distance_breakdown(&segment_breakdowns[s], d3, 0);
        }
        segment_distance_nm[s] = segment_breakdowns[s].total_distance_nm;
        total_breakdown.transit_distance_nm += segment_breakdowns[s].transit_distance_nm;
        total_breakdown.haul_distance_nm += segment_breakdowns[s].haul_distance_nm;
        total_breakdown.total_distance_nm += segment_breakdowns[s].total_distance_nm;
        total_distance += segment_distance_nm[s];
    }
    sqlite3_finalize(stmt_dist); stmt_dist = NULL;

    if (!gsp_build_dock_location_ids_from_segment_ends(boat_start_loc_id,
                                                       segment_end_location_ids,
                                                       segment_count,
                                                       &dock_location_ids,
                                                       &dock_n)) {
        free(segment_end_location_ids);
        free(types); free(table_ids); free(segments); free(resolved_loc_ids); free(station_end_loc_ids); free(catch_amounts);
        free(seg_start); free(seg_end); free(segment_length); free(segment_catch); free(force_append_boat_end_station);
        free(segment_breakdowns); free(segment_distance_nm);
        return 1;
    }
    free(segment_end_location_ids);

    /* Unique waypoint IDs present in expanded tour chains. */
    int *unique_waypoint_location_ids = NULL;
    int uniq_wp_n = 0, uniq_wp_cap = 0;

    /* out is provided by the caller */

    fprintf(out, "{\n");
    fprintf(out, "  \"metadata\": {\n");
    fprintf(out, "    \"solver_version\": \"survey_export_1.1\",\n");
    fprintf(out, "    \"timestamp\": \"%ld\",\n", (long)time(NULL));
    fprintf(out, "    \"mode\": \"survey\",\n");
    fprintf(out, "    \"strategy\": \"baseline\",\n");
    fprintf(out, "    \"boat_id\": %d,\n", boat_id);
    fprintf(out, "    \"boat_name\": \"%s\",\n", boat_name);
    fprintf(out, "    \"boat_docked_location\": {\"lat\": %.6f, \"lon\": %.6f},\n", dock_lat, dock_lon);
    fprintf(out, "    \"boat_location_id\": %d\n", boat_start_loc_id);
    fprintf(out, "  },\n");

    fprintf(out, "  \"problem\": {\n");
    fprintf(out, "    \"num_nodes\": %d,\n", num_nodes);
    fprintf(out, "    \"num_stations\": %d,\n", num_stations);
    fprintf(out, "    \"capacity\": %d\n", capacity);
    fprintf(out, "  },\n");

    fprintf(out, "  \"solution\": {\n");
    {
        gsp_int_list_view_t *location_segments =
            (gsp_int_list_view_t*)calloc((size_t)segment_count, sizeof(gsp_int_list_view_t));
        gsp_int_list_view_t *station_segments =
            (gsp_int_list_view_t*)calloc((size_t)segment_count, sizeof(gsp_int_list_view_t));
        gsp_summary_json_t summary = {0};
        gsp_solution_json_view_t solution_view = {0};
        double distance_trajectory[1] = {total_breakdown.total_distance_nm};
        double runtime_trajectory[1] = {0.0};

        if (!location_segments || !station_segments) {
            free(location_segments);
            free(station_segments);
            free(dock_location_ids);
            free(unique_waypoint_location_ids);
            free(types); free(table_ids); free(segments); free(resolved_loc_ids); free(station_end_loc_ids); free(catch_amounts);
            free(seg_start); free(seg_end); free(segment_length); free(segment_catch); free(force_append_boat_end_station);
            free(segment_breakdowns); free(segment_distance_nm);
            return 1;
        }

        /* Prepare waypoint statement once for all expansion iterations. */
        sqlite3_stmt *stmt_wp = NULL;
        sqlite3_prepare_v2(db,
            "SELECT waypoint_path FROM distances WHERE from_location_id = ? AND to_location_id = ?;",
            -1, &stmt_wp, NULL);

        for (int s = 0; s < segment_count; s++) {
            int a = seg_start[s], b = seg_end[s];
            int start_loc = resolve_segment_boundary_loc(
                1, a, num_nodes, types, resolved_loc_ids, boat_start_loc_id, boat_end_loc_id);
            int end_loc = resolve_segment_boundary_loc(
                0, b, num_nodes, types, resolved_loc_ids, boat_start_loc_id, boat_end_loc_id);
            int base_cap = 2 + 2 * (b - a + 1);
            int *base = (int*)malloc((size_t)base_cap * sizeof(int));
            int *expanded = NULL;
            int expanded_n = 0, expanded_cap = 0;
            int *station_ids = NULL;
            int station_n = 0, station_cap = 0;
            int base_n = 0;

            if (!base) {
                for (int i = 0; i < s; i++) {
                    free((int*)location_segments[i].values);
                    free((int*)station_segments[i].values);
                }
                free(location_segments);
                free(station_segments);
                free(dock_location_ids);
                free(unique_waypoint_location_ids);
                free(types); free(table_ids); free(segments); free(resolved_loc_ids); free(station_end_loc_ids); free(catch_amounts);
                free(seg_start); free(seg_end); free(segment_length); free(segment_catch); free(force_append_boat_end_station);
                free(segment_breakdowns); free(segment_distance_nm);
                return 1;
            }

            base[base_n++] = start_loc;
            for (int i = a; i <= b; i++) {
                if (types[i] == NODE_TYPE_STATION) {
                    base[base_n++] = resolved_loc_ids[i];
                    base[base_n++] = station_end_loc_ids[i];
                    (void)append_int(&station_ids, &station_n, &station_cap, table_ids[i]);
                }
            }
            base[base_n++] = end_loc;

            if (s == segment_count - 1 && force_append_boat_end_station[s]) {
                (void)append_int(&station_ids, &station_n, &station_cap, boat_end_loc_id);
            }

            if (base_n > 0) {
                (void)append_int(&expanded, &expanded_n, &expanded_cap, base[0]);
                for (int i = 0; i < base_n - 1; i++) {
                    int from_loc = base[i];
                    int to_loc = base[i + 1];
                    int *wps = NULL;
                    int n_wps = lookup_waypoint_path_cached(stmt_wp, from_loc, to_loc, &wps);

                    if (n_wps > 0) {
                        for (int k = 0; k < n_wps; k++) {
                            (void)append_int(&expanded, &expanded_n, &expanded_cap, wps[k]);
                            (void)append_unique_int(&unique_waypoint_location_ids, &uniq_wp_n, &uniq_wp_cap, wps[k]);
                        }
                    }

                    (void)append_int(&expanded, &expanded_n, &expanded_cap, to_loc);
                    free(wps);
                }
            }

            free(base);
            location_segments[s].values = expanded;
            location_segments[s].count = expanded_n;
            station_segments[s].values = station_ids;
            station_segments[s].count = station_n;
        }
        sqlite3_finalize(stmt_wp); stmt_wp = NULL;

        fprintf(out, "    \"Spring 2023\": ");
        solution_view.variant_name = "Spring 2023";
        solution_view.tour_segments_location_ids = location_segments;
        solution_view.tour_segments_location_count = segment_count;
        solution_view.dock_location_ids = dock_location_ids;
        solution_view.dock_location_count = dock_n;
        solution_view.unique_waypoint_location_ids = unique_waypoint_location_ids;
        solution_view.unique_waypoint_location_count = uniq_wp_n;
        solution_view.tour_segments_station_ids = station_segments;
        solution_view.tour_segments_station_count = segment_count;
        solution_view.tour_length = segment_length;
        solution_view.tour_length_count = segment_count;
        solution_view.segment_count = segment_count;
        solution_view.segment_catch_amount = segment_catch;
        solution_view.segment_catch_count = segment_count;
        solution_view.segment_breakdowns = segment_breakdowns;
        solution_view.grand_total = &total_breakdown;
        solution_view.feasible = is_feasible;
        gsp_write_solution_json(out, "    ", &solution_view, 0);
        fprintf(out, "  },\n");

        summary.final_name = "Spring 2023";
        summary.stage_name = "survey_export_complete";
        summary.feasible = is_feasible;
        summary.method_name = "survey_export";
        summary.distance_trajectory_nm = distance_trajectory;
        summary.distance_trajectory_count = 1;
        summary.final_distance_nm = total_breakdown.total_distance_nm;
        summary.preprocessing_seconds = 0.0;
        summary.solution_runtime_seconds = runtime_trajectory;
        summary.solution_runtime_count = 1;
        summary.postprocessing_seconds = 0.0;
        summary.grandtotal_seconds = 0.0;
        summary.include_runtime = 1;

        gsp_write_summary_json(out, "  ", &summary, 1);

        for (int s = 0; s < segment_count; s++) {
            free((int*)location_segments[s].values);
            free((int*)station_segments[s].values);
        }
        free(location_segments);
        free(station_segments);
    }
    fprintf(out, "  \"solver_stats\": {\n");
    fprintf(out, "    \"status\": \"survey_baseline\",\n");
    fprintf(out, "    \"runtime_seconds\": 0.0,\n");
    fprintf(out, "    \"method\": \"survey_export\"\n");
    fprintf(out, "  }\n");
    fprintf(out, "}\n");

    printf("[OK] Built survey route for boat_id=%d\n", boat_id);
    printf("  Boat: %s (capacity: %d)\n", boat_name, capacity);
    printf("  Total distance: %.2f nm\n", total_distance);
    printf("  Nodes: %d, Segments: %d\n", num_nodes, segment_count);
    printf("  Feasible: %s\n", is_feasible ? "true" : "false");

    if (!is_feasible) {
        for (int s = 0; s < segment_count; s++) {
            if (segment_catch[s] > capacity) {
                printf("    [!] Segment %d: catch=%d exceeds capacity=%d\n", s+1, segment_catch[s], capacity);
            }
        }
    }

    /* Fill stats_out for the multivessel summary/candidate_ports computation */
    if (stats_out) {
        stats_out->num_stations    = num_stations;
        stats_out->segment_count   = segment_count;
        stats_out->total_transit_nm  = total_breakdown.transit_distance_nm;
        stats_out->total_distance_nm = total_breakdown.total_distance_nm;
        stats_out->boat_name = dup_mv(boat_name);
        stats_out->boat_start_loc_id = boat_start_loc_id;
        stats_out->boat_id = boat_id;
        stats_out->segment_catch = (int *)malloc((size_t)segment_count * sizeof(int));
        if (stats_out->segment_catch)
            memcpy(stats_out->segment_catch, segment_catch, (size_t)segment_count * sizeof(int));
        /* dock_location_ids: [start, end1, end2, ..., endN] — skip index 0 (start) */
        int n_ends = dock_n - 1;
        stats_out->segment_end_dock_ids = (int *)malloc((size_t)(n_ends > 0 ? n_ends : 1) * sizeof(int));
        if (stats_out->segment_end_dock_ids && n_ends > 0)
            memcpy(stats_out->segment_end_dock_ids, dock_location_ids + 1, (size_t)n_ends * sizeof(int));
    }

    free(dock_location_ids);
    free(unique_waypoint_location_ids);
    free(types); free(table_ids); free(segments); free(resolved_loc_ids); free(station_end_loc_ids); free(catch_amounts);
    free(seg_start); free(seg_end); free(segment_length); free(segment_catch); free(force_append_boat_end_station);
    free(segment_breakdowns); free(segment_distance_nm);
    return 0;
}

/* Wrapper: opens file, delegates to export_boat_json_fp, closes file */
static int export_boat_json(sqlite3 *db, const SurveyEntryVec *entries, int boat_id, const char *output_path) {
    FILE *out = fopen(output_path, "w");
    if (!out) { perror("fopen"); return 1; }
    int ret = export_boat_json_fp(db, entries, boat_id, out, NULL);
    fclose(out);
    return ret;
}

/* Write all boats + candidate_ports + survey_summary into one multivessel.json */
static int export_multivessel_json(
    sqlite3 *db,
    const SurveyEntryVec *entries,
    const int *boat_ids,
    int boat_count,
    const char *output_path,
    const char *config_yaml)
{
    boat_stats_t *stats = (boat_stats_t *)calloc((size_t)boat_count, sizeof(boat_stats_t));
    if (!stats) return 1;

    FILE *out = fopen(output_path, "w");
    if (!out) { perror("fopen"); free(stats); return 1; }

    fprintf(out, "{\n  \"boats\": [\n");

    int result = 0;
    for (int i = 0; i < boat_count; i++) {
        if (i > 0) fprintf(out, ",\n");
        if (export_boat_json_fp(db, entries, boat_ids[i], out, &stats[i]) != 0) {
            fprintf(stderr, "Failed to export boat %d\n", boat_ids[i]);
            result = 1;
            goto cleanup;
        }
    }
    fprintf(out, "  ],\n");

    /* --- candidate_ports section (same logic as survey_fixedport_candidates) --- */
    {
        int min_catch_kg = read_fixedport_min_catch_kg(config_yaml);
        port_info_mv_t *port_lookup = NULL;
        int port_lookup_count = 0;
        load_port_lookup_mv(db, &port_lookup, &port_lookup_count);

        int *ordered = NULL;
        int ordered_n = 0, ordered_cap = 0;
        int total_kept = 0;

        for (int i = 0; i < boat_count; i++) {
            for (int s = 0; s < stats[i].segment_count; s++) {
                if (!stats[i].segment_catch || !stats[i].segment_end_dock_ids) continue;
                if (stats[i].segment_catch[s] < min_catch_kg) continue;
                if (ordered_n >= ordered_cap) {
                    int nc = (ordered_cap > 0) ? (ordered_cap * 2) : 16;
                    int *tmp = (int *)realloc(ordered, (size_t)nc * sizeof(int));
                    if (!tmp) { free(ordered); goto candidate_done; }
                    ordered = tmp; ordered_cap = nc;
                }
                ordered[ordered_n++] = stats[i].segment_end_dock_ids[s];
                total_kept++;
            }
        }

        for (int p = 0; p < ordered_n; p++)
            for (int k = 0; k < port_lookup_count; k++)
                if (port_lookup[k].location_id == ordered[p]) { port_lookup[k].visit_count++; break; }

        fprintf(out, "  \"candidate_ports\": {\n");

        /* port_visit_summary first */
        fprintf(out, "    \"port_visit_summary\": [");
        { int em = 0;
          for (int k = 0; k < port_lookup_count; k++) {
              if (port_lookup[k].visit_count <= 0) continue;
              if (em++) fprintf(out, ",");
              fprintf(out, "\n      {\"location_id\": %d, \"port_id\": %d, \"name\": \"%s\", \"visit_count\": %d}",
                      port_lookup[k].location_id, port_lookup[k].port_id,
                      port_lookup[k].name ? port_lookup[k].name : "", port_lookup[k].visit_count);
          }
          if (em) fprintf(out, "\n    ");
        }
        fprintf(out, "],\n");

        fprintf(out, "    \"min_segment_catch_kg\": %d,\n", min_catch_kg);
        fprintf(out, "    \"boat_count\": %d,\n", boat_count);
        fprintf(out, "    \"kept_segment_count\": %d,\n", total_kept);
        fprintf(out, "    \"ordered_port_location_ids\": [");
        for (int p = 0; p < ordered_n; p++) { if (p) fprintf(out, ", "); fprintf(out, "%d", ordered[p]); }
        fprintf(out, "],\n");

        /* Per-boat segment breakdown — dict keyed by boat_id */
        fprintf(out, "    \"boats\": {");
        for (int i = 0; i < boat_count; i++) {
            if (i > 0) fprintf(out, ",");
            const boat_stats_t *bs = &stats[i];
            int n_segs = bs->segment_count;
            fprintf(out, "\n      \"%d\": {", bs->boat_id);
            fprintf(out, "\n        \"name\": \"%s\",", bs->boat_name ? bs->boat_name : "");
            /* dock_location_ids: [start, end1, ..., endN] */
            fprintf(out, "\n        \"dock_location_ids\": [%d", bs->boat_start_loc_id);
            for (int s = 0; s < n_segs; s++)
                fprintf(out, ", %d", bs->segment_end_dock_ids ? bs->segment_end_dock_ids[s] : 0);
            fprintf(out, "],");
            fprintf(out, "\n        \"segment_end_dock_location_ids\": [");
            for (int s = 0; s < n_segs; s++) {
                if (s) fprintf(out, ", ");
                fprintf(out, "%d", bs->segment_end_dock_ids ? bs->segment_end_dock_ids[s] : 0);
            }
            fprintf(out, "],");
            fprintf(out, "\n        \"segment_catch_amount\": [");
            for (int s = 0; s < n_segs; s++) {
                if (s) fprintf(out, ", ");
                fprintf(out, "%d", bs->segment_catch ? bs->segment_catch[s] : 0);
            }
            fprintf(out, "],");
            int kept = 0, dropped = 0;
            for (int s = 0; s < n_segs; s++) {
                if (bs->segment_catch && bs->segment_catch[s] >= min_catch_kg) kept++;
                else dropped++;
            }
            fprintf(out, "\n        \"kept_segment_count\": %d,", kept);
            fprintf(out, "\n        \"dropped_segment_count\": %d,", dropped);
            fprintf(out, "\n        \"kept_end_dock_location_ids\": [");
            { int first = 1;
              for (int s = 0; s < n_segs; s++) {
                  if (bs->segment_catch && bs->segment_catch[s] >= min_catch_kg) {
                      if (!first) fprintf(out, ", ");
                      fprintf(out, "%d", bs->segment_end_dock_ids ? bs->segment_end_dock_ids[s] : 0);
                      first = 0;
                  }
              }
            }
            fprintf(out, "],");
            fprintf(out, "\n        \"dropped_segments\": [");
            { int first = 1;
              for (int s = 0; s < n_segs; s++) {
                  if (bs->segment_catch && bs->segment_catch[s] < min_catch_kg) {
                      if (!first) fprintf(out, ", ");
                      fprintf(out, "{\"segment\": %d, \"catch_kg\": %d, \"end_dock_location_id\": %d}",
                              s + 1, bs->segment_catch[s],
                              bs->segment_end_dock_ids ? bs->segment_end_dock_ids[s] : 0);
                      first = 0;
                  }
              }
            }
            fprintf(out, "]\n      }");
        }
        fprintf(out, "\n    }\n");
        fprintf(out, "  },\n");

        free(ordered);
        free_port_info_mv(port_lookup, port_lookup_count);
candidate_done:;
    }

    /* --- survey_summary section --- */
    {
        int total_stations = 0, total_segments = 0;
        double total_transit = 0.0, total_distance = 0.0;
        for (int i = 0; i < boat_count; i++) {
            total_stations  += stats[i].num_stations;
            total_segments  += stats[i].segment_count;
            total_transit   += stats[i].total_transit_nm;
            total_distance  += stats[i].total_distance_nm;
        }
        fprintf(out, "  \"survey_summary\": {\n");
        fprintf(out, "    \"boat_count\": %d,\n", boat_count);
        fprintf(out, "    \"total_stations\": %d,\n", total_stations);
        fprintf(out, "    \"total_segments\": %d,\n", total_segments);
        fprintf(out, "    \"total_transit_nm\": %.2f,\n", total_transit);
        fprintf(out, "    \"total_distance_nm\": %.2f\n", total_distance);
        fprintf(out, "  }\n");
    }

    fprintf(out, "}\n");
    printf("[OK] Wrote %s (%d boats)\n", output_path, boat_count);

cleanup:
    fclose(out);
    for (int i = 0; i < boat_count; i++) free_boat_stats(&stats[i]);
    free(stats);
    return result;
}

int main(int argc, char **argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    setlocale(LC_CTYPE, "");
    setlocale(LC_NUMERIC, "C");

    if (argc < 5) {
        fprintf(stderr, "Usage: %s <survey.dat> <database.db> <multivessel.json> <config.yaml>\n", argv[0]);
        return 1;
    }

    const char *dat_path       = argv[1];
    const char *db_path        = argv[2];
    const char *output_path    = argv[3];
    const char *config_yaml    = argv[4];

    SurveyEntryVec entries;
    survey_entry_vec_init(&entries);
    if (!parse_survey_file(dat_path, &entries)) {
        survey_entry_vec_free(&entries);
        return 1;
    }

    sqlite3 *db;
    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }

    /* Collect all unique boat IDs from the survey entries */
    int *boat_ids = NULL;
    int boat_count = 0, boat_cap = 0;
    int current_boat_id = 0;
    for (int i = 0; i < entries.n; i++) {
        if (entries.entries[i].type != NODE_TYPE_BOAT) continue;
        current_boat_id = lookup_boat_id(db, entries.entries[i].name);
        if (current_boat_id <= 0) {
            fprintf(stderr, "Could not resolve boat named '%s'\n",
                    entries.entries[i].name ? entries.entries[i].name : "(null)");
            free(boat_ids);
            sqlite3_close(db);
            survey_entry_vec_free(&entries);
            return 1;
        }
        if (!append_unique_int(&boat_ids, &boat_count, &boat_cap, current_boat_id)) {
            free(boat_ids);
            sqlite3_close(db);
            survey_entry_vec_free(&entries);
            return 1;
        }
    }

    if (boat_count == 0) {
        fprintf(stderr, "No boats found in survey\n");
        free(boat_ids);
        sqlite3_close(db);
        survey_entry_vec_free(&entries);
        return 1;
    }

    /* Create output directory if needed */
    {
        char dir[512];
        strncpy(dir, output_path, sizeof(dir) - 1);
        dir[sizeof(dir) - 1] = '\0';
        char *slash = strrchr(dir, '/');
        char *bslash = strrchr(dir, '\\');
        char *last = (slash > bslash) ? slash : bslash;
        if (last) {
            *last = '\0';
#ifdef _WIN32
            _mkdir(dir);
#else
            mkdir(dir, 0755);
#endif
        }
    }

    int result = export_multivessel_json(db, &entries, boat_ids, boat_count, output_path, config_yaml);

    free(boat_ids);
    sqlite3_close(db);
    survey_entry_vec_free(&entries);
    return result;
}


