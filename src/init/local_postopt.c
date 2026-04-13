#include "local_postopt.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "init_utils.h"

#ifdef HAVE_GUROBI
#include "../mip/include/mip_endpaired_tsp.h"
#endif

#define MAX_LINE 1024

static int count_leading_spaces_local(const char *line) {
    int count = 0;
    while (line && *line == ' ') {
        count++;
        line++;
    }
    return count;
}

static double read_gurobi_phase_scalar_from_yaml(const char *yaml_path,
                                                 const char *nested_section_name,
                                                 const char *nested_phase_key,
                                                 const char *legacy_nested_phase_key,
                                                 const char *flat_default_key,
                                                 const char *flat_phase_key) {
    FILE *fp = fopen(yaml_path, "r");
    char line[MAX_LINE];
    int in_gurobi = 0;
    int in_nested = 0;
    double value = 0.0;
    int have_default = 0;
    char nested_header[MAX_LINE];
    char flat_default_header[MAX_LINE];
    char flat_phase_header[MAX_LINE];
    char phase_header[MAX_LINE];
    char legacy_phase_header[MAX_LINE];
    size_t nested_header_len;
    size_t flat_default_header_len;
    size_t flat_phase_header_len;
    size_t phase_header_len;
    size_t legacy_phase_header_len;

    if (!fp || !nested_section_name || !nested_phase_key) return 0.0;

    snprintf(nested_header, sizeof(nested_header), "%s:", nested_section_name);
    snprintf(flat_default_header, sizeof(flat_default_header), "%s:", flat_default_key ? flat_default_key : "");
    snprintf(flat_phase_header, sizeof(flat_phase_header), "%s:", flat_phase_key ? flat_phase_key : "");
    snprintf(phase_header, sizeof(phase_header), "%s:", nested_phase_key);
    if (legacy_nested_phase_key && *legacy_nested_phase_key) {
        snprintf(legacy_phase_header, sizeof(legacy_phase_header), "%s:", legacy_nested_phase_key);
    } else {
        legacy_phase_header[0] = '\0';
    }
    nested_header_len = strlen(nested_header);
    flat_default_header_len = strlen(flat_default_header);
    flat_phase_header_len = strlen(flat_phase_header);
    phase_header_len = strlen(phase_header);
    legacy_phase_header_len = strlen(legacy_phase_header);

    while (fgets(line, sizeof(line), fp)) {
        char *trimmed = line;
        int indent = count_leading_spaces_local(line);
        while (*trimmed && isspace((unsigned char)*trimmed)) trimmed++;
        if (*trimmed == '#' || *trimmed == '\0' || *trimmed == '\n') continue;

        if (indent == 0) {
            in_gurobi = (strncmp(trimmed, "gurobi:", 7) == 0);
            in_nested = 0;
            continue;
        }
        if (!in_gurobi) continue;

        if (indent == 2) {
            in_nested = (strncmp(trimmed, nested_header, nested_header_len) == 0);
            if (flat_default_key && strncmp(trimmed, flat_default_header, flat_default_header_len) == 0) {
                value = atof(trimmed + flat_default_header_len);
                if (value < 0.0) value = 0.0;
                have_default = 1;
            } else if (flat_phase_key && strncmp(trimmed, flat_phase_header, flat_phase_header_len) == 0) {
                value = atof(trimmed + flat_phase_header_len);
                if (value < 0.0) value = 0.0;
                have_default = 1;
            }
            continue;
        }

        if (indent == 4 && in_nested) {
            if (strncmp(trimmed, phase_header, phase_header_len) == 0) {
                value = atof(trimmed + phase_header_len);
                if (value < 0.0) value = 0.0;
                have_default = 1;
                break;
            }
            if (legacy_phase_header_len > 0 &&
                strncmp(trimmed, legacy_phase_header, legacy_phase_header_len) == 0) {
                value = atof(trimmed + legacy_phase_header_len);
                if (value < 0.0) value = 0.0;
                have_default = 1;
                break;
            }
        }
    }

    fclose(fp);
    return have_default ? value : 0.0;
}

static void zero_solution(nn_solution_t *sol) {
    if (!sol) return;
    memset(sol, 0, sizeof(*sol));
}

void init_free_solution(nn_solution_t *sol) {
    if (!sol) return;
    free(sol->tour);
    free(sol->visit_station_ids);
    free(sol->visit_station_segment);
    free(sol->visit_station_direction);
    free(sol->segment_starts);
    free(sol->segment_ends);
    free(sol->segment_catches);
    free(sol->segment_dists);
    zero_solution(sol);
}

