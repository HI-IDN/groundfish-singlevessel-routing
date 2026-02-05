#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <math.h>
#include <sqlite3.h>
#include "utils.h"

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

#define MAXLINE 8192
#define PROGRESS_INTERVAL 100000

typedef struct {
    int id;
    int lat, lon;
    char *type; /* "S1","S2","B","P","W" */
} Loc;

static Loc *locs = NULL;
static size_t locs_n = 0, locs_cap = 0;

static void ensure_locs(size_t want) {
    if (locs_cap >= want) return;
    size_t nc = locs_cap ? locs_cap * 2 : 256;
    while (nc < want) nc *= 2;
    Loc *n = realloc(locs, nc * sizeof(Loc));
    if (!n) { fprintf(stderr, "out of memory\n"); exit(1); }
    locs = n;
    locs_cap = nc;
}

static char *strdup_safe(const char *s) {
    if (!s) return NULL;
    char *r = malloc(strlen(s) + 1);
    if (!r) return NULL;
    strcpy(r, s);
    return r;
}

static char *trim_inplace(char *s) {
    if (!s) return s;
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)*(e - 1))) *(--e) = '\0';
    return s;
}

/* remove all backslash characters and compress whitespace */
static void remove_backslashes_and_compress(char *s) {
    if (!s) return;
    char *r = s, *p = s;
    int prev_space = 0;
    while (*p) {
        if (*p == '\\') { p++; continue; }
        if (isspace((unsigned char)*p)) {
            if (!prev_space) { *r++ = ' '; prev_space = 1; }
            p++;
            continue;
        }
        prev_space = 0;
        *r++ = *p++;
    }
    while (r > s && isspace((unsigned char)*(r - 1))) r--;
    *r = '\0';
}

/* tokenize: return malloc'd token, advance *pp; handles quoted tokens */
static char *next_token(char **pp) {
    if (!pp || !*pp) return NULL;
    char *p = *pp;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == '\0') { *pp = p; return NULL; }

    if (*p == '"' || *p == '\'') {
        char q = *p++;
        char *start = p;
        size_t len = 0;
        while (*p && *p != q) {
            if (*p == '\\' && p[1]) { p += 2; len++; }
            else { p++; len++; }
        }
        char *out = malloc(len + 1);
        if (!out) return NULL;
        char *o = out;
        p = start;
        while (*p && *p != q) {
            if (*p == '\\' && p[1]) { *o++ = p[1]; p += 2; }
            else { *o++ = *p++; }
        }
        *o = '\0';
        if (*p == q) p++;
        *pp = p;
        return out;
    } else {
        char *start = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        size_t len = p - start;
        char *tok = malloc(len + 1);
        if (!tok) return NULL;
        memcpy(tok, start, len);
        tok[len] = '\0';
        *pp = p;
        return tok;
    }
}

/* remove substring [a,b) from s */
static void remove_range(char *s, char *a, char *b) {
    if (!s || !a || !b) return;
    if (a >= b) return;
    memmove(a, b, strlen(b) + 1);
}

/* find and remove integer key like key=123 from s; returns 1 on success and stores value in out */
static int extract_int_key(char *s, const char *key, int *out) {
    if (!s || !key) return 0;
    char *k = strstr(s, key);
    if (!k) return 0;
    char *eq = strchr(k, '=');
    if (!eq) return 0;
    char *p = eq + 1;
    while (*p && isspace((unsigned char)*p)) p++;
    if (!*p) return 0;
    char *start = p;
    if (*p == '+' || *p == '-') p++;
    if (!isdigit((unsigned char)*p)) return 0;
    while (*p && isdigit((unsigned char)*p)) p++;
    char *end = p;
    char tmp = *end;
    *end = '\0';
    int val = atoi(start);
    *end = tmp;
    /* remove token plus adjacent whitespace/backslashes/commas */
    char *rem_start = k;
    while (rem_start > s && isspace((unsigned char)*(rem_start - 1))) rem_start--;
    char *rem_end = end;
    while (*rem_end && (isspace((unsigned char)*rem_end) || *rem_end == '\\' || *rem_end == ',' || *rem_end == ':' || *rem_end == ';')) rem_end++;
    remove_range(s, rem_start, rem_end);
    trim_inplace(s);
    if (out) *out = val;
    return 1;
}

