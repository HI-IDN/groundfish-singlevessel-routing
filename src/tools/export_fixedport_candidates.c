#include <sqlite3.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE 1024

typedef struct {
    int *data;
    int count;
    int capacity;
} int_vec_t;

typedef struct {
    char *path;
    int *dock_location_ids;
    int dock_count;
    int *segment_end_dock_location_ids;
    int segment_end_dock_count;
    int *segment_catch_amount;
    int segment_count;
    int kept_segment_count;
    int_vec_t kept_end_docks;
    int dropped_segment_count;
    int_vec_t dropped_segment_indices;
    int_vec_t dropped_segment_catches;
    int_vec_t dropped_end_docks;
} boat_candidate_t;

typedef struct {
    int location_id;
    int port_id;
    char *name;
    int visit_count;
} port_info_t;

static char *dup_cstr_local(const char *s) {
    size_t n;
    char *out;
    if (!s) return NULL;
    n = strlen(s) + 1;
    out = (char*)malloc(n);
    if (!out) return NULL;
    memcpy(out, s, n);
    return out;
}

static void int_vec_init(int_vec_t *vec) {
    if (!vec) return;
    vec->data = NULL;
    vec->count = 0;
    vec->capacity = 0;
}

static void int_vec_free(int_vec_t *vec) {
    if (!vec) return;
    free(vec->data);
    vec->data = NULL;
    vec->count = 0;
    vec->capacity = 0;
}

static int int_vec_push(int_vec_t *vec, int value) {
    int *tmp;
    int new_capacity;
    if (!vec) return 0;
    if (vec->count >= vec->capacity) {
        new_capacity = (vec->capacity > 0) ? (vec->capacity * 2) : 16;
        tmp = (int*)realloc(vec->data, (size_t)new_capacity * sizeof(int));
        if (!tmp) return 0;
        vec->data = tmp;
        vec->capacity = new_capacity;
    }
    vec->data[vec->count++] = value;
    return 1;
}

static void free_boat_candidate(boat_candidate_t *boat) {
    if (!boat) return;
    free(boat->path);
    free(boat->dock_location_ids);
    free(boat->segment_end_dock_location_ids);
    free(boat->segment_catch_amount);
    int_vec_free(&boat->kept_end_docks);
    int_vec_free(&boat->dropped_segment_indices);
    int_vec_free(&boat->dropped_segment_catches);
    int_vec_free(&boat->dropped_end_docks);
    memset(boat, 0, sizeof(*boat));
}

static void free_port_info_array(port_info_t *ports, int count) {
    if (!ports) return;
    for (int i = 0; i < count; i++) free(ports[i].name);
    free(ports);
}

