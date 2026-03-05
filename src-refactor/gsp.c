/* GSP Unified Solver
 * Entry point supporting two modes:
 *   --mode init    : Initialization phase (strategies: nn, ge, ci, opt)
 *   --mode sweep   : Improvement phase (sweep/local search)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Forward declarations for mode handlers */
int mode_init(int argc, char **argv);
int mode_sweep(int argc, char **argv);

void print_usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s --mode <init|sweep> [options]\n", prog_name);
    fprintf(stderr, "\n");
    fprintf(stderr, "Modes:\n");
    fprintf(stderr, "  init    Initialization phase (construct initial solution)\n");
    fprintf(stderr, "  sweep   Improvement phase (local search/refinement)\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Init mode options:\n");
    fprintf(stderr, "  --mode init --strategy <nn|ge|ci|opt>\n");
    fprintf(stderr, "              --database <path>     (required: gsp_data.db)\n");
    fprintf(stderr, "              --config <path>       (required: gsp_solver.yaml)\n");
    fprintf(stderr, "              --output <path>       (optional: sol/init_<strategy>.json)\n");
    fprintf(stderr, "              --time-limit <seconds> (optional: for opt strategy)\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Sweep mode options:\n");
    fprintf(stderr, "  --mode sweep --strategy <nn|ge|ci|opt>\n");
    fprintf(stderr, "              --database <path>     (required: gsp_data.db)\n");
    fprintf(stderr, "              --config <path>       (required: gsp_solver.yaml)\n");
    fprintf(stderr, "              --input <path>        (optional: previous solution)\n");
    fprintf(stderr, "              --output <path>       (optional: sol/sweep_<strategy>.json)\n");
    fprintf(stderr, "              --time-limit <seconds> (optional)\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "  %s --mode init --strategy nn --database dat/gsp_data.db --config config/gsp_solver.yaml\n", prog_name);
    fprintf(stderr, "  %s --mode sweep --strategy nn --database dat/gsp_data.db --config config/gsp_solver.yaml\n", prog_name);
}

int main(int argc, char **argv) {
    printf("============================================================\n");
    printf("GSP Solver - Unified Entry Point\n");
    printf("============================================================\n\n");

    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    /* Parse --mode argument */
    const char *mode = NULL;
    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--mode") == 0) {
            mode = argv[i + 1];
            break;
        }
    }

    if (!mode) {
        fprintf(stderr, "ERROR: Missing --mode argument\n\n");
        print_usage(argv[0]);
        return 1;
    }

    printf("Mode: %s\n\n", mode);

    if (strcmp(mode, "init") == 0) {
        return mode_init(argc, argv);
    } else if (strcmp(mode, "sweep") == 0) {
        return mode_sweep(argc, argv);
    } else {
        fprintf(stderr, "ERROR: Unknown mode '%s'\n", mode);
        fprintf(stderr, "Valid modes: init, sweep\n\n");
        print_usage(argv[0]);
        return 1;
    }
}

