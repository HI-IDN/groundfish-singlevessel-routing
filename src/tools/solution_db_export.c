#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#endif

#define SQL_CHECK(db, rc, label) do { \
    if ((rc) != SQLITE_OK && (rc) != SQLITE_DONE && (rc) != SQLITE_ROW) { \
        fprintf(stderr, "%s: %s\n", (label), sqlite3_errmsg(db)); \
        return 0; \
    } \
} while (0)

typedef struct {
    sqlite3 *db;
    const char *sol_dir;
    int run_count;
} export_ctx_t;

static int is_dir_path(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int has_suffix(const char *s, const char *suffix) {
    size_t n = strlen(s), m = strlen(suffix);
    return n >= m && strcmp(s + n - m, suffix) == 0;
}

static char *read_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    long len;
    char *buf;
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    len = ftell(fp);
    if (len < 0) { fclose(fp); return NULL; }
    rewind(fp);
    buf = (char*)malloc((size_t)len + 1);
    if (!buf) { fclose(fp); return NULL; }
    if (fread(buf, 1, (size_t)len, fp) != (size_t)len) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    buf[len] = '\0';
    fclose(fp);
    return buf;
}

static const char *base_name(const char *path) {
    const char *a = strrchr(path, '/');
    const char *b = strrchr(path, '\\');
    const char *p = a > b ? a : b;
    return p ? p + 1 : path;
}

static void strip_json_stem(const char *name, char *out, size_t out_sz) {
    snprintf(out, out_sz, "%s", name);
    char *dot = strrchr(out, '.');
    if (dot) *dot = '\0';
}

static int path_contains(const char *path, const char *needle) {
    return strstr(path, needle) != NULL;
}

static int method_from_path(const char *path, char *out, size_t out_sz) {
    const char *methods[] = {"noport", "fixedport", "nn", "ge", "ci", "lkh"};
    for (size_t i = 0; i < sizeof(methods) / sizeof(methods[0]); i++) {
        char pat1[64], pat2[64];
        snprintf(pat1, sizeof(pat1), "/%s/", methods[i]);
        snprintf(pat2, sizeof(pat2), "\\%s\\", methods[i]);
        if (path_contains(path, pat1) || path_contains(path, pat2)) {
            snprintf(out, out_sz, "%s", methods[i]);
            return 1;
        }
    }
    return 0;
}

static int phase_from_file(const char *path, char *out, size_t out_sz) {
    char stem[128];
    strip_json_stem(base_name(path), stem, sizeof(stem));
    if (strcmp(stem, "construction") == 0) {
        snprintf(out, out_sz, "construction");
        return 1;
    }
    if (strcmp(stem, "segment") == 0) {
        snprintf(out, out_sz, "segmentation");
        return 1;
    }
    if (strcmp(stem, "refinement") == 0 || strncmp(stem, "refinement_", 11) == 0) {
        snprintf(out, out_sz, "refinement");
        return 1;
    }
    return 0;
}

static int l2seg_from_file_or_json(sqlite3 *db, const char *path, const char *json) {
    char stem[128];
    strip_json_stem(base_name(path), stem, sizeof(stem));
    if (strncmp(stem, "refinement_", 11) == 0) {
        return atoi(stem + 11);
    }

    sqlite3_stmt *stmt = NULL;
    int value = -1;
    const char *sql =
        "SELECT COALESCE("
        "json_extract(?1,'$.metadata.l2seg_time_limit_seconds'),"
        "json_extract(?1,'$.metadata.mip_time_limit_seconds'))";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, json, -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
        value = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return value;
}

static char *scalar_text(sqlite3 *db, const char *json, const char *path) {
    sqlite3_stmt *stmt = NULL;
    char *out = NULL;
    if (sqlite3_prepare_v2(db, "SELECT json_extract(?1, ?2)", -1, &stmt, NULL) != SQLITE_OK) return NULL;
    sqlite3_bind_text(stmt, 1, json, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, path, -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
        const unsigned char *txt = sqlite3_column_text(stmt, 0);
        if (txt) out = strdup((const char*)txt);
    }
    sqlite3_finalize(stmt);
    return out;
}

static int scalar_int(sqlite3 *db, const char *json, const char *path, int fallback) {
    sqlite3_stmt *stmt = NULL;
    int out = fallback;
    if (sqlite3_prepare_v2(db, "SELECT json_extract(?1, ?2)", -1, &stmt, NULL) != SQLITE_OK) return fallback;
    sqlite3_bind_text(stmt, 1, json, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, path, -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
        out = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return out;
}

static double scalar_double(sqlite3 *db, const char *json, const char *path, double fallback) {
    sqlite3_stmt *stmt = NULL;
    double out = fallback;
    if (sqlite3_prepare_v2(db, "SELECT json_extract(?1, ?2)", -1, &stmt, NULL) != SQLITE_OK) return fallback;
    sqlite3_bind_text(stmt, 1, json, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, path, -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
        out = sqlite3_column_double(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return out;
}

static char *final_variant(sqlite3 *db, const char *json) {
    char *v = scalar_text(db, json, "$.summary.status.final");
    if (v) return v;
    v = scalar_text(db, json, "$.summary.final");
    if (v) return v;

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, "SELECT key FROM json_each(?1,'$.solution') LIMIT 1", -1, &stmt, NULL) != SQLITE_OK) {
        return NULL;
    }
    sqlite3_bind_text(stmt, 1, json, -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *txt = sqlite3_column_text(stmt, 0);
        if (txt) v = strdup((const char*)txt);
    }
    sqlite3_finalize(stmt);
    return v;
}

static int exec_sql(sqlite3 *db, const char *sql) {
    char *err = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err ? err : sqlite3_errmsg(db));
        sqlite3_free(err);
        return 0;
    }
    return 1;
}