static char *read_text_file(const char *path) {
    FILE *fp;
    long size;
    char *buf;
    if (!path) return NULL;
    fp = fopen(path, "rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);
    buf = (char*)malloc((size_t)size + 1u);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    if (size > 0 && fread(buf, 1, (size_t)size, fp) != (size_t)size) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    buf[size] = '\0';
    fclose(fp);
    return buf;
}

static const char *skip_ws(const char *p) {
    while (p && *p && isspace((unsigned char)*p)) p++;
    return p;
}

static const char *find_json_key(const char *text, const char *key) {
    char pattern[128];
    if (!text || !key) return NULL;
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    return strstr(text, pattern);
}

static const char *find_matching_bracket(const char *p) {
    int depth = 0;
    if (!p || *p != '[') return NULL;
    while (*p) {
        if (*p == '[') depth++;
        else if (*p == ']') {
            depth--;
            if (depth == 0) return p;
        }
        p++;
    }
    return NULL;
}

static int parse_int_array_after_key(const char *json, const char *key, int **out_values, int *out_count) {
    const char *p;
    const char *start;
    const char *end;
    int *values = NULL;
    int count = 0;
    int capacity = 0;

    if (out_values) *out_values = NULL;
    if (out_count) *out_count = 0;
    p = find_json_key(json, key);
    if (!p) return 0;
    p = strchr(p, ':');
    if (!p) return 0;
    p = skip_ws(p + 1);
    if (!p || *p != '[') return 0;
    start = p;
    end = find_matching_bracket(start);
    if (!end) return 0;

    p = start + 1;
    while (p < end) {
        char *next;
        long value;
        p = skip_ws(p);
        if (!p || p >= end) break;
        if (*p == '[') {
            const char *nested_end = find_matching_bracket(p);
            if (!nested_end || nested_end > end) {
                free(values);
                return 0;
            }
            p = nested_end + 1;
            continue;
        }
        if (*p == ',') {
            p++;
            continue;
        }
        value = strtol(p, &next, 10);
        if (next == p) {
            p++;
            continue;
        }
        if (count >= capacity) {
            int new_capacity = (capacity > 0) ? (capacity * 2) : 16;
            int *tmp = (int*)realloc(values, (size_t)new_capacity * sizeof(int));
            if (!tmp) {
                free(values);
                return 0;
            }
            values = tmp;
            capacity = new_capacity;
        }
        values[count++] = (int)value;
        p = next;
    }

    if (out_values) *out_values = values;
    else free(values);
    if (out_count) *out_count = count;
    return 1;
}

static int parse_segment_end_docks_after_key(const char *json, const char *key, int **out_values, int *out_count) {
    const char *p;
    const char *start;
    const char *end;
    int *values = NULL;
    int count = 0;
    int capacity = 0;

    if (out_values) *out_values = NULL;
    if (out_count) *out_count = 0;
    p = find_json_key(json, key);
    if (!p) return 0;
    p = strchr(p, ':');
    if (!p) return 0;
    p = skip_ws(p + 1);
    if (!p || *p != '[') return 0;
    start = p;
    end = find_matching_bracket(start);
    if (!end) return 0;

    p = start + 1;
    while (p < end) {
        const char *nested_end;
        const char *q;
        int last_value = 0;
        int have_value = 0;
        p = skip_ws(p);
        if (!p || p >= end) break;
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p != '[') {
            p++;
            continue;
        }
        nested_end = find_matching_bracket(p);
        if (!nested_end || nested_end > end) {
            free(values);
            return 0;
        }
        q = p + 1;
        while (q < nested_end) {
            char *next;
            long value;
            q = skip_ws(q);
            if (!q || q >= nested_end) break;
            if (*q == ',') {
                q++;
                continue;
            }
            value = strtol(q, &next, 10);
            if (next == q) {
                q++;
                continue;
            }
            last_value = (int)value;
            have_value = 1;
            q = next;
        }
        if (have_value) {
            if (count >= capacity) {
                int new_capacity = (capacity > 0) ? (capacity * 2) : 16;
                int *tmp = (int*)realloc(values, (size_t)new_capacity * sizeof(int));
                if (!tmp) {
                    free(values);
                    return 0;
                }
                values = tmp;
                capacity = new_capacity;
            }
            values[count++] = last_value;
        }
        p = nested_end + 1;
    }

    if (out_values) *out_values = values;
    else free(values);
    if (out_count) *out_count = count;
    return 1;
}

static int read_fixedport_min_segment_catch_kg(const char *yaml_path) {
    FILE *fp;
    char line[MAX_LINE];
    int in_init = 0;
    int threshold = 1000;
    const char *key = "fixedport_min_segment_catch_kg:";
    size_t key_len = strlen(key);

    if (!yaml_path) return threshold;
    fp = fopen(yaml_path, "r");
    if (!fp) return threshold;

    while (fgets(line, sizeof(line), fp)) {
        char *trimmed = line;
        while (*trimmed && isspace((unsigned char)*trimmed)) trimmed++;
        if (*trimmed == '#' || *trimmed == '\0' || *trimmed == '\n') continue;
        if (!isspace((unsigned char)line[0])) {
            in_init = (strncmp(trimmed, "init:", 5) == 0);
            continue;
        }
        if (!in_init) continue;
        if (strncmp(trimmed, key, key_len) == 0) {
            threshold = atoi(trimmed + key_len);
            if (threshold < 0) threshold = 0;
            break;
        }
    }

    fclose(fp);
    return threshold;
}