int init_copy_solution(const nn_solution_t *src, nn_solution_t *dst) {
    if (!src || !dst) return 0;
    zero_solution(dst);

    if (src->tour_length > 0) {
        dst->tour = (int*)malloc((size_t)src->tour_length * sizeof(int));
        if (!dst->tour) goto fail;
        memcpy(dst->tour, src->tour, (size_t)src->tour_length * sizeof(int));
    }
    if (src->visit_station_count > 0) {
        dst->visit_station_ids = (int*)malloc((size_t)src->visit_station_count * sizeof(int));
        dst->visit_station_segment = (int*)malloc((size_t)src->visit_station_count * sizeof(int));
        dst->visit_station_direction = (int*)malloc((size_t)src->visit_station_count * sizeof(int));
        if (!dst->visit_station_ids || !dst->visit_station_segment || !dst->visit_station_direction) goto fail;
        memcpy(dst->visit_station_ids, src->visit_station_ids, (size_t)src->visit_station_count * sizeof(int));
        memcpy(dst->visit_station_segment, src->visit_station_segment, (size_t)src->visit_station_count * sizeof(int));
        memcpy(dst->visit_station_direction, src->visit_station_direction, (size_t)src->visit_station_count * sizeof(int));
    }
    if (src->segment_count > 0) {
        dst->segment_starts = (int*)malloc((size_t)src->segment_count * sizeof(int));
        dst->segment_ends = (int*)malloc((size_t)src->segment_count * sizeof(int));
        dst->segment_catches = (int*)malloc((size_t)src->segment_count * sizeof(int));
        dst->segment_dists = (double*)malloc((size_t)src->segment_count * sizeof(double));
        if (!dst->segment_starts || !dst->segment_ends || !dst->segment_catches || !dst->segment_dists) goto fail;
        memcpy(dst->segment_starts, src->segment_starts, (size_t)src->segment_count * sizeof(int));
        memcpy(dst->segment_ends, src->segment_ends, (size_t)src->segment_count * sizeof(int));
        memcpy(dst->segment_catches, src->segment_catches, (size_t)src->segment_count * sizeof(int));
        memcpy(dst->segment_dists, src->segment_dists, (size_t)src->segment_count * sizeof(double));
    }

    dst->tour_length = src->tour_length;
    dst->visit_station_count = src->visit_station_count;
    dst->segment_count = src->segment_count;
    dst->total_distance = src->total_distance;
    dst->total_catch = src->total_catch;
    return 1;

fail:
    init_free_solution(dst);
    return 0;
}

double read_init_mip_time_limit_from_yaml(const char *yaml_path) {
    return read_gurobi_phase_scalar_from_yaml(yaml_path, "time_limit_seconds", "1seg", "init", "l1seg", NULL);
}

double read_noport_mip_time_limit_from_yaml(const char *yaml_path) {
    return read_gurobi_phase_scalar_from_yaml(yaml_path, "time_limit_seconds", "0seg", "noport", "l0seg", NULL);
}

double read_sweep_mip_time_limit_from_yaml(const char *yaml_path) {
    return read_gurobi_phase_scalar_from_yaml(yaml_path, "time_limit_seconds", "2seg", "sweep", "l2seg", NULL);
}

double read_fixedport_mip_time_limit_from_yaml(const char *yaml_path) {
    return read_gurobi_phase_scalar_from_yaml(yaml_path, "time_limit_seconds", "Xseg", "fixedport", "lXseg", NULL);
}

int read_objective_include_haul_distance_from_yaml(const char *yaml_path) {
    FILE *fp = fopen(yaml_path, "r");
    char line[MAX_LINE];
    int in_objective = 0;
    int include_haul_distance = 0;

    if (!fp) return 0;

    while (fgets(line, sizeof(line), fp)) {
        char *trimmed = line;
        while (*trimmed && isspace((unsigned char)*trimmed)) trimmed++;
        if (*trimmed == '#' || *trimmed == '\0' || *trimmed == '\n') continue;

        if (!isspace((unsigned char)line[0])) {
            in_objective = (strncmp(trimmed, "objective:", 10) == 0);
            continue;
        }
        if (!in_objective) continue;

        if (strncmp(trimmed, "include_haul_distance:", 22) == 0) {
            char *value = trimmed + 22;
            while (*value && isspace((unsigned char)*value)) value++;
            include_haul_distance =
                !(strncmp(value, "false", 5) == 0 ||
                  strncmp(value, "False", 5) == 0 ||
                  strncmp(value, "FALSE", 5) == 0 ||
                  strncmp(value, "0", 1) == 0 ||
                  strncmp(value, "no", 2) == 0 ||
                  strncmp(value, "No", 2) == 0 ||
                  strncmp(value, "NO", 2) == 0);
            break;
        }
    }

    fclose(fp);
    return include_haul_distance;
}

