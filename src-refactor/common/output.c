#include "../include/output.h"

char *serialize_solution_json(const init_result_t *sol) {
    (void)sol;
    return (char *)0;
}

char *serialize_solution_csv(const init_result_t *sol) {
    (void)sol;
    return (char *)0;
}

char *serialize_trajectory_json(const trajectory_point_t *traj, int len, const int *best_tour) {
    (void)traj;
    (void)len;
    (void)best_tour;
    return (char *)0;
}

int write_solution_json(const char *filepath, const init_result_t *sol) {
    (void)filepath;
    (void)sol;
    return 0;
}

int append_trajectory_csv(const char *filepath, const trajectory_point_t *point) {
    (void)filepath;
    (void)point;
    return 0;
}