static int append_port_info(port_info_t **ports,
                            int *count,
                            int *capacity,
                            int location_id,
                            int port_id,
                            const char *name) {
    port_info_t *tmp;
    int new_capacity;
    if (!ports || !count || !capacity) return 0;
    if (*count >= *capacity) {
        new_capacity = (*capacity > 0) ? (*capacity * 2) : 16;
        tmp = (port_info_t*)realloc(*ports, (size_t)new_capacity * sizeof(port_info_t));
        if (!tmp) return 0;
        *ports = tmp;
        *capacity = new_capacity;
    }
    (*ports)[*count].location_id = location_id;
    (*ports)[*count].port_id = port_id;
    (*ports)[*count].name = dup_cstr_local(name ? name : "");
    (*ports)[*count].visit_count = 0;
    if (!(*ports)[*count].name) return 0;
    (*count)++;
    return 1;
}

static int load_port_lookup(sqlite3 *db, port_info_t **out_ports, int *out_count) {
    sqlite3_stmt *stmt = NULL;
    port_info_t *ports = NULL;
    int count = 0;
    int capacity = 0;
    const char *sql =
        "SELECT p.location_id, p.id, p.name "
        "FROM ports p "
        "ORDER BY p.location_id;";

    if (out_ports) *out_ports = NULL;
    if (out_count) *out_count = 0;
    if (!db) return 0;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int location_id = sqlite3_column_int(stmt, 0);
        int port_id = sqlite3_column_int(stmt, 1);
        const char *name = (const char*)sqlite3_column_text(stmt, 2);
        if (!append_port_info(&ports, &count, &capacity, location_id, port_id, name)) {
            sqlite3_finalize(stmt);
            free_port_info_array(ports, count);
            return 0;
        }
    }

    sqlite3_finalize(stmt);
    if (out_ports) *out_ports = ports;
    else free_port_info_array(ports, count);
    if (out_count) *out_count = count;
    return 1;
}

static port_info_t *find_port_info_by_location(port_info_t *ports, int count, int location_id) {
    if (!ports) return NULL;
    for (int i = 0; i < count; i++) {
        if (ports[i].location_id == location_id) return &ports[i];
    }
    return NULL;
}

static const char *path_basename_local(const char *path) {
    const char *slash;
    const char *backslash;
    if (!path) return "";
    slash = strrchr(path, '/');
    backslash = strrchr(path, '\\');
    if (!slash) return backslash ? backslash + 1 : path;
    if (!backslash) return slash + 1;
    return (slash > backslash) ? (slash + 1) : (backslash + 1);
}

static int load_boat_candidate(const char *json_path, int min_segment_catch_kg, boat_candidate_t *boat) {
    char *json = NULL;
    if (!json_path || !boat) return 0;
    memset(boat, 0, sizeof(*boat));
    int_vec_init(&boat->kept_end_docks);
    int_vec_init(&boat->dropped_segment_indices);
    int_vec_init(&boat->dropped_segment_catches);
    int_vec_init(&boat->dropped_end_docks);
    boat->path = dup_cstr_local(json_path);
    if (!boat->path) return 0;

    json = read_text_file(json_path);
    if (!json) {
        free_boat_candidate(boat);
        return 0;
    }

    if (!parse_int_array_after_key(json, "dock_location_ids", &boat->dock_location_ids, &boat->dock_count) ||
        !parse_segment_end_docks_after_key(json, "tour_segments_location_ids",
                                           &boat->segment_end_dock_location_ids,
                                           &boat->segment_end_dock_count) ||
        !parse_int_array_after_key(json, "segment_catch_amount", &boat->segment_catch_amount, &boat->segment_count)) {
        free(json);
        free_boat_candidate(boat);
        return 0;
    }
    free(json);

    if (boat->segment_end_dock_count != boat->segment_count) {
        free_boat_candidate(boat);
        return 0;
    }

    for (int i = 0; i < boat->segment_count; i++) {
        if (boat->segment_catch_amount[i] < min_segment_catch_kg) {
            boat->dropped_segment_count++;
            if (!int_vec_push(&boat->dropped_segment_indices, i + 1) ||
                !int_vec_push(&boat->dropped_segment_catches, boat->segment_catch_amount[i]) ||
                !int_vec_push(&boat->dropped_end_docks, boat->segment_end_dock_location_ids[i])) {
                free_boat_candidate(boat);
                return 0;
            }
            continue;
        }
        if (!int_vec_push(&boat->kept_end_docks, boat->segment_end_dock_location_ids[i])) {
            free_boat_candidate(boat);
            return 0;
        }
        boat->kept_segment_count++;
    }

    return 1;
}