static int create_schema(sqlite3 *db) {
    return exec_sql(db,
        "PRAGMA foreign_keys=ON;"
        "CREATE TABLE runs ("
        " run_id TEXT PRIMARY KEY,"
        " method TEXT NOT NULL,"
        " phase TEXT NOT NULL,"
        " solution_key TEXT NOT NULL,"
        " parent_run_id TEXT,"
        " is_final INTEGER NOT NULL DEFAULT 0,"
        " l1seg_timeout_seconds INTEGER,"
        " l2seg_timeout_seconds INTEGER,"
        " timeout_seconds INTEGER,"
        " n_segments INTEGER,"
        " feasible INTEGER,"
        " boat_id INTEGER,"
        " boat_name TEXT,"
        " runtime_seconds REAL,"
        " source_json TEXT NOT NULL,"
        " created_at TEXT NOT NULL"
        ");"
        "CREATE TABLE location_segments ("
        " run_id TEXT NOT NULL,"
        " segment INTEGER NOT NULL,"
        " sequence INTEGER NOT NULL,"
        " location_id INTEGER NOT NULL,"
        " point_type TEXT,"
        " PRIMARY KEY(run_id,segment,sequence),"
        " FOREIGN KEY(run_id) REFERENCES runs(run_id) ON DELETE CASCADE"
        ");"
        "CREATE VIEW route_locations AS SELECT * FROM location_segments;"
        "CREATE TABLE station_segments ("
        " run_id TEXT NOT NULL,"
        " segment INTEGER NOT NULL,"
        " sequence INTEGER NOT NULL,"
        " signed_station_id INTEGER NOT NULL,"
        " station_id INTEGER NOT NULL,"
        " PRIMARY KEY(run_id,segment,sequence),"
        " FOREIGN KEY(run_id) REFERENCES runs(run_id) ON DELETE CASCADE"
        ");"
        "CREATE TABLE distance ("
        " run_id TEXT NOT NULL,"
        " segment INTEGER,"
        " transit_nm REAL,"
        " total_nm REAL,"
        " FOREIGN KEY(run_id) REFERENCES runs(run_id) ON DELETE CASCADE"
        ");"
        "CREATE TABLE mip_solves ("
        " phase_code TEXT,"
        " segment_model TEXT,"
        " station_count INTEGER,"
        " node_count INTEGER,"
        " model_variable_count INTEGER,"
        " model_constraint_count INTEGER,"
        " runtime_seconds REAL,"
        " gap_percent REAL"
        ");"
        "CREATE TABLE refinement_passes ("
        " run_id TEXT NOT NULL,"
        " solution_run_id TEXT NOT NULL,"
        " pass_number INTEGER NOT NULL,"
        " changed INTEGER,"
        " stations_moved INTEGER,"
        " boundary_attempts INTEGER,"
        " boundary_changes INTEGER,"
        " mip_solves INTEGER,"
        " runtime_seconds REAL,"
        " PRIMARY KEY(run_id,pass_number),"
        " FOREIGN KEY(solution_run_id) REFERENCES runs(run_id) ON DELETE CASCADE"
        ");"
        "CREATE TABLE refinement_solve_context ("
        " run_id TEXT NOT NULL,"
        " pass_number INTEGER NOT NULL,"
        " solve_idx INTEGER NOT NULL,"
        " segment_model TEXT NOT NULL,"
        " timeout_seconds REAL,"
        " boundary_index INTEGER,"
        " candidate_split_index INTEGER,"
        " segment INTEGER,"
        " moved_stations INTEGER,"
        " FOREIGN KEY(run_id,pass_number) REFERENCES refinement_passes(run_id,pass_number) ON DELETE CASCADE"
        ");"
        "CREATE TABLE refinement_station_mutations ("
        " run_id TEXT NOT NULL,"
        " pass_number INTEGER NOT NULL,"
        " segment INTEGER NOT NULL,"
        " sequence INTEGER NOT NULL,"
        " signed_station_id INTEGER NOT NULL,"
        " station_id INTEGER NOT NULL,"
        " PRIMARY KEY(run_id,pass_number,segment,sequence),"
        " FOREIGN KEY(run_id,pass_number) REFERENCES refinement_passes(run_id,pass_number) ON DELETE CASCADE"
        ");"
        "CREATE TABLE refinement_station_moves ("
        " run_id TEXT NOT NULL,"
        " pass_number INTEGER NOT NULL,"
        " station_id INTEGER NOT NULL,"
        " from_segment INTEGER NOT NULL,"
        " to_segment INTEGER NOT NULL,"
        " PRIMARY KEY(run_id,pass_number,station_id),"
        " FOREIGN KEY(run_id,pass_number) REFERENCES refinement_passes(run_id,pass_number) ON DELETE CASCADE"
        ");"
        "CREATE TABLE refinement_unique_station_moves ("
        " run_id TEXT NOT NULL,"
        " station_id INTEGER NOT NULL,"
        " initial_segment INTEGER NOT NULL,"
        " final_segment INTEGER NOT NULL,"
        " PRIMARY KEY(run_id,station_id)"
        ");"
        "CREATE TABLE refinement_summary ("
        " run_id TEXT PRIMARY KEY,"
        " init_run_id TEXT NOT NULL,"
        " final_run_id TEXT NOT NULL,"
        " moved_stations INTEGER NOT NULL,"
        " unique_moved_stations INTEGER NOT NULL"
        ");"
        "CREATE INDEX idx_runs_method_phase ON runs(method,phase);"
        "CREATE INDEX idx_runs_parent ON runs(parent_run_id);"
        "CREATE INDEX idx_location_segments_run ON location_segments(run_id,segment,sequence);"
        "CREATE INDEX idx_station_segments_run ON station_segments(run_id,segment,sequence);"
        "CREATE INDEX idx_distance_run ON distance(run_id,segment);"
        "CREATE INDEX idx_mip_solves_kind ON mip_solves(phase_code,segment_model);"
        "CREATE INDEX idx_refinement_passes_solution ON refinement_passes(solution_run_id);"
        "CREATE INDEX idx_refinement_solve_context_run ON refinement_solve_context(run_id,pass_number);"
        "CREATE INDEX idx_refinement_station_mutations_run ON refinement_station_mutations(run_id,pass_number,segment);"
        "CREATE INDEX idx_refinement_station_moves_run ON refinement_station_moves(run_id,pass_number,to_segment);"
        "CREATE INDEX idx_refinement_unique_station_moves_run ON refinement_unique_station_moves(run_id,final_segment);");
}