static double read_phase_haul_distance_scale_from_yaml(const char *yaml_path, const char *phase_name) {
    char flat_phase_key[MAX_LINE];
    const char *nested_phase_key = phase_name;
    const char *legacy_nested_phase_key = phase_name;
    if (strcmp(phase_name, "noport") == 0) nested_phase_key = "0seg";
    else if (strcmp(phase_name, "init") == 0) nested_phase_key = "1seg";
    else if (strcmp(phase_name, "sweep") == 0) nested_phase_key = "2seg";
    else if (strcmp(phase_name, "fixedport") == 0) nested_phase_key = "Xseg";
    snprintf(flat_phase_key, sizeof(flat_phase_key), "%s_haul_distance_scale", phase_name);
    return read_gurobi_phase_scalar_from_yaml(yaml_path, "haul_distance_scale", nested_phase_key,
                                              legacy_nested_phase_key, "haul_distance_scale", flat_phase_key);
}

double read_noport_haul_distance_scale_from_yaml(const char *yaml_path) {
    return read_phase_haul_distance_scale_from_yaml(yaml_path, "noport");
}

double read_init_haul_distance_scale_from_yaml(const char *yaml_path) {
    return read_phase_haul_distance_scale_from_yaml(yaml_path, "init");
}

double read_sweep_haul_distance_scale_from_yaml(const char *yaml_path) {
    return read_phase_haul_distance_scale_from_yaml(yaml_path, "sweep");
}

double read_fixedport_haul_distance_scale_from_yaml(const char *yaml_path) {
    return read_phase_haul_distance_scale_from_yaml(yaml_path, "fixedport");
}

static int find_station_index_local(const nn_instance_t *inst, int station_id) {
    for (int i = 0; i < inst->num_stations; i++) {
        if (inst->nodes[i].table_id == station_id) return i;
    }
    return -1;
}

static int count_segment_reordered_stations(const nn_solution_t *input,
                                            int segment_index,
                                            const int *signed_station_ids,
                                            int station_count) {
    int moved = 0;
    int k = 0;
    int *original_signed_ids = NULL;

    if (!input || !signed_station_ids || station_count <= 0) return 0;
    original_signed_ids = (int*)malloc((size_t)station_count * sizeof(int));
    if (!original_signed_ids) return 0;

    for (int i = 0; i < input->visit_station_count; i++) {
        if (input->visit_station_segment[i] != segment_index) continue;
        if (k >= station_count) break;
        original_signed_ids[k++] = input->visit_station_ids[i] *
                                   ((input->visit_station_direction[i] < 0) ? -1 : 1);
    }
    if (k != station_count) {
        free(original_signed_ids);
        return 0;
    }

    for (int i = 0; i < station_count; i++) {
        if (original_signed_ids[i] != signed_station_ids[i]) moved++;
    }

    free(original_signed_ids);
    return moved;
}

