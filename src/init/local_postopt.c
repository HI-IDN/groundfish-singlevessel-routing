#include "local_postopt.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "init_utils.h"

#ifdef HAVE_GUROBI
#include "../mip/include/mip_endpaired_tsp.h"
#endif

#define MAX_LINE 1024

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

double read_init_local_postopt_time_limit_from_yaml(const char *yaml_path) {
    FILE *fp = fopen(yaml_path, "r");
    char line[MAX_LINE];
    int in_init = 0;
    int in_local_post_opt = 0;
    double time_limit = 0.0;

    if (!fp) return 0.0;

    while (fgets(line, sizeof(line), fp)) {
        char *trimmed = line;
        while (*trimmed && isspace((unsigned char)*trimmed)) trimmed++;
        if (*trimmed == '#' || *trimmed == '\0' || *trimmed == '\n') continue;

        if (!isspace((unsigned char)line[0])) {
            in_init = (strncmp(trimmed, "init:", 5) == 0);
            in_local_post_opt = 0;
            continue;
        }
        if (!in_init) continue;

        if (trimmed == line + 2 && strncmp(trimmed, "local_post_opt:", 15) == 0) {
            in_local_post_opt = 1;
            continue;
        }
        if (trimmed == line + 2 && strncmp(trimmed, "local_post_opt:", 15) != 0) {
            in_local_post_opt = 0;
        }
        if (in_local_post_opt && strncmp(trimmed, "time_limit_seconds:", 19) == 0) {
            time_limit = atof(trimmed + 19);
            break;
        }
    }

    fclose(fp);
    return time_limit;
}

static int find_station_index_local(const nn_instance_t *inst, int station_id) {
    for (int i = 0; i < inst->num_stations; i++) {
        if (inst->nodes[i].table_id == station_id) return i;
    }
    return -1;
}

#ifdef HAVE_GUROBI
static int solve_segment_order(const nn_instance_t *inst,
                               const int *station_ids,
                               int station_count,
                               int start_loc_id,
                               int end_loc_id,
                               double time_limit_seconds,
                               GRBenv *env,
                               int **signed_station_ids_out,
                               double *runtime_seconds_out) {
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

    if (solve_mip_endpaired_tsp(&mip_instance, &mip_params,
                                start_loc_id, end_loc_id, &mip_solution) != 0) {
        goto fail;
    }

    *signed_station_ids_out = mip_solution.signed_station_ids;
    mip_solution.signed_station_ids = NULL;
    if (runtime_seconds_out) *runtime_seconds_out = mip_solution.runtime_seconds;

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
                             nn_solution_t *output,
                             double *runtime_seconds_out,
                             int *segment_solve_count_out) {
    if (runtime_seconds_out) *runtime_seconds_out = 0.0;
    if (segment_solve_count_out) *segment_solve_count_out = 0;
    if (!inst || !input || !output) return 0;
    init_free_solution(output);

#ifndef HAVE_GUROBI
    return init_copy_solution(input, output);
#else
    GRBenv *env = NULL;
    int *tour = NULL, tour_cap = 0, tour_len = 0;
    int *visit_ids = NULL, visit_ids_cap = 0, visit_count = 0;
    int *visit_seg = NULL, visit_seg_cap = 0;
    int *visit_dir = NULL, visit_dir_cap = 0;
    int *segment_starts = NULL, seg_starts_cap = 0;
    int *segment_ends = NULL, seg_ends_cap = 0;
    int *segment_catches = NULL, seg_catches_cap = 0;
    double *segment_dists = NULL; int seg_dists_cap = 0;
    double total_distance = 0.0;
    int total_catch = 0;

    if (GRBloadenv(&env, NULL) != 0) return 0;
    GRBsetintparam(env, "OutputFlag", 0);
    GRBsetintparam(env, "LogToConsole", 0);

    for (int s = 0; s < input->segment_count; s++) {
        int station_count = 0;
        int start_loc_id;
        int end_loc_id;
        int *station_ids = NULL;
        int *signed_station_ids = NULL;
        double segment_runtime = 0.0;
        double segment_distance = 0.0;
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

        if (!solve_segment_order(inst, station_ids, station_count, start_loc_id, end_loc_id,
                                 time_limit_seconds, env, &signed_station_ids, &segment_runtime)) {
            free(station_ids);
            goto fail;
        }
        free(station_ids);

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

    GRBfreeenv(env);
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
    return 0;
#endif
}
