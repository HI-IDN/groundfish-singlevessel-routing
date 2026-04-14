/*
 * survey_fixedport_candidates
 * Extracts the embedded "candidate_ports" object from sol/survey/multivessel.json
 * and writes it as a standalone sol/fixedport/candidate_ports.json for the fixedport MIP.
 *
 * Usage: survey_fixedport_candidates <multivessel.json> <output_candidate_ports.json>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_text_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    if (size < 0) { fclose(fp); return NULL; }
    rewind(fp);
    char *buf = (char *)malloc((size_t)size + 1u);
    if (!buf) { fclose(fp); return NULL; }
    if (size > 0 && fread(buf, 1, (size_t)size, fp) != (size_t)size) {
        free(buf); fclose(fp); return NULL;
    }
    buf[size] = '\0';
    fclose(fp);
    return buf;
}

static const char *find_matching_brace(const char *p) {
    int depth = 0, in_str = 0, esc = 0;
    if (!p || *p != '{') return NULL;
    for (; *p; p++) {
        if (in_str) {
            if (esc) { esc = 0; continue; }
            if (*p == '\\') { esc = 1; continue; }
            if (*p == '"') in_str = 0;
            continue;
        }
        if (*p == '"')  { in_str = 1; continue; }
        if (*p == '{')  { depth++; continue; }
        if (*p == '}')  { if (--depth == 0) return p; }
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <multivessel.json> <output_candidate_ports.json>\n", argv[0]);
        return 1;
    }

    const char *multivessel_path = argv[1];
    const char *output_path      = argv[2];

    char *text = read_text_file(multivessel_path);
    if (!text) {
        fprintf(stderr, "Cannot read %s\n", multivessel_path);
        return 1;
    }

    const char *key = strstr(text, "\"candidate_ports\"");
    if (!key) {
        fprintf(stderr, "\"candidate_ports\" not found in %s\n", multivessel_path);
        free(text);
        return 1;
    }

    const char *brace = strchr(key, '{');
    if (!brace) {
        fprintf(stderr, "No object after \"candidate_ports\"\n");
        free(text);
        return 1;
    }

    const char *end = find_matching_brace(brace);
    if (!end) {
        fprintf(stderr, "Malformed JSON: unmatched brace in candidate_ports\n");
        free(text);
        return 1;
    }

    FILE *fp = fopen(output_path, "w");
    if (!fp) { perror("fopen"); free(text); return 1; }
    fwrite(brace, 1, (size_t)(end - brace + 1), fp);
    fprintf(fp, "\n");
    fclose(fp);

    printf("Wrote %s (extracted from %s)\n", output_path, multivessel_path);
    free(text);
    return 0;
}
