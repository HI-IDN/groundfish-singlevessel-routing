/* sols_to_sqlite.c
 * Import paired solution CSVs into a SQLite DB with three tables:
 *   configurations(id, file_prefix, file_summary, file_extra, l2seg, method)
 *   summary(id, config_id, <columns from .cap.csv header...>)   -- data from X.cap.csv
 *   extra(id, config_id, <columns from .csv header...>)       -- data from X.csv
 *
 * Usage:
 *   sols_to_sqlite file1.csv file2.csv ... out.sqlite
 *
 * The program groups input files by a common prefix `X` where filenames are
 * either `X.cap.csv` (summary/detailed) or `X.csv` (extra/aggregate). For each
 * prefix it upserts a row into `configurations` and imports the two files (if
 * present) into `summary` and `extra` respectively, linking rows by config_id.
 *
 * CSV parsing is intentionally simple (no support for embedded newlines in
 * quoted fields). All imported data columns are created as TEXT.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sqlite3.h>
#include <libgen.h>
#include <unistd.h>
#include <math.h>

#define MAX_LINE 32768

static char *basename_of(const char *path) {
    const char *p = path + strlen(path);
    while (p > path && (*p != '/' && *p != '\\')) p--;
    if (*p == '/' || *p == '\\') p++;
    return strdup(p);
}

static int ends_with(const char *s, const char *suf) {
    size_t ls = strlen(s), l = strlen(suf);
    if (ls < l) return 0;
    return strcmp(s + ls - l, suf) == 0;
}

static char *strip_suffix(const char *s, const char *suf) {
    if (!ends_with(s, suf)) return NULL;
    size_t n = strlen(s) - strlen(suf);
    char *r = malloc(n + 1);
    if (!r) return NULL;
    memcpy(r, s, n);
    r[n] = '\0';
    return r;
}

static char *trim_inplace(char *s) {
    if (!s) return s;
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)*(e-1))) *(--e) = '\0';
    return s;
}

static char *strip_quotes_inplace(char *s) {
    if (!s) return s;
    size_t n = strlen(s);
    if (n >= 2 && ((s[0] == '"' && s[n-1] == '"') || (s[0] == '\'' && s[n-1] == '\''))) {
        s[n-1] = '\0';
        return s+1;
    }
    return s;
}

static char *sanitize_colname(const char *in) {
    size_t n = strlen(in);
    char *out = malloc(n + 2);
    if (!out) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)in[i];
        if (isalnum(c) || c == '_') out[j++] = c;
        else out[j++] = '_';
    }
    if (j == 0) { free(out); return strdup("col"); }
    out[j] = '\0';
    return out;
}

/* split a CSV line into malloc'd strings (caller must free each entry and the array)
   naive: supports quoted fields without embedded newlines */
static char **split_csv_line_dup(const char *line, int *out_n) {
    if (!line) { *out_n = 0; return NULL; }
    const char *p = line;
    int cap = 16, cnt = 0;
    char **arr = malloc(cap * sizeof(char*));
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        char *tok = NULL;
        if (*p == '"' || *p == '\'') {
            char q = *p++;
            const char *start = p;
            while (*p && *p != q) p++;
            size_t len = (size_t)(p - start);
            tok = malloc(len + 1);
            if (!tok) break;
            memcpy(tok, start, len);
            tok[len] = '\0';
            if (*p == q) p++;
        } else {
            const char *start = p;
            while (*p && *p != ',') p++;
            size_t len = (size_t)(p - start);
            tok = malloc(len + 1);
            if (!tok) break;
            memcpy(tok, start, len);
            tok[len] = '\0';
        }
        if (*p == ',') p++;
        trim_inplace(tok);
        char *s2 = strip_quotes_inplace(tok);
        char *dup = strdup(s2 ? s2 : "");
        free(tok);
        if (cnt == cap) { cap *= 2; arr = realloc(arr, cap * sizeof(char*)); }
        arr[cnt++] = dup;
    }
    *out_n = cnt;
    return arr;
}