static int write_output_json(const char *output_path,
                             int min_segment_catch_kg,
                             const boat_candidate_t *boats,
                             int boat_count,
                             const int_vec_t *aggregate_ports,
                             port_info_t *port_lookup,
                             int port_lookup_count) {
    FILE *fp;
    int total_kept_segments = 0;
    if (!output_path || !boats || boat_count <= 0 || !aggregate_ports) return 0;
    fp = fopen(output_path, "w");
    if (!fp) return 0;

    for (int i = 0; i < boat_count; i++) total_kept_segments += boats[i].kept_segment_count;
    for (int i = 0; i < aggregate_ports->count; i++) {
        port_info_t *port = find_port_info_by_location(port_lookup, port_lookup_count, aggregate_ports->data[i]);
        if (port) port->visit_count += 1;
    }

    fprintf(fp, "{\n");
    fprintf(fp, "  \"min_segment_catch_kg\": %d,\n", min_segment_catch_kg);
    fprintf(fp, "  \"boat_count\": %d,\n", boat_count);
    fprintf(fp, "  \"kept_segment_count\": %d,\n", total_kept_segments);
    fprintf(fp, "  \"ordered_port_location_ids\": [");
    for (int i = 0; i < aggregate_ports->count; i++) {
        if (i) fprintf(fp, ", ");
        fprintf(fp, "%d", aggregate_ports->data[i]);
    }
    fprintf(fp, "],\n");
    fprintf(fp, "  \"port_visit_summary\": [\n");
    {
        int emitted = 0;
        for (int i = 0; i < port_lookup_count; i++) {
            if (port_lookup[i].visit_count <= 0) continue;
            if (emitted > 0) fprintf(fp, ",\n");
            fprintf(fp, "    {\"location_id\": %d, \"port_id\": %d, \"name\": \"%s\", \"visit_count\": %d}",
                    port_lookup[i].location_id,
                    port_lookup[i].port_id,
                    port_lookup[i].name ? port_lookup[i].name : "",
                    port_lookup[i].visit_count);
            emitted++;
        }
        if (emitted > 0) fprintf(fp, "\n");
    }
    fprintf(fp, "  ],\n");
    fprintf(fp, "  \"boats\": [\n");
    for (int i = 0; i < boat_count; i++) {
        fprintf(fp, "    {\n");
        fprintf(fp, "      \"file\": \"%s\",\n", path_basename_local(boats[i].path));
        fprintf(fp, "      \"dock_location_ids\": [");
        for (int j = 0; j < boats[i].dock_count; j++) {
            if (j) fprintf(fp, ", ");
            fprintf(fp, "%d", boats[i].dock_location_ids[j]);
        }
        fprintf(fp, "],\n");
        fprintf(fp, "      \"segment_end_dock_location_ids\": [");
        for (int j = 0; j < boats[i].segment_end_dock_count; j++) {
            if (j) fprintf(fp, ", ");
            fprintf(fp, "%d", boats[i].segment_end_dock_location_ids[j]);
        }
        fprintf(fp, "],\n");
        fprintf(fp, "      \"segment_catch_amount\": [");
        for (int j = 0; j < boats[i].segment_count; j++) {
            if (j) fprintf(fp, ", ");
            fprintf(fp, "%d", boats[i].segment_catch_amount[j]);
        }
        fprintf(fp, "],\n");
        fprintf(fp, "      \"kept_segment_count\": %d,\n", boats[i].kept_segment_count);
        fprintf(fp, "      \"dropped_segment_count\": %d,\n", boats[i].dropped_segment_count);
        fprintf(fp, "      \"kept_end_dock_location_ids\": [");
        for (int j = 0; j < boats[i].kept_end_docks.count; j++) {
            if (j) fprintf(fp, ", ");
            fprintf(fp, "%d", boats[i].kept_end_docks.data[j]);
        }
        fprintf(fp, "],\n");
        fprintf(fp, "      \"dropped_segments\": [");
        for (int j = 0; j < boats[i].dropped_segment_count; j++) {
            if (j) fprintf(fp, ", ");
            fprintf(fp,
                    "{\"segment\": %d, \"catch_kg\": %d, \"end_dock_location_id\": %d}",
                    boats[i].dropped_segment_indices.data[j],
                    boats[i].dropped_segment_catches.data[j],
                    boats[i].dropped_end_docks.data[j]);
        }
        fprintf(fp, "]\n");
        fprintf(fp, "    }%s\n", (i + 1 < boat_count) ? "," : "");
    }
    fprintf(fp, "  ]\n");
    fprintf(fp, "}\n");
    fclose(fp);
    return 1;
}