static int fix_lineage(sqlite3 *db) {
    return exec_sql(db,
        "UPDATE runs AS r "
        "SET parent_run_id = ("
        "  SELECT c.run_id FROM runs AS c "
        "  WHERE c.method = r.method AND c.phase = 'construction' AND c.is_final = 1 "
        "  LIMIT 1"
        ") "
        "WHERE r.phase = 'segmentation';"
        "UPDATE runs AS r "
        "SET parent_run_id = ("
        "  SELECT s.run_id FROM runs AS s "
        "  WHERE s.method = r.method AND s.phase = 'segmentation' AND s.is_final = 1 "
        "  LIMIT 1"
        ") "
        "WHERE r.phase = 'refinement' AND r.solution_key = 'init';");
}

static int populate_refinement_move_tables(sqlite3 *db) {
    return exec_sql(db,
        "INSERT INTO refinement_station_moves(run_id,pass_number,station_id,from_segment,to_segment) "
        "SELECT p.run_id, p.pass_number, curr.station_id, prev.segment, curr.segment "
        "FROM refinement_passes p "
        "JOIN runs r ON r.run_id = p.solution_run_id "
        "JOIN station_segments curr ON curr.run_id = p.solution_run_id "
        "JOIN station_segments prev ON prev.run_id = r.parent_run_id AND prev.station_id = curr.station_id "
        "WHERE p.pass_number > 0 "
        "AND r.parent_run_id IS NOT NULL "
        "AND curr.segment <> prev.segment;"
        "INSERT INTO refinement_unique_station_moves(run_id,station_id,initial_segment,final_segment) "
        "WITH final_pass AS ("
        "  SELECT p.run_id, init.solution_run_id AS init_run_id, p.solution_run_id AS final_run_id "
        "  FROM refinement_passes p "
        "  JOIN refinement_passes init ON init.run_id = p.run_id AND init.pass_number = 0 "
        "  WHERE p.pass_number = (SELECT MAX(p2.pass_number) FROM refinement_passes p2 WHERE p2.run_id = p.run_id)"
        ") "
        "SELECT fp.run_id, final.station_id, init.segment, final.segment "
        "FROM final_pass fp "
        "JOIN station_segments init ON init.run_id = fp.init_run_id "
        "JOIN station_segments final ON final.run_id = fp.final_run_id AND final.station_id = init.station_id "
        "WHERE final.segment <> init.segment;"
        "INSERT INTO refinement_summary(run_id,init_run_id,final_run_id,moved_stations,unique_moved_stations) "
        "WITH final_pass AS ("
        "  SELECT p.run_id, init.solution_run_id AS init_run_id, p.solution_run_id AS final_run_id "
        "  FROM refinement_passes p "
        "  JOIN refinement_passes init ON init.run_id = p.run_id AND init.pass_number = 0 "
        "  WHERE p.pass_number = (SELECT MAX(p2.pass_number) FROM refinement_passes p2 WHERE p2.run_id = p.run_id)"
        ") "
        "SELECT fp.run_id, fp.init_run_id, fp.final_run_id, "
        "COALESCE((SELECT SUM(p.stations_moved) FROM refinement_passes p WHERE p.run_id = fp.run_id AND p.pass_number > 0), 0), "
        "COALESCE((SELECT COUNT(*) FROM refinement_unique_station_moves u WHERE u.run_id = fp.run_id), 0) "
        "FROM final_pass fp;");
}