#ifdef HAVE_GUROBI
static int solve_segment_order(const nn_instance_t *inst,
                               const int *station_ids,
                               int station_count,
                               int start_loc_id,
                               int end_loc_id,
                               double time_limit_seconds,
                               int include_haul_distance,
                               double haul_distance_scale,
                               GRBenv *env,
                               int **signed_station_ids_out,
                               double *runtime_seconds_out,
                               double *gap_percent_out,
                               int *model_num_vars_out,
                               int *model_num_constrs_out) {
    mip_endpaired_instance_t mip_instance;
    mip_endpaired_solution_t mip_solution;
    mip_params_t mip_params;
    int *instance_station_ids = NULL;
    int *start_loc_ids = NULL;
    int *end_loc_ids = NULL;
    int *amounts = NULL;

    memset(&mip_instance, 0, sizeof(mip_instance));
    memset(&mip_solution, 0, sizeof(mip_solution));
    memset(&mip_params, 0, sizeof(mip_params));

    instance_station_ids = (int*)malloc((size_t)station_count * sizeof(int));
    start_loc_ids = (int*)malloc((size_t)station_count * sizeof(int));
    end_loc_ids = (int*)malloc((size_t)station_count * sizeof(int));
    amounts = (int*)malloc((size_t)station_count * sizeof(int));
    if (!instance_station_ids || !start_loc_ids || !end_loc_ids || !amounts) goto fail;

    for (int i = 0; i < station_count; i++) {
        int idx = find_station_index_local(inst, station_ids[i]);
        if (idx < 0) goto fail;
        instance_station_ids[i] = station_ids[i];
        start_loc_ids[i] = inst->nodes[idx].start_loc_id;
        end_loc_ids[i] = inst->nodes[idx].end_loc_id;
        amounts[i] = inst->nodes[idx].amount;
    }

    mip_instance.num_stations = station_count;
    mip_instance.station_ids = instance_station_ids;
    mip_instance.station_start_loc_ids = start_loc_ids;
    mip_instance.station_end_loc_ids = end_loc_ids;
    mip_instance.station_amounts = amounts;
    mip_instance.distances = inst->distances;
    mip_instance.max_location_id = inst->max_loc_id;

    mip_params.shared_env = env;
    mip_params.verbose = 0;
    mip_params.time_limit_seconds = (time_limit_seconds > 0.0) ? time_limit_seconds : 0.0;
    mip_params.exclude_haul_distance = !include_haul_distance && !(haul_distance_scale > 0.0);
    mip_params.use_scaled_haul_distance = !include_haul_distance && (haul_distance_scale > 0.0);
    mip_params.haul_distance_scale = (!include_haul_distance && (haul_distance_scale > 0.0)) ? haul_distance_scale : 0.0;

    if (solve_mip_endpaired_tsp(&mip_instance, &mip_params,
                                start_loc_id, end_loc_id, &mip_solution) != 0) {
        goto fail;
    }

    *signed_station_ids_out = mip_solution.signed_station_ids;
    mip_solution.signed_station_ids = NULL;
    if (runtime_seconds_out) *runtime_seconds_out = mip_solution.runtime_seconds;
    if (gap_percent_out) *gap_percent_out = mip_solution.gap * 100.0;
    if (model_num_vars_out) *model_num_vars_out = mip_solution.model_num_vars;
    if (model_num_constrs_out) *model_num_constrs_out = mip_solution.model_num_constrs;

    free_mip_endpaired_solution(&mip_solution);
    free(instance_station_ids);
    free(start_loc_ids);
    free(end_loc_ids);
    free(amounts);
    return 1;

fail:
    free_mip_endpaired_solution(&mip_solution);
    free(instance_station_ids);
    free(start_loc_ids);
    free(end_loc_ids);
    free(amounts);
    return 0;
}
#endif