/* find existing location by lat,lon,type */
static int find_loc(int lat, int lon, const char *type) {
    for (size_t i = 0; i < locs_n; ++i) {
        if (locs[i].lat == lat && locs[i].lon == lon && strcmp(locs[i].type, type) == 0) return (int)i;
    }
    return -1;
}

/* add or return existing index */
static int add_loc(int lat, int lon, const char *type) {
    int idx = find_loc(lat, lon, type);
    if (idx >= 0) return idx;
    ensure_locs(locs_n + 1);
    locs[locs_n].id = -1;
    locs[locs_n].lat = lat;
    locs[locs_n].lon = lon;
    locs[locs_n].type = strdup_safe(type);
    return (int)(locs_n++);
}

/* geometry helpers are provided by the shared utilities in utils.c/h.
   Use the declarations in utils.h (included above). */

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s input.dat [outdir] [island_bin] [--force]\n", argv[0]);
        fprintf(stderr, "  --force   overwrite existing parsed_data.sqlite if present\n");
        return 1;
    }
    const char *infile = argv[1];
    const char *outdir = "dat";
    const char *island_path = "bin/island.bin"; /* default */
    int force = 0;
    /* scan optional positional args and flags from argv[2..] */
    int seen_out = 0, seen_island = 0;
    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--force") == 0) { force = 1; continue; }
        if (!seen_out) { outdir = argv[i]; seen_out = 1; continue; }
        if (!seen_island) { island_path = argv[i]; seen_island = 1; continue; }
        /* ignore extras */
    }
    char dbpath[1024];
    snprintf(dbpath, sizeof(dbpath), "%s/parsed_data.sqlite", outdir);

    if (MKDIR(outdir) != 0 && errno != EEXIST) { /* best-effort */ }

    /* If the DB exists already, require --force to overwrite; otherwise abort to avoid accidental data loss */
    FILE *fcheck = fopen(dbpath, "rb");
    if (fcheck) {
        fclose(fcheck);
        if (!force) {
            fprintf(stderr, "parsed database already exists at '%s'. To overwrite, run with --force.\n", dbpath);
            return 2;
        }
        /* remove existing DB when --force was specified */
        if (remove(dbpath) != 0) {
            fprintf(stderr, "warning: could not remove existing DB '%s' (errno=%d)\n", dbpath, errno);
        } else {
            fprintf(stderr, "Removed existing DB '%s' (--force).\n", dbpath);
        }
    }

    FILE *f = fopen(infile, "r");
    if (!f) { fprintf(stderr, "open %s: %s\n", infile, strerror(errno)); return 1; }

    sqlite3 *db = NULL;
    if (sqlite3_open_v2(dbpath, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK) {
        fprintf(stderr, "sqlite open %s: %s\n", dbpath, sqlite3_errmsg(db));
        if (db) sqlite3_close(db);
        fclose(f);
        return 1;
    }

    /* enable foreign keys */
    if (sqlite3_exec(db, "PRAGMA foreign_keys = ON;", NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "failed to enable foreign keys: %s\n", sqlite3_errmsg(db));
    }

    const char *schema =
        "CREATE TABLE IF NOT EXISTS locations(id INTEGER PRIMARY KEY, lat_int INTEGER, lon_int INTEGER, type TEXT);"
        "CREATE UNIQUE INDEX IF NOT EXISTS locations_unique ON locations(lat_int,lon_int,type);"
        "CREATE TABLE IF NOT EXISTS boats(id INTEGER PRIMARY KEY, start_loc INTEGER, end_loc INTEGER, capacity INTEGER, c1 INTEGER, c2 INTEGER, c3 INTEGER, c4 INTEGER, c5 INTEGER, c6 INTEGER, name TEXT, FOREIGN KEY(start_loc) REFERENCES locations(id), FOREIGN KEY(end_loc) REFERENCES locations(id));"
        "CREATE TABLE IF NOT EXISTS ports(id INTEGER PRIMARY KEY, loc_id INTEGER, name TEXT, flag INTEGER, FOREIGN KEY(loc_id) REFERENCES locations(id));"
        "CREATE TABLE IF NOT EXISTS stations(id INTEGER PRIMARY KEY, ext_id INTEGER, start_loc INTEGER, end_loc INTEGER, remark TEXT, bottom_depth_cast INTEGER, bottom_depth_haul INTEGER, FOREIGN KEY(start_loc) REFERENCES locations(id), FOREIGN KEY(end_loc) REFERENCES locations(id));"
        "CREATE TABLE IF NOT EXISTS waypoints(id INTEGER PRIMARY KEY, loc_id INTEGER, flag INTEGER, FOREIGN KEY(loc_id) REFERENCES locations(id));";
    char *err = NULL;
    if (sqlite3_exec(db, schema, NULL, NULL, &err) != SQLITE_OK) {
        fprintf(stderr, "sqlite schema error: %s\n", err);
        sqlite3_free(err);
        sqlite3_close(db);
        fclose(f);
        return 1;
    }

    sqlite3_stmt *st_loc_ins = NULL, *st_loc_sel = NULL;
    sqlite3_stmt *st_boat = NULL, *st_port = NULL, *st_stat = NULL, *st_wayp = NULL;
    if (sqlite3_prepare_v2(db, "INSERT OR IGNORE INTO locations(lat_int,lon_int,type) VALUES(?,?,?);", -1, &st_loc_ins, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(db, "SELECT id FROM locations WHERE lat_int=? AND lon_int=? AND type=?;", -1, &st_loc_sel, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(db, "INSERT INTO boats(start_loc,end_loc,capacity,c1,c2,c3,c4,c5,c6,name) VALUES(?,?,?,?,?,?,?,?,?,?);", -1, &st_boat, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(db, "INSERT INTO ports(loc_id,name,flag) VALUES(?,?,?);", -1, &st_port, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(db, "INSERT INTO stations(ext_id,start_loc,end_loc,remark,bottom_depth_cast,bottom_depth_haul) VALUES(?,?,?,?,?,?);", -1, &st_stat, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(db, "INSERT INTO waypoints(loc_id,flag) VALUES(?,?);", -1, &st_wayp, NULL) != SQLITE_OK) {
        fprintf(stderr, "sqlite prepare error: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(st_loc_ins); sqlite3_finalize(st_loc_sel);
        sqlite3_finalize(st_boat); sqlite3_finalize(st_port); sqlite3_finalize(st_stat); sqlite3_finalize(st_wayp);
        sqlite3_close(db); fclose(f);
        return 1;
    }

    char line[MAXLINE];
    int lineno = 0;
    sqlite3_exec(db, "BEGIN;", NULL, NULL, NULL);

    while (fgets(line, sizeof(line), f)) {
        lineno++;
        for (char *q = line; *q; ++q) if (*q == ',') *q = ' ';
        char *sline = trim_inplace(line);
        if (*sline == '\0') continue;
        if (*sline == '#') continue;

        char *hash = strchr(sline, '#');
        char *comment = NULL;
        if (hash) {
            comment = strdup_safe(hash + 1);
            *hash = '\0';
            if (comment) { remove_backslashes_and_compress(comment); trim_inplace(comment); }
            trim_inplace(sline);
        }

        char *p = sline;
        char *tok = next_token(&p);
        if (!tok) { free(comment); continue; }

        if (strcmp(tok, "PORT") == 0) {
            char *slat = next_token(&p);
            char *slon = next_token(&p);
            char *name = next_token(&p);
            char *flag = next_token(&p);
            if (!slat || !slon) {
                fprintf(stderr, "line %d: PORT parse error\n", lineno);
                free(slat); free(slon); free(name); free(flag); free(tok); free(comment);
                continue;
            }
            int lat = atoi(slat), lon = atoi(slon);
            int li = add_loc(lat, lon, "P");
            /* insert location if needed */
            if (locs[li].id == -1) {
                sqlite3_bind_int(st_loc_ins, 1, locs[li].lat);
                sqlite3_bind_int(st_loc_ins, 2, locs[li].lon);
                sqlite3_bind_text(st_loc_ins, 3, locs[li].type, -1, SQLITE_STATIC);
                sqlite3_step(st_loc_ins);
                sqlite3_reset(st_loc_ins);
                sqlite3_clear_bindings(st_loc_ins);

                sqlite3_bind_int(st_loc_sel, 1, locs[li].lat);
                sqlite3_bind_int(st_loc_sel, 2, locs[li].lon);
                sqlite3_bind_text(st_loc_sel, 3, locs[li].type, -1, SQLITE_STATIC);
                if (sqlite3_step(st_loc_sel) == SQLITE_ROW) locs[li].id = sqlite3_column_int(st_loc_sel, 0);
                sqlite3_reset(st_loc_sel);
                sqlite3_clear_bindings(st_loc_sel);
            }

            sqlite3_bind_int(st_port, 1, locs[li].id);
            sqlite3_bind_text(st_port, 2, name ? name : "", -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(st_port, 3, flag ? atoi(flag) : 0);
            if (sqlite3_step(st_port) != SQLITE_DONE)
                fprintf(stderr, "sqlite insert port error: %s\n", sqlite3_errmsg(db));
            sqlite3_reset(st_port);
            sqlite3_clear_bindings(st_port);

            free(slat); free(slon); free(name); free(flag);

        } else if (strcmp(tok, "BOAT") == 0) {
            /* BOAT lat1 lon1 lat2 lon2 capacity c1..c6 name? (name may be quoted) */
            char *slat1 = next_token(&p), *slon1 = next_token(&p), *slat2 = next_token(&p), *slon2 = next_token(&p);
            char *scap = next_token(&p);
            char *c[6];
            for (int i = 0; i < 6; ++i) c[i] = next_token(&p);
            char *name = NULL;
            if (p && *p) {
                char *tp = p;
                while (*tp && isspace((unsigned char)*tp)) tp++;
                if (*tp) {
                    name = next_token(&p);
                }
            }
            if (!slat1 || !slon1 || !slat2 || !slon2 || !scap) {
                fprintf(stderr, "line %d: BOAT parse error\n", lineno);
                free(slat1); free(slon1); free(slat2); free(slon2); free(scap);
                for (int i=0;i<6;++i) free(c[i]);
                free(name); free(tok); free(comment);
                continue;
            }
            int lat1 = atoi(slat1), lon1 = atoi(slon1), lat2 = atoi(slat2), lon2 = atoi(slon2);
            int capacity = atoi(scap);
            int ci[6] = {0,0,0,0,0,0};
            for (int i = 0; i < 6; ++i) if (c[i]) ci[i] = atoi(c[i]);

            int li1 = add_loc(lat1, lon1, "B");
            int li2 = add_loc(lat2, lon2, "B");
            int arr_li[2] = { li1, li2 };
            for (int ai = 0; ai < 2; ++ai) {
                int li = arr_li[ai];
                if (locs[li].id == -1) {
                    sqlite3_bind_int(st_loc_ins, 1, locs[li].lat);
                    sqlite3_bind_int(st_loc_ins, 2, locs[li].lon);
                    sqlite3_bind_text(st_loc_ins, 3, locs[li].type, -1, SQLITE_STATIC);
                    sqlite3_step(st_loc_ins);
                    sqlite3_reset(st_loc_ins);
                    sqlite3_clear_bindings(st_loc_ins);

                    sqlite3_bind_int(st_loc_sel, 1, locs[li].lat);
                    sqlite3_bind_int(st_loc_sel, 2, locs[li].lon);
                    sqlite3_bind_text(st_loc_sel, 3, locs[li].type, -1, SQLITE_STATIC);
                    if (sqlite3_step(st_loc_sel) == SQLITE_ROW) locs[li].id = sqlite3_column_int(st_loc_sel, 0);
                    sqlite3_reset(st_loc_sel);
                    sqlite3_clear_bindings(st_loc_sel);
                }
            }

            int has_name = (name && *name);
            sqlite3_bind_int(st_boat, 1, locs[li1].id);
            sqlite3_bind_int(st_boat, 2, locs[li2].id);
            sqlite3_bind_int(st_boat, 3, capacity);
            for (int i = 0; i < 6; ++i) sqlite3_bind_int(st_boat, 4 + i, ci[i]);
            if (has_name) sqlite3_bind_text(st_boat, 10, name, -1, SQLITE_TRANSIENT);
            else sqlite3_bind_null(st_boat, 10);

            if (sqlite3_step(st_boat) != SQLITE_DONE)
                fprintf(stderr, "sqlite insert boat error: %s\n", sqlite3_errmsg(db));
            sqlite3_reset(st_boat);
            sqlite3_clear_bindings(st_boat);

            free(slat1); free(slon1); free(slat2); free(slon2); free(scap);
            for (int i=0;i<6;++i) free(c[i]);
            free(name);

        } else if (strcmp(tok, "STAT") == 0) {
            /* STAT ext_id subid flag lat1 lon1 lat2 lon2 ... # comment */
            char *t1 = next_token(&p);
            char *t2 = next_token(&p);
            char *t3 = next_token(&p);
            char *slat1 = next_token(&p);
            char *slon1 = next_token(&p);
            char *slat2 = next_token(&p);
            char *slon2 = next_token(&p);
            if (!t1 || !slat1 || !slon1 || !slat2 || !slon2) {
                fprintf(stderr, "line %d: STAT parse error\n", lineno);
                free(t1); free(t2); free(t3); free(slat1); free(slon1); free(slat2); free(slon2);
                free(tok); free(comment);
                continue;
            }
            int ext_id = atoi(t1);
            int lat1 = atoi(slat1), lon1 = atoi(slon1), lat2 = atoi(slat2), lon2 = atoi(slon2);
            int bot_k = -1, bot_h = -1;
            char *remark = NULL;
            if (comment) {
                extract_int_key(comment, "botndypi_kastad", &bot_k);
                extract_int_key(comment, "botndypi_hift", &bot_h);
                trim_inplace(comment);
                if (comment && *comment) remark = strdup_safe(comment);
            }

            int li1 = add_loc(lat1, lon1, "S1");
            int li2 = add_loc(lat2, lon2, "S2");
            int arr_li2[2] = { li1, li2 };
            for (int ai = 0; ai < 2; ++ai) {
                int li = arr_li2[ai];
                if (locs[li].id == -1) {
                    sqlite3_bind_int(st_loc_ins, 1, locs[li].lat);
                    sqlite3_bind_int(st_loc_ins, 2, locs[li].lon);
                    sqlite3_bind_text(st_loc_ins, 3, locs[li].type, -1, SQLITE_STATIC);
                    sqlite3_step(st_loc_ins);
                    sqlite3_reset(st_loc_ins);
                    sqlite3_clear_bindings(st_loc_ins);

                    sqlite3_bind_int(st_loc_sel, 1, locs[li].lat);
                    sqlite3_bind_int(st_loc_sel, 2, locs[li].lon);
                    sqlite3_bind_text(st_loc_sel, 3, locs[li].type, -1, SQLITE_STATIC);
                    if (sqlite3_step(st_loc_sel) == SQLITE_ROW) locs[li].id = sqlite3_column_int(st_loc_sel, 0);
                    sqlite3_reset(st_loc_sel);
                    sqlite3_clear_bindings(st_loc_sel);
                }
            }

            sqlite3_bind_int(st_stat, 1, ext_id);
            sqlite3_bind_int(st_stat, 2, locs[li1].id);
            sqlite3_bind_int(st_stat, 3, locs[li2].id);
            if (remark) sqlite3_bind_text(st_stat, 4, remark, -1, SQLITE_TRANSIENT);
            else sqlite3_bind_null(st_stat, 4);
            if (bot_k >= 0) sqlite3_bind_int(st_stat, 5, bot_k); else sqlite3_bind_null(st_stat, 5);
            if (bot_h >= 0) sqlite3_bind_int(st_stat, 6, bot_h); else sqlite3_bind_null(st_stat, 6);

            if (sqlite3_step(st_stat) != SQLITE_DONE)
                fprintf(stderr, "sqlite insert stat error: %s\n", sqlite3_errmsg(db));
            sqlite3_reset(st_stat);
            sqlite3_clear_bindings(st_stat);

            free(remark);
            free(t1); free(t2); free(t3); free(slat1); free(slon1); free(slat2); free(slon2);

        } else if (strcmp(tok, "WAYP") == 0) {
            char *slat = next_token(&p);
            char *slon = next_token(&p);
            char *sflag = next_token(&p);
            if (!slat || !slon) {
                free(slat); free(slon); free(sflag);
                free(tok); free(comment);
                continue;
            }
            int lat = atoi(slat), lon = atoi(slon);
            int flag = sflag ? atoi(sflag) : 0;
            int li = add_loc(lat, lon, "W");
            if (locs[li].id == -1) {
                sqlite3_bind_int(st_loc_ins, 1, locs[li].lat);
                sqlite3_bind_int(st_loc_ins, 2, locs[li].lon);
                sqlite3_bind_text(st_loc_ins, 3, locs[li].type, -1, SQLITE_STATIC);
                sqlite3_step(st_loc_ins);
                sqlite3_reset(st_loc_ins);
                sqlite3_clear_bindings(st_loc_ins);

                sqlite3_bind_int(st_loc_sel, 1, locs[li].lat);
                sqlite3_bind_int(st_loc_sel, 2, locs[li].lon);
                sqlite3_bind_text(st_loc_sel, 3, locs[li].type, -1, SQLITE_STATIC);
                if (sqlite3_step(st_loc_sel) == SQLITE_ROW) locs[li].id = sqlite3_column_int(st_loc_sel, 0);
                sqlite3_reset(st_loc_sel);
                sqlite3_clear_bindings(st_loc_sel);
            }

            /* insert into waypoints table */
            sqlite3_bind_int(st_wayp, 1, locs[li].id);
            sqlite3_bind_int(st_wayp, 2, flag);
            if (sqlite3_step(st_wayp) != SQLITE_DONE)
                fprintf(stderr, "sqlite insert waypoint error: %s\n", sqlite3_errmsg(db));
            sqlite3_reset(st_wayp);
            sqlite3_clear_bindings(st_wayp);

            free(slat); free(slon); free(sflag);
        } else {
            /* ignore other lines */
        }

        free(tok);
        free(comment);
    }

    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
    fclose(f);

    /* Create a view `v_locations` exposing integer coords and approximate degrees.
       We use a simple conversion lat_deg = lat_int / 10000.0 (approximate).
       This view makes it easy to inspect coordinates from SQL without changing
       the stored integer representation. */
    /* create v_locations that mimics utils.degmin2deg() conversion
       degmin2deg(degmin):
         if (abs(degmin) < 10000) degmin = degmin * 100
         min = (degmin/100) - floor(degmin/10000) * 100
         return (degmin + (200.0/3.0) * min) / 10000
    */
    const char *create_v_locations =
        "CREATE VIEW IF NOT EXISTS v_locations AS "
        "SELECT id, lat_int, lon_int, "
        "( (CASE WHEN ABS(lat_int) < 10000 THEN lat_int*100.0 ELSE lat_int END) + (200.0/3.0) * ( ((CASE WHEN ABS(lat_int) < 10000 THEN lat_int*100.0 ELSE lat_int END)/100.0) - floor((CASE WHEN ABS(lat_int) < 10000 THEN lat_int*100.0 ELSE lat_int END)/10000.0) * 100.0 ) ) / 10000.0 AS lat_deg, "
        "( (CASE WHEN ABS(lon_int) < 10000 THEN lon_int*100.0 ELSE lon_int END) + (200.0/3.0) * ( ((CASE WHEN ABS(lon_int) < 10000 THEN lon_int*100.0 ELSE lon_int END)/100.0) - floor((CASE WHEN ABS(lon_int) < 10000 THEN lon_int*100.0 ELSE lon_int END)/10000.0) * 100.0 ) ) / 10000.0 AS lon_deg, "
        "type FROM locations;";
    char *verr = NULL;
    if (sqlite3_exec(db, create_v_locations, NULL, NULL, &verr) != SQLITE_OK) {
        fprintf(stderr, "failed to create v_locations view: %s\n", verr ? verr : "unknown");
        sqlite3_free(verr);
    }

    /* Create an empty distances table but do NOT compute or insert distances here.
       Distance computation is intentionally left to a separate tool to keep
       parsing fast and to avoid heavy CPU/IO in this step. */
    if (sqlite3_exec(db,
        "BEGIN;"
        "DROP TABLE IF EXISTS distances;"
        "CREATE TABLE distances(i INTEGER NOT NULL, j INTEGER NOT NULL, dist REAL NOT NULL, via_waypoint INTEGER NOT NULL);"
        "CREATE INDEX IF NOT EXISTS distances_idx ON distances(i,j);"
        "COMMIT;",
        NULL, NULL, &err) != SQLITE_OK) {
        fprintf(stderr, "schema error (distances): %s\n", err ? err : "unknown");
        sqlite3_free(err);
    } else {
        fprintf(stderr, "Created empty distances table; distance computation skipped in parser.\n");
    }

    /* distances are empty in this run; no prepared insert is needed here */

    /* Print final counts for main tables to stdout so users can see progress at the end */
    {
        const char *count_qs[] = {
            "SELECT 'locations', COUNT(*) FROM locations;",
            "SELECT 'boats', COUNT(*) FROM boats;",
            "SELECT 'ports', COUNT(*) FROM ports;",
            "SELECT 'stations', COUNT(*) FROM stations;",
            "SELECT 'waypoints', COUNT(*) FROM waypoints;",
            "SELECT 'distances', COUNT(*) FROM distances;",
            NULL
        };
        sqlite3_stmt *qc = NULL;
        for (const char **pq = count_qs; *pq; ++pq) {
            if (sqlite3_prepare_v2(db, *pq, -1, &qc, NULL) == SQLITE_OK) {
                if (sqlite3_step(qc) == SQLITE_ROW) {
                    const unsigned char *label = sqlite3_column_text(qc, 0);
                    long long cnt = sqlite3_column_int64(qc, 1);
                    printf("%s: %lld\n", label ? (const char*)label : "count", (long long)cnt);
                }
                sqlite3_finalize(qc);
                qc = NULL;
            } else {
                /* print error but continue */
                fprintf(stderr, "count query failed: %s\n", sqlite3_errmsg(db));
            }
        }
    }

    sqlite3_finalize(st_loc_ins);
    sqlite3_finalize(st_loc_sel);
    sqlite3_finalize(st_boat);
    sqlite3_finalize(st_port);
    sqlite3_finalize(st_stat);
    sqlite3_finalize(st_wayp);
    sqlite3_close(db);

    for (size_t i = 0; i < locs_n; ++i) free(locs[i].type);
    free(locs);

    printf("Parsed and wrote database: %s\n", dbpath);
    return 0;
}