static int bind_text_or_null(sqlite3_stmt *stmt, int idx, const char *s) {
    return s ? sqlite3_bind_text(stmt, idx, s, -1, SQLITE_TRANSIENT) : sqlite3_bind_null(stmt, idx);
}

static int insert_run(sqlite3 *db, const char *run_id, const char *method, const char *phase,
                      const char *solution_key, const char *parent_run_id, int is_final,
                      int l1seg, int l2seg, int timeout, int n_segments, int feasible,
                      int boat_id, const char *boat_name, const char *source_json,
                      double runtime_seconds, const char *created_at) {
    const char *sql =
        "INSERT INTO runs(run_id,method,phase,solution_key,parent_run_id,is_final,"
        "l1seg_timeout_seconds,l2seg_timeout_seconds,timeout_seconds,n_segments,"
        "feasible,boat_id,boat_name,runtime_seconds,source_json,created_at) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    SQL_CHECK(db, rc, "prepare insert_run");
    sqlite3_bind_text(stmt, 1, run_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, method, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, phase, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, solution_key, -1, SQLITE_TRANSIENT);
    bind_text_or_null(stmt, 5, parent_run_id);
    sqlite3_bind_int(stmt, 6, is_final);
    l1seg >= 0 ? sqlite3_bind_int(stmt, 7, l1seg) : sqlite3_bind_null(stmt, 7);
    l2seg >= 0 ? sqlite3_bind_int(stmt, 8, l2seg) : sqlite3_bind_null(stmt, 8);
    timeout >= 0 ? sqlite3_bind_int(stmt, 9, timeout) : sqlite3_bind_null(stmt, 9);
    sqlite3_bind_int(stmt, 10, n_segments);
    feasible >= 0 ? sqlite3_bind_int(stmt, 11, feasible) : sqlite3_bind_null(stmt, 11);
    boat_id >= 0 ? sqlite3_bind_int(stmt, 12, boat_id) : sqlite3_bind_null(stmt, 12);
    bind_text_or_null(stmt, 13, boat_name);
    runtime_seconds >= 0.0 ? sqlite3_bind_double(stmt, 14, runtime_seconds) : sqlite3_bind_null(stmt, 14);
    sqlite3_bind_text(stmt, 15, source_json, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 16, created_at, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    SQL_CHECK(db, rc, "insert_run");
    return 1;
}

static int insert_distance(sqlite3 *db, const char *run_id, const char *solution_json) {
    const char *grand_sql =
        "INSERT INTO distance(run_id,segment,transit_nm,total_nm) "
        "VALUES(?1,NULL,json_extract(?2,'$.distance_nm.grand_total.transit'),json_extract(?2,'$.distance_nm.grand_total.total'))";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, grand_sql, -1, &stmt, NULL);
    SQL_CHECK(db, rc, "prepare distance grand");
    sqlite3_bind_text(stmt, 1, run_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, solution_json, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    SQL_CHECK(db, rc, "insert distance grand");

    const char *seg_sql =
        "INSERT INTO distance(run_id,segment,transit_nm,total_nm) "
        "SELECT ?1, CAST(t.key AS INTEGER)+1, t.value, "
        "json_extract(?2,'$.distance_nm.segment.total[' || t.key || ']') "
        "FROM json_each(?2,'$.distance_nm.segment.transit') AS t";
    rc = sqlite3_prepare_v2(db, seg_sql, -1, &stmt, NULL);
    SQL_CHECK(db, rc, "prepare distance segment");
    sqlite3_bind_text(stmt, 1, run_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, solution_json, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    SQL_CHECK(db, rc, "insert distance segment");
    return 1;
}

static int insert_location_segments(sqlite3 *db, const char *run_id, const char *solution_json, int boat_location_id) {
    const char *sql =
        "INSERT INTO location_segments(run_id,segment,sequence,location_id,point_type) "
        "WITH docks(id) AS (SELECT CAST(value AS INTEGER) FROM json_each(?2,'$.dock_location_ids')), "
        "wps(id) AS (SELECT CAST(value AS INTEGER) FROM json_each(?2,'$.unique_waypoint_location_ids')) "
        "SELECT ?1, CAST(seg.key AS INTEGER)+1, CAST(loc.key AS INTEGER)+1, CAST(loc.value AS INTEGER), "
        "CASE WHEN CAST(loc.value AS INTEGER)=?3 THEN 'BOAT' "
        "WHEN CAST(loc.value AS INTEGER) IN (SELECT id FROM docks) THEN 'PORT' "
        "WHEN CAST(loc.value AS INTEGER) IN (SELECT id FROM wps) THEN 'WAYP' ELSE 'STAT' END "
        "FROM json_each(?2,'$.tour_segments_location_ids') AS seg, json_each(seg.value) AS loc";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    SQL_CHECK(db, rc, "prepare location_segments");
    sqlite3_bind_text(stmt, 1, run_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, solution_json, -1, SQLITE_STATIC);
    boat_location_id >= 0 ? sqlite3_bind_int(stmt, 3, boat_location_id) : sqlite3_bind_null(stmt, 3);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    SQL_CHECK(db, rc, "insert location_segments");
    return 1;
}

static int insert_station_segments(sqlite3 *db, const char *run_id, const char *solution_json) {
    const char *sql =
        "INSERT INTO station_segments(run_id,segment,sequence,signed_station_id,station_id) "
        "SELECT ?1, CAST(seg.key AS INTEGER)+1, CAST(st.key AS INTEGER)+1, "
        "CAST(st.value AS INTEGER), abs(CAST(st.value AS INTEGER)) "
        "FROM json_each(?2,'$.tour_segments_station_ids') AS seg, json_each(seg.value) AS st";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    SQL_CHECK(db, rc, "prepare station_segments");
    sqlite3_bind_text(stmt, 1, run_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, solution_json, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    SQL_CHECK(db, rc, "insert station_segments");
    return 1;
}

static int insert_refinement_pass(sqlite3 *db, const char *refinement_run_id,
                                  const char *solution_run_id, const char *solution_json) {
    const char *sql =
        "INSERT INTO refinement_passes(run_id,solution_run_id,pass_number,changed,stations_moved,"
        "boundary_attempts,boundary_changes,mip_solves,runtime_seconds) "
        "VALUES(?1,?2,json_extract(?3,'$.pass'),json_extract(?3,'$.changed'),"
        "json_extract(?3,'$.stations_moved'),json_extract(?3,'$.boundary_attempts'),"
        "json_extract(?3,'$.boundary_changes'),"
        "json_extract(?3,'$.mip_solves'),json_extract(?3,'$.pass_runtime_seconds'))";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    SQL_CHECK(db, rc, "prepare refinement_passes");
    sqlite3_bind_text(stmt, 1, refinement_run_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, solution_run_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, solution_json, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    SQL_CHECK(db, rc, "insert refinement_passes");
    return 1;
}

static int insert_mip_solves(sqlite3 *db, const char *run_id, const char *variant, const char *json) {
    (void)variant;
    const char *simple_sql =
        "INSERT INTO mip_solves(phase_code,segment_model,station_count,node_count,model_variable_count,"
        "model_constraint_count,runtime_seconds,gap_percent) "
        "SELECT "
        "CASE (SELECT phase FROM runs WHERE run_id=?1) "
        "WHEN 'construction' THEN 'C' WHEN 'segmentation' THEN 'S' WHEN 'refinement' THEN 'R' ELSE NULL END, "
        "CASE WHEN json_extract(?2,'$.mip.phase')='noport' THEN '0seg' "
        "WHEN json_extract(?2,'$.mip.phase')='l0seg' THEN '0seg' "
        "WHEN json_extract(?2,'$.mip.phase')='l1seg' THEN '1seg' "
        "WHEN json_extract(?2,'$.mip.phase')='fixedport' THEN 'Xseg' "
        "WHEN (SELECT phase FROM runs WHERE run_id=?1)='construction' THEN '0seg' "
        "WHEN (SELECT phase FROM runs WHERE run_id=?1)='segmentation' THEN '1seg' ELSE NULL END, "
        "json_extract(s.value,'$[0]'), "
        "CASE WHEN json_array_length(s.value) >= 5 THEN json_extract(s.value,'$[1]') ELSE json_extract(s.value,'$[0]') + 2 END, "
        "CASE WHEN json_array_length(s.value) >= 5 THEN json_extract(s.value,'$[2][0]') ELSE (2 * (json_extract(s.value,'$[0]') + 1)) * (2 * (json_extract(s.value,'$[0]') + 1)) END, "
        "CASE WHEN json_array_length(s.value) >= 5 THEN json_extract(s.value,'$[2][1]') ELSE 5 * (json_extract(s.value,'$[0]') + 1) END, "
        "CASE WHEN json_array_length(s.value) >= 5 THEN json_extract(s.value,'$[3]') ELSE json_extract(s.value,'$[1]') END, "
        "CASE WHEN json_array_length(s.value) >= 5 THEN json_extract(s.value,'$[4]') ELSE json_extract(s.value,'$[2]') END "
        "FROM json_each(?2,'$.mip.solves') AS s";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, simple_sql, -1, &stmt, NULL);
    SQL_CHECK(db, rc, "prepare mip simple");
    sqlite3_bind_text(stmt, 1, run_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, json, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    SQL_CHECK(db, rc, "insert mip simple");

    const char *ref_sql =
        "INSERT INTO mip_solves(phase_code,segment_model,station_count,node_count,model_variable_count,"
        "model_constraint_count,runtime_seconds,gap_percent) "
        "SELECT 'R', fam.key, "
        "json_extract(v.value,'$[4]'), json_extract(v.value,'$[5]'), "
        "json_extract(v.value,'$[7][0]'), json_extract(v.value,'$[7][1]'), "
        "json_extract(v.value,'$[8]'), json_extract(v.value,'$[9]') "
        "FROM json_each(?2,'$.mip') AS fam, json_each(fam.value,'$.values') AS v "
        "WHERE fam.key IN ('1seg','2seg')";
    rc = sqlite3_prepare_v2(db, ref_sql, -1, &stmt, NULL);
    SQL_CHECK(db, rc, "prepare mip refinement");
    sqlite3_bind_text(stmt, 1, run_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, json, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    SQL_CHECK(db, rc, "insert mip refinement");
    return 1;
}

static int insert_refinement_solve_context(sqlite3 *db, const char *refinement_run_id,
                                           const char *solution_json, const char *json) {
    const char *ctx_sql =
        "INSERT INTO refinement_solve_context(run_id,pass_number,solve_idx,segment_model,timeout_seconds,"
        "boundary_index,candidate_split_index,segment,moved_stations) "
        "SELECT ?1, json_extract(?3,'$.pass'), CAST(v.key AS INTEGER)+1, fam.key, json_extract(fam.value,'$.L'), "
        "json_extract(v.value,'$[1]'), json_extract(v.value,'$[2]'), json_extract(v.value,'$[3]'), "
        "json_extract(v.value,'$[6]') "
        "FROM json_each(?2,'$.mip') AS fam, json_each(fam.value,'$.values') AS v "
        "WHERE fam.key IN ('1seg','2seg') "
        "AND json_extract(v.value,'$[0]') = json_extract(?3,'$.pass')";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, ctx_sql, -1, &stmt, NULL);
    SQL_CHECK(db, rc, "prepare refinement_solve_context");
    sqlite3_bind_text(stmt, 1, refinement_run_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, json, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, solution_json, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    SQL_CHECK(db, rc, "insert refinement_solve_context");
    return 1;
}

static int insert_refinement_station_mutations(sqlite3 *db, const char *refinement_run_id,
                                               const char *solution_json) {
    const char *sql =
        "INSERT INTO refinement_station_mutations(run_id,pass_number,segment,sequence,signed_station_id,station_id) "
        "SELECT ?1, json_extract(?2,'$.pass'), CAST(seg.key AS INTEGER)+1, "
        "CAST(st.key AS INTEGER)+1, CAST(st.value AS INTEGER), abs(CAST(st.value AS INTEGER)) "
        "FROM json_each(?2,'$.tour_segments_station_mutation_ids') AS seg, json_each(seg.value) AS st";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    SQL_CHECK(db, rc, "prepare refinement_station_mutations");
    sqlite3_bind_text(stmt, 1, refinement_run_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, solution_json, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    SQL_CHECK(db, rc, "insert refinement_station_mutations");
    return 1;
}

static int solution_segment_count(sqlite3 *db, const char *solution_json) {
    int n = scalar_int(db, solution_json, "$.segment_count", -1);
    if (n >= 0) return n;
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, "SELECT count(*) FROM json_each(?1,'$.tour_segments_location_ids')", -1, &stmt, NULL) != SQLITE_OK) {
        return 0;
    }
    sqlite3_bind_text(stmt, 1, solution_json, -1, SQLITE_STATIC);
    n = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) n = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return n;
}

static void make_run_id(char *out, size_t out_sz, const char *method, const char *phase, int l2seg, const char *solution_key) {
    if (strcmp(phase, "refinement") == 0 && l2seg >= 0) {
        snprintf(out, out_sz, "%s:%s:l2seg=%d:%s", method, phase, l2seg, solution_key);
    } else {
        snprintf(out, out_sz, "%s:%s:%s", method, phase, solution_key);
    }
}

static void make_refinement_run_id(char *out, size_t out_sz, const char *method, int l2seg) {
    if (l2seg >= 0) {
        snprintf(out, out_sz, "%s:refinement:l2seg=%d", method, l2seg);
    } else {
        snprintf(out, out_sz, "%s:refinement", method);
    }
}

static void make_parent_id(char *out, size_t out_sz, const char *method, const char *phase, int l2seg,
                           const char *solution_key, const char *prev_run_id) {
    out[0] = '\0';
    if (strcmp(phase, "segmentation") == 0) {
        snprintf(out, out_sz, "%s:construction:capacity-infeasible", method);
    } else if (strcmp(phase, "refinement") == 0) {
        if (strcmp(solution_key, "init") == 0) {
            snprintf(out, out_sz, "%s:segmentation:capacity-feasible", method);
        } else if (prev_run_id && *prev_run_id) {
            snprintf(out, out_sz, "%s", prev_run_id);
        } else if (l2seg >= 0) {
            snprintf(out, out_sz, "%s:refinement:l2seg=%d:init", method, l2seg);
        }
    }
}

static int ingest_solution_file(sqlite3 *db, const char *path, const char *created_at) {
    char method[32], phase[32];
    char *json = NULL, *final = NULL, *boat_name = NULL;
    int inserted = 0;
    if (!method_from_path(path, method, sizeof(method)) || !phase_from_file(path, phase, sizeof(phase))) return 1;
    if (strcmp(base_name(path), "candidate_ports.json") == 0) return 1;
    json = read_file(path);
    if (!json) {
        fprintf(stderr, "Failed to read %s: %s\n", path, strerror(errno));
        return 0;
    }
    final = final_variant(db, json);
    boat_name = scalar_text(db, json, "$.metadata.boat_name");
    int boat_id = scalar_int(db, json, "$.metadata.boat_id", -1);
    int boat_location_id = scalar_int(db, json, "$.metadata.boat_location_id", -1);
    int l2seg = l2seg_from_file_or_json(db, path, json);
    int l1seg = scalar_int(db, json, "$.metadata.l1seg_time_limit_seconds", -1);
    int timeout = strcmp(phase, "refinement") == 0 ? l2seg : scalar_int(db, json, "$.metadata.global_time_limit_seconds", -1);
    double runtime_seconds = scalar_double(db, json, "$.summary.runtime_seconds.grandtotal", -1.0);
    sqlite3_stmt *stmt = NULL;
    const char *sql =
        "SELECT key, value FROM json_each(?1,'$.solution') "
        "ORDER BY CASE WHEN key='init' THEN 0 WHEN key GLOB 'pass[0-9]*' THEN CAST(substr(key,5) AS INTEGER) ELSE 100000 END";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) goto fail;
    sqlite3_bind_text(stmt, 1, json, -1, SQLITE_STATIC);
    char prev_run_id[256] = "";
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *solution_key = (const char*)sqlite3_column_text(stmt, 0);
        const char *solution_json = (const char*)sqlite3_column_text(stmt, 1);
        if (!solution_key || !solution_json) continue;
        char run_id[256], parent_id[256], refinement_run_id[256];
        make_run_id(run_id, sizeof(run_id), method, phase, l2seg, solution_key);
        make_refinement_run_id(refinement_run_id, sizeof(refinement_run_id), method, l2seg);
        make_parent_id(parent_id, sizeof(parent_id), method, phase, l2seg, solution_key, prev_run_id);
        int nseg = solution_segment_count(db, solution_json);
        int feasible = scalar_int(db, solution_json, "$.feasible", -1);
        if (!insert_run(db, run_id, method, phase, solution_key, parent_id[0] ? parent_id : NULL,
                        final && strcmp(solution_key, final) == 0, l1seg, l2seg, timeout,
                        nseg, feasible, boat_id, boat_name, path, runtime_seconds, created_at)) goto fail_stmt;
        if (!insert_distance(db, run_id, solution_json)) goto fail_stmt;
        if (!insert_location_segments(db, run_id, solution_json, boat_location_id)) goto fail_stmt;
        if (!insert_station_segments(db, run_id, solution_json)) goto fail_stmt;
        if (strcmp(phase, "refinement") == 0) {
            if (!insert_refinement_pass(db, refinement_run_id, run_id, solution_json)) goto fail_stmt;
            if (!insert_refinement_solve_context(db, refinement_run_id, solution_json, json)) goto fail_stmt;
            if (!insert_refinement_station_mutations(db, refinement_run_id, solution_json)) goto fail_stmt;
        }
        if (final && strcmp(solution_key, final) == 0 && !insert_mip_solves(db, run_id, solution_key, json)) goto fail_stmt;
        snprintf(prev_run_id, sizeof(prev_run_id), "%s", run_id);
        inserted++;
    }
    sqlite3_finalize(stmt);
    free(json);
    free(final);
    free(boat_name);
    return inserted >= 0;
fail_stmt:
    sqlite3_finalize(stmt);
fail:
    free(json);
    free(final);
    free(boat_name);
    return 0;
}

static int ingest_multivessel(sqlite3 *db, const char *path, const char *created_at) {
    char *json = read_file(path);
    if (!json) return 0;
    sqlite3_stmt *boats = NULL;
    int inserted = 0;
    if (sqlite3_prepare_v2(db, "SELECT value FROM json_each(?1,'$.boats')", -1, &boats, NULL) != SQLITE_OK) {
        free(json);
        return 0;
    }
    sqlite3_bind_text(boats, 1, json, -1, SQLITE_STATIC);
    while (sqlite3_step(boats) == SQLITE_ROW) {
        const char *boat_json = (const char*)sqlite3_column_text(boats, 0);
        char *final = final_variant(db, boat_json);
        if (!boat_json || !final) { free(final); continue; }
        sqlite3_stmt *sol_stmt = NULL;
        if (sqlite3_prepare_v2(db, "SELECT json_extract(?1,'$.solution.' || json_quote(?2))", -1, &sol_stmt, NULL) != SQLITE_OK) {
            free(final);
            continue;
        }
        sqlite3_bind_text(sol_stmt, 1, boat_json, -1, SQLITE_STATIC);
        sqlite3_bind_text(sol_stmt, 2, final, -1, SQLITE_STATIC);
        if (sqlite3_step(sol_stmt) == SQLITE_ROW && sqlite3_column_type(sol_stmt, 0) != SQLITE_NULL) {
            const char *solution_json = (const char*)sqlite3_column_text(sol_stmt, 0);
            int boat_id = scalar_int(db, boat_json, "$.metadata.boat_id", -1);
            int boat_location_id = scalar_int(db, boat_json, "$.metadata.boat_location_id", -1);
            char *boat_name = scalar_text(db, boat_json, "$.metadata.boat_name");
            char run_id[256];
            snprintf(run_id, sizeof(run_id), "survey:boat=%d:%s", boat_id, final);
            int feasible = scalar_int(db, solution_json, "$.feasible", -1);
            if (!insert_run(db, run_id, "survey", "survey", final, NULL, 1, -1, -1, -1,
                            solution_segment_count(db, solution_json), feasible, boat_id, boat_name, path, -1.0, created_at) ||
                !insert_distance(db, run_id, solution_json) ||
                !insert_location_segments(db, run_id, solution_json, boat_location_id) ||
                !insert_station_segments(db, run_id, solution_json)) {
                free(boat_name);
                sqlite3_finalize(sol_stmt);
                free(final);
                sqlite3_finalize(boats);
                free(json);
                return 0;
            }
            free(boat_name);
            inserted++;
        }
        sqlite3_finalize(sol_stmt);
        free(final);
    }
    sqlite3_finalize(boats);
    free(json);
    (void)inserted;
    return 1;
}

static int walk_sol(sqlite3 *db, const char *dir, const char *created_at) {
    DIR *dp = opendir(dir);
    if (!dp) {
        fprintf(stderr, "Cannot open %s: %s\n", dir, strerror(errno));
        return 0;
    }
    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        char path[2048];
        snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
        if (is_dir_path(path)) {
            if (!walk_sol(db, path, created_at)) { closedir(dp); return 0; }
        } else if (has_suffix(de->d_name, ".json")) {
            if (strcmp(de->d_name, "candidate_ports.json") == 0) continue;
            if (strstr(path, "/survey/multivessel.json") || strstr(path, "\\survey\\multivessel.json")) {
                if (!ingest_multivessel(db, path, created_at)) { closedir(dp); return 0; }
            } else {
                int before = sqlite3_total_changes(db);
                if (!ingest_solution_file(db, path, created_at)) { closedir(dp); return 0; }
                if (sqlite3_total_changes(db) > before) {
                    /* counted below from runs table */
                }
            }
        }
    }
    closedir(dp);
    return 1;
}