int main(int argc, char **argv) {
    const char *yaml_path;
    const char *db_path;
    const char *output_path;
    boat_candidate_t *boats = NULL;
    int boat_count;
    int min_segment_catch_kg;
    int_vec_t aggregate_ports;
    sqlite3 *db = NULL;
    port_info_t *port_lookup = NULL;
    int port_lookup_count = 0;
    int ok = 0;

    if (argc < 5) {
        fprintf(stderr, "Usage: %s <config.yaml> <database.db> <output.json> <boat1.json> [boat2.json ...]\n", argv[0]);
        return 1;
    }

    yaml_path = argv[1];
    db_path = argv[2];
    output_path = argv[3];
    boat_count = argc - 4;
    min_segment_catch_kg = read_fixedport_min_segment_catch_kg(yaml_path);
    boats = (boat_candidate_t*)calloc((size_t)boat_count, sizeof(boat_candidate_t));
    if (!boats) return 1;
    int_vec_init(&aggregate_ports);

    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }
    if (!load_port_lookup(db, &port_lookup, &port_lookup_count)) {
        fprintf(stderr, "Failed to load port lookup from database\n");
        goto cleanup;
    }

    for (int i = 0; i < boat_count; i++) {
        if (!load_boat_candidate(argv[i + 4], min_segment_catch_kg, &boats[i])) {
            fprintf(stderr, "Failed to parse survey boat JSON: %s\n", argv[i + 4]);
            goto cleanup;
        }
        for (int j = 0; j < boats[i].kept_end_docks.count; j++) {
            if (!int_vec_push(&aggregate_ports, boats[i].kept_end_docks.data[j])) {
                goto cleanup;
            }
        }
    }

    if (!write_output_json(output_path, min_segment_catch_kg, boats, boat_count,
                           &aggregate_ports, port_lookup, port_lookup_count)) {
        fprintf(stderr, "Failed to write %s\n", output_path);
        goto cleanup;
    }

    printf("Wrote %s\n", output_path);
    printf("  min_segment_catch_kg: %d\n", min_segment_catch_kg);
    printf("  kept_segment_count: %d\n", aggregate_ports.count);
    printf("  ordered_port_location_ids: [");
    for (int i = 0; i < aggregate_ports.count; i++) {
        if (i) printf(", ");
        printf("%d", aggregate_ports.data[i]);
    }
    printf("]\n");
    ok = 1;

cleanup:
    for (int i = 0; i < boat_count; i++) free_boat_candidate(&boats[i]);
    free(boats);
    int_vec_free(&aggregate_ports);
    free_port_info_array(port_lookup, port_lookup_count);
    if (db) sqlite3_close(db);
    return ok ? 0 : 1;
}