int init_apply_local_postopt(const nn_instance_t *inst,
                             const nn_solution_t *input,
                             int boat_start_loc_id,
                             int boat_end_loc_id,
                             double time_limit_seconds,
                             int include_haul_distance,
                             double haul_distance_scale,
                             nn_solution_t *output,
                             double *runtime_seconds_out,
                             int *segment_solve_count_out,
                             gsp_mip_solve_detail_t **solve_details_out,
                             int *solve_detail_count_out) {
    if (runtime_seconds_out) *runtime_seconds_out = 0.0;
    if (segment_solve_count_out) *segment_solve_count_out = 0;
    if (solve_details_out) *solve_details_out = NULL;
    if (solve_detail_count_out) *solve_detail_count_out = 0;
    if (!inst || !input || !output) return 0;
    init_free_solution(output);

#ifndef HAVE_GUROBI
    return init_copy_solution(input, output);
#else
    GRBenv *env = NULL;
    struct timespec t_postopt_start;
    int *tour = NULL, tour_cap = 0, tour_len = 0;
    int *visit_ids = NULL, visit_ids_cap = 0, visit_count = 0;
    int *visit_seg = NULL, visit_seg_cap = 0;
    int *visit_dir = NULL, visit_dir_cap = 0;
    int *segment_starts = NULL, seg_starts_cap = 0;
    int *segment_ends = NULL, seg_ends_cap = 0;
    int *segment_catches = NULL, seg_catches_cap = 0;
    double *segment_dists = NULL; int seg_dists_cap = 0;
    gsp_mip_solve_detail_t *solve_details = NULL;
    double total_distance = 0.0;
    int total_catch = 0;

    clock_gettime(CLOCK_MONOTONIC, &t_postopt_start);
    printf("[POSTOPT] Starting local segment post-opt for %d segments (time_limit=%s%.0f)\n",
           input->segment_count,
           (time_limit_seconds > 0.0) ? "" : "uncapped ",
           (time_limit_seconds > 0.0) ? time_limit_seconds : 0.0);
    fflush(stdout);

    if (GRBloadenv(&env, NULL) != 0) return 0;
    GRBsetintparam(env, "OutputFlag", 0);
    GRBsetintparam(env, "LogToConsole", 0);

    if (input->segment_count > 0) {
        solve_details = (gsp_mip_solve_detail_t*)calloc((size_t)input->segment_count,
                                                        sizeof(gsp_mip_solve_detail_t));
        if (!solve_details) goto fail;
    }

    for (int s = 0; s < input->segment_count; s++) {
        int station_count = 0;
        int start_loc_id;
        int end_loc_id;
        int *station_ids = NULL;
        int *signed_station_ids = NULL;
        double segment_runtime = 0.0;
        double segment_gap_percent = -1.0;
        double segment_distance = 0.0;
        double input_segment_distance = 0.0;
        int segment_model_num_vars = 0;
        int segment_model_num_constrs = 0;
        int current_loc_id;
        int segment_start_idx = tour_len;

        for (int i = 0; i < input->visit_station_count; i++) {
            if (input->visit_station_segment[i] == s) station_count++;
        }
        if (station_count <= 0) continue;

        station_ids = (int*)malloc((size_t)station_count * sizeof(int));
        if (!station_ids) goto fail;
        {
            int k = 0;
            for (int i = 0; i < input->visit_station_count; i++) {
                if (input->visit_station_segment[i] == s) station_ids[k++] = input->visit_station_ids[i];
            }
        }

        start_loc_id = (s == 0) ? boat_start_loc_id : input->tour[input->segment_ends[s - 1]];
        end_loc_id = (s == input->segment_count - 1) ? boat_end_loc_id : input->tour[input->segment_ends[s]];
        input_segment_distance = input->segment_dists[s];
        if (s == input->segment_count - 1 && input->tour_length > 0) {
            double return_dist = get_distance(inst, input->tour[input->tour_length - 1], boat_end_loc_id);
            if (return_dist > 0.0) input_segment_distance += return_dist;
        }

        printf("[POSTOPT] Segment %d/%d: %d stations, dock %d -> %d, baseline %.2f nm\n",
               s + 1, input->segment_count, station_count,
               start_loc_id, end_loc_id, input_segment_distance);
        fflush(stdout);

        if (!solve_segment_order(inst, station_ids, station_count, start_loc_id, end_loc_id,
                                 time_limit_seconds, include_haul_distance, haul_distance_scale,
                                 env, &signed_station_ids, &segment_runtime,
                                 &segment_gap_percent, &segment_model_num_vars,
                                 &segment_model_num_constrs)) {
            free(station_ids);
            goto fail;
        }
        free(station_ids);

        if (solve_details) {
            gsp_mip_solve_detail_init(&solve_details[s]);
            solve_details[s].segment_index = s + 1;
            solve_details[s].station_count = station_count;
            solve_details[s].node_count = station_count + 2;
            solve_details[s].moved_stations = count_segment_reordered_stations(input, s, signed_station_ids, station_count);
            solve_details[s].model_num_vars = segment_model_num_vars;
            solve_details[s].model_num_constrs = segment_model_num_constrs;
            solve_details[s].runtime_seconds = segment_runtime;
            solve_details[s].gap_percent = segment_gap_percent;
        }

        current_loc_id = start_loc_id;
        for (int i = 0; i < station_count; i++) {
            int signed_station_id = signed_station_ids[i];
            int station_idx = find_station_index_local(inst, abs(signed_station_id));
            int entry_loc;
            int exit_loc;
            double leg_dist;

            if (station_idx < 0) {
                free(signed_station_ids);
                goto fail;
            }

            entry_loc = (signed_station_id < 0) ? inst->nodes[station_idx].end_loc_id : inst->nodes[station_idx].start_loc_id;
            exit_loc = (signed_station_id < 0) ? inst->nodes[station_idx].start_loc_id : inst->nodes[station_idx].end_loc_id;

            leg_dist = get_distance(inst, current_loc_id, entry_loc);
            if (leg_dist > 0.0) segment_distance += leg_dist;
            if (exit_loc != entry_loc) {
                leg_dist = get_distance(inst, entry_loc, exit_loc);
                if (leg_dist > 0.0) segment_distance += leg_dist;
            }

            if (!grow_int_array(&tour, &tour_cap, tour_len + ((exit_loc != entry_loc) ? 2 : 1)) ||
                !grow_int_array(&visit_ids, &visit_ids_cap, visit_count + 1) ||
                !grow_int_array(&visit_seg, &visit_seg_cap, visit_count + 1) ||
                !grow_int_array(&visit_dir, &visit_dir_cap, visit_count + 1)) {
                free(signed_station_ids);
                goto fail;
            }

            tour[tour_len++] = entry_loc;
            if (exit_loc != entry_loc) tour[tour_len++] = exit_loc;
            visit_ids[visit_count] = abs(signed_station_id);
            visit_seg[visit_count] = s;
            visit_dir[visit_count] = (signed_station_id < 0) ? -1 : 1;
            visit_count++;
            total_catch += inst->nodes[station_idx].amount;
            current_loc_id = exit_loc;
        }

        if (s < input->segment_count - 1) {
            double leg_dist = get_distance(inst, current_loc_id, end_loc_id);
            if (leg_dist > 0.0) segment_distance += leg_dist;
            if (!grow_int_array(&tour, &tour_cap, tour_len + 1)) {
                free(signed_station_ids);
                goto fail;
            }
            tour[tour_len++] = end_loc_id;
            current_loc_id = end_loc_id;
        }

        if (!grow_int_array(&segment_starts, &seg_starts_cap, output->segment_count + 1) ||
            !grow_int_array(&segment_ends, &seg_ends_cap, output->segment_count + 1) ||
            !grow_int_array(&segment_catches, &seg_catches_cap, output->segment_count + 1) ||
            !grow_dist_array(&segment_dists, &seg_dists_cap, output->segment_count + 1)) {
            free(signed_station_ids);
            goto fail;
        }

        segment_starts[output->segment_count] = segment_start_idx;
        segment_ends[output->segment_count] = tour_len - 1;
        segment_catches[output->segment_count] = input->segment_catches[s];
        segment_dists[output->segment_count] = segment_distance;
        output->segment_count++;
        total_distance += segment_distance;
        if (runtime_seconds_out) *runtime_seconds_out += segment_runtime;
        if (segment_solve_count_out) (*segment_solve_count_out)++;

        printf("[POSTOPT] Segment %d/%d done: %.2f -> %.2f nm (improvement %.2f nm) in %.2f s\n",
               s + 1, input->segment_count,
               input_segment_distance,
               segment_distance,
               input_segment_distance - segment_distance,
               segment_runtime);
        fflush(stdout);

        if (s == input->segment_count - 1) {
            double return_dist = get_distance(inst, current_loc_id, boat_end_loc_id);
            if (return_dist > 0.0) total_distance += return_dist;
        }

        free(signed_station_ids);
    }

    output->tour = tour;
    output->tour_length = tour_len;
    output->visit_station_ids = visit_ids;
    output->visit_station_count = visit_count;
    output->visit_station_segment = visit_seg;
    output->visit_station_direction = visit_dir;
    output->segment_starts = segment_starts;
    output->segment_ends = segment_ends;
    output->segment_catches = segment_catches;
    output->segment_dists = segment_dists;
    output->total_distance = total_distance;
    output->total_catch = total_catch;

    {
        struct timespec t_postopt_end;
        clock_gettime(CLOCK_MONOTONIC, &t_postopt_end);
        printf("[POSTOPT] Completed local post-opt: %.2f nm total in %.2f s wall time\n",
               total_distance,
               (double)(t_postopt_end.tv_sec - t_postopt_start.tv_sec) +
               (double)(t_postopt_end.tv_nsec - t_postopt_start.tv_nsec) / 1e9);
        fflush(stdout);
    }

    GRBfreeenv(env);
    if (solve_details_out) *solve_details_out = solve_details;
    else free(solve_details);
    if (solve_detail_count_out) *solve_detail_count_out = input->segment_count;
    return 1;

fail:
    if (env) GRBfreeenv(env);
    init_free_solution(output);
    free(tour);
    free(visit_ids);
    free(visit_seg);
    free(visit_dir);
    free(segment_starts);
    free(segment_ends);
    free(segment_catches);
    free(segment_dists);
    free(solve_details);
    return 0;
#endif
}