static void dirname_of(const char *path, char *out, size_t out_sz) {
    snprintf(out, out_sz, "%s", path);
    char *a = strrchr(out, '/');
    char *b = strrchr(out, '\\');
    char *p = a > b ? a : b;
    if (p) *p = '\0';
    else snprintf(out, out_sz, ".");
}

static void ensure_dir(const char *dir) {
#ifdef _WIN32
    _mkdir(dir);
#else
    mkdir(dir, 0777);
#endif
}

int main(int argc, char **argv) {
    const char *sol_dir = (argc >= 2) ? argv[1] : "sol";
    const char *out_db = (argc >= 3) ? argv[2] : "dat/solution.db";
    sqlite3 *db = NULL;
    char out_dir[2048];
    char created_at[64];
    time_t now = time(NULL);
    struct tm *utc = gmtime(&now);
    strftime(created_at, sizeof(created_at), "%Y-%m-%dT%H:%M:%SZ", utc);

    dirname_of(out_db, out_dir, sizeof(out_dir));
    ensure_dir(out_dir);
    if (remove(out_db) != 0 && errno != ENOENT) {
        fprintf(stderr, "Cannot remove existing %s: %s\n", out_db, strerror(errno));
        return 1;
    }

    if (sqlite3_open(out_db, &db) != SQLITE_OK) {
        fprintf(stderr, "Cannot open %s: %s\n", out_db, sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }
    if (!create_schema(db) || !exec_sql(db, "BEGIN") || !walk_sol(db, sol_dir, created_at) ||
        !fix_lineage(db) || !populate_refinement_move_tables(db) || !exec_sql(db, "COMMIT")) {
        exec_sql(db, "ROLLBACK");
        sqlite3_close(db);
        return 1;
    }

    sqlite3_stmt *stmt = NULL;
    int runs = 0;
    if (sqlite3_prepare_v2(db, "SELECT count(*) FROM runs", -1, &stmt, NULL) == SQLITE_OK &&
        sqlite3_step(stmt) == SQLITE_ROW) {
        runs = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    printf("Wrote %s (%d runs)\n", out_db, runs);
    return 0;
}
