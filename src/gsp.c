/* GSP Unified Solver
 * Entry point supporting workflow-stage modes:
 *   --mode construction : construction / segment stage entrypoint
 *   --mode refinement   : refinement stage entrypoint
 *
 * Compatibility aliases:
 *   --mode init  -> construction
 *   --mode sweep -> refinement
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Forward declarations for mode handlers */
int mode_construction(int argc, char **argv);
int mode_refinement(int argc, char **argv);

void print_usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s --mode <construction|refinement> [options]\n", prog_name);
    fprintf(stderr, "\n");
    fprintf(stderr, "Modes:\n");
    fprintf(stderr, "  construction   construction / segment stage entrypoint\n");
    fprintf(stderr, "  refinement     matheuristic refinement stage entrypoint\n");
    fprintf(stderr, "  init           compatibility alias for construction\n");
    fprintf(stderr, "  sweep          compatibility alias for refinement\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Construction mode options:\n");
    fprintf(stderr, "  --mode construction --strategy <nn|ge|ci>\n");
    fprintf(stderr, "              --database <path>     (required: gsp_data.db)\n");
    fprintf(stderr, "              --config <path>       (required: gsp_solver.yaml)\n");
    fprintf(stderr, "              --output <path>       (optional: sol/<strategy>/construction.json or segment.json)\n");
    fprintf(stderr, "Refinement mode options:\n");
    fprintf(stderr, "  --mode refinement --strategy <nn|ge|ci|noport>\n");
    fprintf(stderr, "              --database <path>     (required: gsp_data.db)\n");
    fprintf(stderr, "              --config <path>       (required: gsp_solver.yaml)\n");
    fprintf(stderr, "              --input <path>        (required: segment.json)\n");
    fprintf(stderr, "              --output <path>       (optional: sol/<strategy>/refinement.json)\n");
    fprintf(stderr, "              --time-limit <seconds> (optional)\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "  %s --mode construction --strategy nn --database dat/gsp_data.db --config config/gsp_solver.yaml --output sol/nn/construction.json\n", prog_name);
    fprintf(stderr, "  %s --mode refinement --strategy nn --database dat/gsp_data.db --config config/gsp_solver.yaml --input sol/nn/segment.json --output sol/nn/refinement.json\n", prog_name);
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

    if (strcmp(mode, "construction") == 0 || strcmp(mode, "init") == 0) {
        return mode_construction(argc, argv);
    } else if (strcmp(mode, "refinement") == 0 || strcmp(mode, "sweep") == 0) {
        return mode_refinement(argc, argv);
    } else {
        fprintf(stderr, "ERROR: Unknown mode '%s'\n", mode);
        fprintf(stderr, "Valid modes: construction, refinement (aliases: init, sweep)\n\n");
        print_usage(argv[0]);
        return 1;
    }
}

