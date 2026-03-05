/* GSP Phase 1: Sweep/Improvement Solver
 * Local search and refinement strategies
 * Supports: OPT, NN, GE, CI strategies (as improvement methods)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Parse command-line arguments for sweep mode */
void parse_sweep_args(int argc, char **argv,
                      const char **strategy, const char **database,
                      const char **config, const char **input,
                      const char **output, int *time_limit) {
    *strategy = NULL;
    *database = NULL;
    *config = NULL;
    *input = NULL;
    *output = NULL;
    *time_limit = 0;

    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--strategy") == 0) {
            *strategy = argv[i + 1];
        } else if (strcmp(argv[i], "--database") == 0) {
            *database = argv[i + 1];
        } else if (strcmp(argv[i], "--config") == 0) {
            *config = argv[i + 1];
        } else if (strcmp(argv[i], "--input") == 0) {
            *input = argv[i + 1];
        } else if (strcmp(argv[i], "--output") == 0) {
            *output = argv[i + 1];
        } else if (strcmp(argv[i], "--time-limit") == 0) {
            *time_limit = atoi(argv[i + 1]);
        }
    }
}

/* Mode: sweep */
int mode_sweep(int argc, char **argv) {
    printf("============================================================\n");
    printf("GSP Solver - Phase 1: Improvement/Sweep\n");
    printf("============================================================\n\n");

    const char *strategy, *database, *config, *input, *output;
    int time_limit;

    parse_sweep_args(argc, argv, &strategy, &database, &config, &input, &output, &time_limit);

    /* Validate required arguments */
    if (!strategy) {
        fprintf(stderr, "ERROR: Missing --strategy argument\n");
        fprintf(stderr, "Valid strategies: nn, ge, ci, opt\n");
        return 1;
    }

    if (!database) {
        fprintf(stderr, "ERROR: Missing --database argument\n");
        return 1;
    }

    if (!config) {
        fprintf(stderr, "ERROR: Missing --config argument\n");
        return 1;
    }

    /* Validate strategy */
    if (strcmp(strategy, "nn") != 0 && strcmp(strategy, "ge") != 0 &&
        strcmp(strategy, "ci") != 0 && strcmp(strategy, "opt") != 0) {
        fprintf(stderr, "ERROR: Unknown strategy '%s'\n", strategy);
        fprintf(stderr, "Valid strategies: nn, ge, ci, opt\n");
        return 1;
    }

    /* Check if strategy is implemented */
    fprintf(stderr, "ERROR: Strategy '%s' is not yet implemented for sweep mode\n", strategy);
    fprintf(stderr, "\nComing soon:\n");
    fprintf(stderr, "  - nn: Nearest Neighbor improvement\n");
    fprintf(stderr, "  - ge: Greedy Edge improvement\n");
    fprintf(stderr, "  - ci: Cheapest Insertion improvement\n");
    fprintf(stderr, "  - opt: Optimal NP-MIP solver\n");
    return 1;

    printf("[TODO] Sweep mode not yet implemented\n");
    printf("[TODO] Input file: %s\n", input ? input : "(none)");
    printf("[TODO] Output file: %s\n", output ? output : "(default)");
    printf("[TODO] Time limit: %d seconds\n", time_limit);

    return 0;
}