/* structure to hold paired files by prefix */
typedef struct {
    char *prefix;      /* X */
    char *summary;     /* path to X.cap.csv (may be NULL) */
    char *extra;       /* path to X.csv (may be NULL) */
} pair_t;

static int find_prefix(pair_t *arr, int n, const char *pref) {
    for (int i = 0; i < n; ++i) if (strcmp(arr[i].prefix, pref) == 0) return i;
    return -1;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s file1.csv [file2.csv ...] out.sqlite\n", argv[0]);
        return 1;
    }
    const char *out_db = argv[argc-1];
    int nfiles = argc - 2;
    char **files = &argv[1];

    /* Build prefix map */
    pair_t *pairs = NULL; int pcap = 0, pcap_cap = 0;
    for (int i = 0; i < nfiles; ++i) {
        const char *path = files[i];
        char *bname = basename_of(path);
        char *prefix = NULL; int is_summary = 0, is_extra = 0;
        if ((prefix = strip_suffix(bname, ".cap.csv")) != NULL) { is_summary = 1; }
        else if ((prefix = strip_suffix(bname, ".csv")) != NULL) { is_extra = 1; }
        else { free(bname); continue; }
        int idx = find_prefix(pairs, pcap, prefix);
        if (idx == -1) {
            if (pcap == pcap_cap) { pcap_cap = pcap_cap ? pcap_cap*2 : 64; pairs = realloc(pairs, pcap_cap * sizeof(pair_t)); }
            idx = pcap++;
            pairs[idx].prefix = prefix; pairs[idx].summary = NULL; pairs[idx].extra = NULL;
        } else {
            free(prefix);
        }
        if (is_summary) {
            pairs[idx].summary = strdup(path);
        } else if (is_extra) {
            pairs[idx].extra = strdup(path);
        }
        free(bname);
    }

    if (pcap == 0) { fprintf(stderr, "No CSV files found\n"); return 1; }

    /* Find a representative header for summary and extra (first available) */
    char **hdr_summary = NULL; int n_summary_cols = 0;
    char **hdr_extra = NULL; int n_extra_cols = 0;
    for (int i = 0; i < pcap; ++i) {
        if (!hdr_summary && pairs[i].summary) {
            FILE *f = fopen(pairs[i].summary, "r");
            if (f) {
                char line[MAX_LINE]; if (fgets(line, sizeof(line), f)) {
                    size_t L = strlen(line); while (L>0 && (line[L-1]=='\n' || line[L-1]=='\r')) line[--L]='\0';
                    hdr_summary = split_csv_line_dup(line, &n_summary_cols);
                }
                fclose(f);
            }
        }
        if (!hdr_extra && pairs[i].extra) {
            FILE *f = fopen(pairs[i].extra, "r");
            if (f) {
                char line[MAX_LINE]; if (fgets(line, sizeof(line), f)) {
                    size_t L = strlen(line); while (L>0 && (line[L-1]=='\n' || line[L-1]=='\r')) line[--L]='\0';
                    hdr_extra = split_csv_line_dup(line, &n_extra_cols);
                }
                fclose(f);
            }
        }
        if (hdr_summary && hdr_extra) break;
    }

    /* open DB */
    sqlite3 *db = NULL;
    if (sqlite3_open_v2(out_db, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK) {
        fprintf(stderr, "sqlite open %s: %s\n", out_db, sqlite3_errmsg(db)); if (db) sqlite3_close(db); return 1;
    }

    char *err = NULL;
    if (sqlite3_exec(db, "BEGIN;", NULL, NULL, &err) != SQLITE_OK) {
        fprintf(stderr, "begin: %s\n", err?err:"unknown"); sqlite3_free(err);
    }

    /* Build create table SQL */
    {
        size_t bufsz = 2048 + (n_summary_cols + n_extra_cols) * 128;
        char *sql = malloc(bufsz);
        if (!sql) { fprintf(stderr, "OOM\n"); sqlite3_close(db); return 1; }
        /* configurations now includes a version integer (capmut_v<number>_),
           and we add a logs table to store parsed metadata from corresponding .txt files */
        strcpy(sql, "CREATE TABLE IF NOT EXISTS configurations(id INTEGER PRIMARY KEY AUTOINCREMENT, file_prefix TEXT UNIQUE, file_summary TEXT UNIQUE, file_extra TEXT UNIQUE, file_log TEXT UNIQUE, L2seg INTEGER, initialization TEXT, version INTEGER);");
        if (hdr_summary) {
            strcat(sql, "CREATE TABLE IF NOT EXISTS summary(id INTEGER PRIMARY KEY AUTOINCREMENT, config_id INTEGER");
            for (int i = 0; i < n_summary_cols; ++i) {
                char *nm = sanitize_colname(hdr_summary[i]); strcat(sql, ", "); strcat(sql, nm); strcat(sql, " TEXT"); free(nm);
            }
            strcat(sql, ", FOREIGN KEY(config_id) REFERENCES configurations(id));");
        }
        if (hdr_extra) {
            strcat(sql, "CREATE TABLE IF NOT EXISTS extra(id INTEGER PRIMARY KEY AUTOINCREMENT, config_id INTEGER");
            for (int i = 0; i < n_extra_cols; ++i) {
                char *nm = sanitize_colname(hdr_extra[i]); strcat(sql, ", "); strcat(sql, nm); strcat(sql, " TEXT"); free(nm);
            }
            strcat(sql, ", FOREIGN KEY(config_id) REFERENCES configurations(id));");
        }
        /* logs table to hold parsed metadata + raw text for inspection */
        strcat(sql, "CREATE TABLE IF NOT EXISTS logs(id INTEGER PRIMARY KEY AUTOINCREMENT, config_id INTEGER, ship TEXT, shipcap INTEGER, initial_noport REAL, progress_pass INTEGER, progress_changed INTEGER, progress_total REAL, progress_best REAL, best_obj REAL, best_bound REAL, gap REAL, time_limit_reached INTEGER, seed INTEGER, initial_station_order TEXT, raw TEXT, FOREIGN KEY(config_id) REFERENCES configurations(id));");
        if (sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK) {
            fprintf(stderr, "create tables error: %s\n", err ? err : sqlite3_errmsg(db)); sqlite3_free(err); free(sql); sqlite3_close(db); return 1;
        }
        free(sql);
    }

    /* Prepare insert/lookup statements */
    sqlite3_stmt *st_conf_sel = NULL, *st_conf_ins = NULL, *st_conf_upd = NULL;
    if (sqlite3_prepare_v2(db, "SELECT id, file_summary, file_extra, file_log FROM configurations WHERE file_prefix = ?;", -1, &st_conf_sel, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(db, "INSERT INTO configurations(file_prefix, file_summary, file_extra, file_log, L2seg, initialization, version) VALUES(?,?,?,?,?,?,?);", -1, &st_conf_ins, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(db, "UPDATE configurations SET file_summary = COALESCE(file_summary, ?), file_extra = COALESCE(file_extra, ?), file_log = COALESCE(file_log, ?), L2seg = COALESCE(L2seg, ?), initialization = COALESCE(initialization, ?), version = COALESCE(version, ?) WHERE id = ?;", -1, &st_conf_upd, NULL) != SQLITE_OK) {
        fprintf(stderr, "prepare config stmts failed: %s\n", sqlite3_errmsg(db)); sqlite3_close(db); return 1;
    }

    /* prepare summary and extra inserts if headers exist */
    sqlite3_stmt *st_sum_ins = NULL, *st_ext_ins = NULL;
    if (hdr_summary) {
        size_t bufsz = 512 + n_summary_cols * 32; char *q = malloc(bufsz);
        strcpy(q, "INSERT INTO summary(config_id");
        for (int i = 0; i < n_summary_cols; ++i) {
            char *nm = sanitize_colname(hdr_summary[i]); strcat(q, ", "); strcat(q, nm); free(nm);
        }
        strcat(q, ") VALUES(?"); for (int i = 0; i < n_summary_cols; ++i) strcat(q, ",?"); strcat(q, ");");
        if (sqlite3_prepare_v2(db, q, -1, &st_sum_ins, NULL) != SQLITE_OK) { fprintf(stderr, "prepare summary insert failed: %s\n", sqlite3_errmsg(db)); free(q); sqlite3_close(db); return 1; }
        free(q);
    }
    if (hdr_extra) {
        size_t bufsz = 512 + n_extra_cols * 32; char *q = malloc(bufsz);
        strcpy(q, "INSERT INTO extra(config_id");
        for (int i = 0; i < n_extra_cols; ++i) {
            char *nm = sanitize_colname(hdr_extra[i]); strcat(q, ", "); strcat(q, nm); free(nm);
        }
        strcat(q, ") VALUES(?"); for (int i = 0; i < n_extra_cols; ++i) strcat(q, ",?"); strcat(q, ");");
        if (sqlite3_prepare_v2(db, q, -1, &st_ext_ins, NULL) != SQLITE_OK) { fprintf(stderr, "prepare extra insert failed: %s\n", sqlite3_errmsg(db)); free(q); sqlite3_close(db); return 1; }
        free(q);
    }

    /* prepare log insert */
    sqlite3_stmt *st_log_ins = NULL;
    {
        const char *log_q = "INSERT INTO logs(config_id, ship, shipcap, initial_noport, progress_pass, progress_changed, progress_total, progress_best, best_obj, best_bound, gap, time_limit_reached, seed, initial_station_order, raw) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);";
        if (sqlite3_prepare_v2(db, log_q, -1, &st_log_ins, NULL) != SQLITE_OK) {
            fprintf(stderr, "prepare log insert failed: %s\n", sqlite3_errmsg(db)); sqlite3_close(db); return 1;
        }
    }

    /* For each prefix, upsert configuration and import files */
    for (int i = 0; i < pcap; ++i) {
        const char *pref = pairs[i].prefix;
        const char *sum_path = pairs[i].summary;
        const char *ext_path = pairs[i].extra;

        int config_id = -1;
        /* parse L2seg, initialization, and version from prefix (or from filenames)
           Use a generic digit-based detection (no explicit enumeration):
           - prefer trailing digits (after the last '_')
           - else search for the last occurrence of '_<digits>' anywhere in the prefix
           This will capture values like 60,360, etc., without hardcoding a list. */
        int l2seg = -1;
        int len_pref = (int)strlen(pref);
        /* 1) try trailing digits after last underscore */
        const char *last_us = strrchr(pref, '_');
        if (last_us) {
            const char *pnum = last_us + 1;
            if (*pnum != '\0') {
                int all_digits = 1;
                for (const char *q = pnum; *q; ++q) if (!isdigit((unsigned char)*q)) { all_digits = 0; break; }
                if (all_digits) {
                    l2seg = atoi(pnum);
                }
            }
        }
        /* 2) fallback: find the last occurrence of '_' followed by digits anywhere in the prefix */
        if (l2seg == -1) {
            int best_val = -1;
            for (int i = 0; i < len_pref; ++i) {
                if (pref[i] == '_' && i + 1 < len_pref && isdigit((unsigned char)pref[i+1])) {
                    int j = i + 1;
                    while (j < len_pref && isdigit((unsigned char)pref[j])) j++;
                    int nd = j - (i + 1);
                    if (nd > 0 && nd < 10) {
                        char numbuf[16]; int n = nd < (int)sizeof(numbuf)-1 ? nd : (int)sizeof(numbuf)-1;
                        memcpy(numbuf, &pref[i+1], n); numbuf[n] = '\0';
                        int val = atoi(numbuf);
                        best_val = val; /* keep last found */
                    }
                }
            }
            if (best_val >= 0) l2seg = best_val;
        }
        const char *initialization = "NP";
        if ((sum_path && strstr(sum_path, "cheapest")) || (ext_path && strstr(ext_path, "cheapest")) || strstr(pref, "cheapest")) initialization = "CI";
        else if ((sum_path && (strstr(sum_path, "greedy_edge")||strstr(sum_path,"greedy-edge")||strstr(sum_path,"greedyedge"))) || (ext_path && (strstr(ext_path, "greedy_edge")||strstr(ext_path,"greedy-edge")||strstr(ext_path,"greedyedge"))) || strstr(pref, "greedy_edge")) initialization = "GE";
        /* version: look for capmut_v<number>_ in prefix */
        int version = -1;
        const char *pver = strstr(pref, "capmut_v");
        if (pver) {
            pver += strlen("capmut_v");
            char numbuf[32]; int ni = 0;
            while (*pver && isdigit((unsigned char)*pver) && ni < (int)sizeof(numbuf)-1) numbuf[ni++] = *pver++;
            numbuf[ni] = '\0';
            if (ni > 0) version = atoi(numbuf);
        }

        /* detect a matching .txt/.log for this prefix so we can store it in configurations.file_log */
        char *file_log_path = NULL;
        {
            char log_candidate[1024];
            int found_log = 0;
            char *repo_dir = NULL;
            if (sum_path) {
                char *copy = strdup(sum_path);
                char *d = dirname(copy);
                if (d) {
                    char *d2 = strdup(d);
                    char *p = dirname(d2);
                    if (p) repo_dir = strdup(p);
                    free(d2);
                }
                free(copy);
            } else if (ext_path) {
                char *copy = strdup(ext_path);
                char *d = dirname(copy);
                if (d) {
                    char *d2 = strdup(d);
                    char *p = dirname(d2);
                    if (p) repo_dir = strdup(p);
                    free(d2);
                }
                free(copy);
            }
            if (repo_dir) {
                snprintf(log_candidate, sizeof(log_candidate), "%s/%s.txt", repo_dir, pref);
                if (access(log_candidate, F_OK) == 0) found_log = 1;
                else {
                    snprintf(log_candidate, sizeof(log_candidate), "%s/%s.log", repo_dir, pref);
                    if (access(log_candidate, F_OK) == 0) found_log = 1;
                }
                free(repo_dir);
            }
            if (!found_log) {
                snprintf(log_candidate, sizeof(log_candidate), "%s.txt", pref);
                if (access(log_candidate, F_OK) == 0) found_log = 1;
            }
            if (found_log) file_log_path = strdup(log_candidate);
        }

        /* lookup by prefix */
        sqlite3_bind_text(st_conf_sel, 1, pref, -1, SQLITE_STATIC);
        if (sqlite3_step(st_conf_sel) == SQLITE_ROW) {
            config_id = sqlite3_column_int(st_conf_sel, 0);
        }
        sqlite3_reset(st_conf_sel);
        sqlite3_clear_bindings(st_conf_sel);

        if (config_id == -1) {
            /* insert new configuration */
            sqlite3_bind_text(st_conf_ins, 1, pref, -1, SQLITE_STATIC);
            if (sum_path) sqlite3_bind_text(st_conf_ins, 2, sum_path, -1, SQLITE_STATIC); else sqlite3_bind_null(st_conf_ins, 2);
            if (ext_path) sqlite3_bind_text(st_conf_ins, 3, ext_path, -1, SQLITE_STATIC); else sqlite3_bind_null(st_conf_ins, 3);
            if (file_log_path) sqlite3_bind_text(st_conf_ins, 4, file_log_path, -1, SQLITE_STATIC); else sqlite3_bind_null(st_conf_ins, 4);
            if (l2seg > 0) sqlite3_bind_int(st_conf_ins, 5, l2seg); else sqlite3_bind_null(st_conf_ins, 5);
            sqlite3_bind_text(st_conf_ins, 6, initialization, -1, SQLITE_STATIC);
            if (version >= 0) sqlite3_bind_int(st_conf_ins, 7, version); else sqlite3_bind_null(st_conf_ins, 7);
            if (sqlite3_step(st_conf_ins) != SQLITE_DONE) fprintf(stderr, "insert config error: %s\n", sqlite3_errmsg(db));
            sqlite3_reset(st_conf_ins);
            sqlite3_clear_bindings(st_conf_ins);
            config_id = (int)sqlite3_last_insert_rowid(db);
        } else {
            /* update existing row to fill missing file_summary/file_extra/l2seg/method if needed */
            sqlite3_bind_text(st_conf_upd, 1, sum_path ? sum_path : "", -1, SQLITE_STATIC);
            sqlite3_bind_text(st_conf_upd, 2, ext_path ? ext_path : "", -1, SQLITE_STATIC);
            sqlite3_bind_text(st_conf_upd, 3, file_log_path ? file_log_path : "", -1, SQLITE_STATIC);
            if (l2seg > 0) sqlite3_bind_int(st_conf_upd, 4, l2seg); else sqlite3_bind_null(st_conf_upd, 4);
            sqlite3_bind_text(st_conf_upd, 5, initialization, -1, SQLITE_STATIC);
            if (version >= 0) sqlite3_bind_int(st_conf_upd, 6, version); else sqlite3_bind_null(st_conf_upd, 6);
            sqlite3_bind_int(st_conf_upd, 7, config_id);
            if (sqlite3_step(st_conf_upd) != SQLITE_DONE) fprintf(stderr, "update config error: %s\n", sqlite3_errmsg(db));
            sqlite3_reset(st_conf_upd);
            sqlite3_clear_bindings(st_conf_upd);
        }

        /* import summary (.cap.csv) */
        if (sum_path && st_sum_ins) {
            FILE *f = fopen(sum_path, "r");
            if (!f) { fprintf(stderr, "warning: cannot open %s\n", sum_path); }
            else {
                char buf[MAX_LINE];
                /* read & ignore header */
                if (!fgets(buf, sizeof(buf), f)) { fclose(f); }
                else {
                    while (fgets(buf, sizeof(buf), f)) {
                        size_t L2 = strlen(buf); while (L2>0 && (buf[L2-1]=='\n' || buf[L2-1]=='\r')) buf[--L2]='\0';
                        int toks = 0; char **vals = split_csv_line_dup(buf, &toks);
                        sqlite3_bind_int(st_sum_ins, 1, config_id);
                        for (int c = 0; c < n_summary_cols; ++c) {
                            if (c < toks && vals[c] && vals[c][0] != '\0') sqlite3_bind_text(st_sum_ins, 2 + c, vals[c], -1, SQLITE_TRANSIENT);
                            else sqlite3_bind_null(st_sum_ins, 2 + c);
                        }
                        if (sqlite3_step(st_sum_ins) != SQLITE_DONE) fprintf(stderr, "summary insert error: %s\n", sqlite3_errmsg(db));
                        sqlite3_reset(st_sum_ins);
                        for (int c = 0; c < toks; ++c) free(vals[c]); free(vals);
                    }
                    fclose(f);
                }
            }
        }

        /* import extra (.csv) */
        if (ext_path && st_ext_ins) {
            FILE *f = fopen(ext_path, "r");
            if (!f) { fprintf(stderr, "warning: cannot open %s\n", ext_path); }
            else {
                char buf[MAX_LINE];
                if (!fgets(buf, sizeof(buf), f)) { fclose(f); }
                else {
                    while (fgets(buf, sizeof(buf), f)) {
                        size_t L2 = strlen(buf); while (L2>0 && (buf[L2-1]=='\n' || buf[L2-1]=='\r')) buf[--L2]='\0';
                        int toks = 0; char **vals = split_csv_line_dup(buf, &toks);
                        sqlite3_bind_int(st_ext_ins, 1, config_id);
                        for (int c = 0; c < n_extra_cols; ++c) {
                            if (c < toks && vals[c] && vals[c][0] != '\0') sqlite3_bind_text(st_ext_ins, 2 + c, vals[c], -1, SQLITE_TRANSIENT);
                            else sqlite3_bind_null(st_ext_ins, 2 + c);
                        }
                        if (sqlite3_step(st_ext_ins) != SQLITE_DONE) fprintf(stderr, "extra insert error: %s\n", sqlite3_errmsg(db));
                        sqlite3_reset(st_ext_ins);
                        for (int c = 0; c < toks; ++c) free(vals[c]); free(vals);
                    }
                    fclose(f);
                }
            }
        }

        /* parse corresponding .txt log if present: look in repo dir (parent of sol) */
        if (file_log_path && st_log_ins) {
            FILE *lf = fopen(file_log_path, "r");
            if (lf) {
                // parse simple key values and capture raw
                char *raw = NULL; size_t rawcap = 0; size_t rawlen = 0;
                char lbuf[MAX_LINE];
                const char *ship = NULL; int shipcap = -1; double initial_noport = NAN;
                int prog_pass = -1, prog_changed = -1; double prog_total = NAN, prog_best = NAN;
                double best_obj = NAN, best_bound = NAN, gap = NAN; int time_limit = 0; long seed = -1;
                char *init_order = NULL;
                while (fgets(lbuf, sizeof(lbuf), lf)) {
                    size_t L = strlen(lbuf);
                    if (rawlen + L + 1 > rawcap) { rawcap = (rawlen + L + 1) * 2; raw = realloc(raw, rawcap); }
                    memcpy(raw + rawlen, lbuf, L); rawlen += L; raw[rawlen] = '\0';
                    // trim
                    char t[4096]; strncpy(t, lbuf, sizeof(t)-1); t[sizeof(t)-1] = '\0';
                    char *s = trim_inplace(t);
                    if (strncmp(s, "Ship:", 5) == 0) {
                        char *v = s + 5; trim_inplace(v); ship = strdup(v);
                    } else if (strncmp(s, "ShipCap:", 8) == 0) {
                        shipcap = atoi(s + 8);
                    } else if (strncmp(s, "Initial no-port objective:", 27) == 0) {
                        initial_noport = atof(s + 27);
                    } else if (strncmp(s, "Initial station order", 21) == 0) {
                        // capture the rest of the line (may be long)
                        char *colon = strchr(lbuf, ':'); if (colon) { char *v = trim_inplace(colon+1); init_order = strdup(v); }
                    } else if (strstr(s, "PROGRESS pass=")) {
                        // parse key=val pairs
                        char *p = strstr(s, "PROGRESS"); if (p) {
                            char *q = p; // move through
                            int local_pass = -1, local_changed=-1; double local_total=NAN, local_best=NAN;
                            char *tok = strtok(q, " =\t\n");
                            while (tok) {
                                if (strncmp(tok, "pass", 4) == 0) { tok = strtok(NULL, " =\t\n"); if (tok) local_pass = atoi(tok); }
                                else if (strncmp(tok, "changed", 7) == 0) { tok = strtok(NULL, " =\t\n"); if (tok) local_changed = atoi(tok); }
                                else if (strncmp(tok, "total", 5) == 0) { tok = strtok(NULL, " =\t\n"); if (tok) local_total = atof(tok); }
                                else if (strncmp(tok, "best", 4) == 0) { tok = strtok(NULL, " =\t\n"); if (tok) local_best = atof(tok); }
                                else tok = strtok(NULL, " =\t\n");
                            }
                            // store last seen progress
                            if (!isnan(local_total)) prog_total = local_total;
                            if (!isnan(local_best)) prog_best = local_best;
                            if (local_pass >= 0) prog_pass = local_pass;
                            if (local_changed >= 0) prog_changed = local_changed;
                        }
                    } else if (strstr(s, "Best objective") && strstr(s, "best bound")) {
                        // Best objective 5.590860532070e+02, best bound 5.328002888094e+02, gap 4.7016%
                        double bo=NAN, bb=NAN, g=NAN;
                        if (sscanf(s, "Best objective %lf, best bound %lf, gap %lf%%", &bo, &bb, &g) == 3) { best_obj = bo; best_bound = bb; gap = g; }
                    } else if (strstr(s, "Time limit reached") || strstr(s, "TimeLimit reached") ) {
                        time_limit = 1;
                    } else if (strstr(s, "Set parameter Seed to value")) {
                        char *p = strstr(s, "Set parameter Seed to value"); if (p) { seed = atol(p + strlen("Set parameter Seed to value")); }
                    }
                }
                fclose(lf);
                // insert into logs
                sqlite3_bind_int(st_log_ins, 1, config_id);
                sqlite3_bind_text(st_log_ins, 2, ship, -1, SQLITE_TRANSIENT);
                if (shipcap >= 0) sqlite3_bind_int(st_log_ins, 3, shipcap); else sqlite3_bind_null(st_log_ins, 3);
                if (!isnan(initial_noport)) sqlite3_bind_double(st_log_ins, 4, initial_noport); else sqlite3_bind_null(st_log_ins, 4);
                if (prog_pass >= 0) sqlite3_bind_int(st_log_ins, 5, prog_pass); else sqlite3_bind_null(st_log_ins, 5);
                if (prog_changed >= 0) sqlite3_bind_int(st_log_ins, 6, prog_changed); else sqlite3_bind_null(st_log_ins, 6);
                if (!isnan(prog_total)) sqlite3_bind_double(st_log_ins, 7, prog_total); else sqlite3_bind_null(st_log_ins, 7);
                if (!isnan(prog_best)) sqlite3_bind_double(st_log_ins, 8, prog_best); else sqlite3_bind_null(st_log_ins, 8);
                if (!isnan(best_obj)) sqlite3_bind_double(st_log_ins, 9, best_obj); else sqlite3_bind_null(st_log_ins, 9);
                if (!isnan(best_bound)) sqlite3_bind_double(st_log_ins, 10, best_bound); else sqlite3_bind_null(st_log_ins, 10);
                if (!isnan(gap)) sqlite3_bind_double(st_log_ins, 11, gap); else sqlite3_bind_null(st_log_ins, 11);
                sqlite3_bind_int(st_log_ins, 12, time_limit);
                if (seed >= 0) sqlite3_bind_int64(st_log_ins, 13, seed); else sqlite3_bind_null(st_log_ins, 13);
                if (init_order) sqlite3_bind_text(st_log_ins, 14, init_order, -1, SQLITE_TRANSIENT); else sqlite3_bind_null(st_log_ins, 14);
                if (raw && rawlen>0) sqlite3_bind_text(st_log_ins, 15, raw, -1, SQLITE_TRANSIENT); else sqlite3_bind_null(st_log_ins, 15);
                if (sqlite3_step(st_log_ins) != SQLITE_DONE) fprintf(stderr, "log insert error: %s\n", sqlite3_errmsg(db));
                sqlite3_reset(st_log_ins);
                if (ship) free((void*)ship); if (init_order) free(init_order); if (raw) free(raw);
            }
        }

        if (file_log_path) free((void*)file_log_path);
    }

    /* finalize & commit */
    if (st_sum_ins) sqlite3_finalize(st_sum_ins);
    if (st_ext_ins) sqlite3_finalize(st_ext_ins);
    if (st_log_ins) sqlite3_finalize(st_log_ins);
    sqlite3_finalize(st_conf_sel); sqlite3_finalize(st_conf_ins); sqlite3_finalize(st_conf_upd);

    if (sqlite3_exec(db, "COMMIT;", NULL, NULL, &err) != SQLITE_OK) {
        fprintf(stderr, "commit error: %s\n", err ? err : sqlite3_errmsg(db)); sqlite3_free(err);
    }
    sqlite3_close(db);

    /* cleanup */
    for (int i = 0; i < pcap; ++i) {
        free(pairs[i].prefix); if (pairs[i].summary) free(pairs[i].summary); if (pairs[i].extra) free(pairs[i].extra);
    }
    free(pairs);
    if (hdr_summary) { for (int i = 0; i < n_summary_cols; ++i) free(hdr_summary[i]); free(hdr_summary); }
    if (hdr_extra) { for (int i = 0; i < n_extra_cols; ++i) free(hdr_extra[i]); free(hdr_extra); }
    return 0;
}

