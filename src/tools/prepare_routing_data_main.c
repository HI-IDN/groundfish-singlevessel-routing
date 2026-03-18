#include "../include/country_bootstrap.h"
#include "../include/distance_builder.h"
#include "../include/station_import.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *argv0) {
    fprintf(stderr,
            "Usage: %s --db <gsp_data.db> --coastline-file <island.tsv> --stations-file <stations.dat> [options]\n"
            "\n"
            "Options:\n"
            "  --waypoint-file <waypoints.dat>\n"
            "  --port-file <ports.dat>\n"
            "  --boat-file <boats.dat>\n",
            argv0);
}

int main(int argc, char **argv) {
    const char *db_path = NULL;
    const char *coastline_file = NULL;
    const char *stations_file = NULL;
    int country_argc = 1;

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "--db") == 0) && i + 1 < argc) {
            db_path = argv[i + 1];
            country_argc += 2;
            i++;
        } else if ((strcmp(argv[i], "--coastline-file") == 0) && i + 1 < argc) {
            coastline_file = argv[i + 1];
            country_argc += 2;
            i++;
        } else if ((strcmp(argv[i], "--stations-file") == 0) && i + 1 < argc) {
            stations_file = argv[i + 1];
            i++;
        } else if ((strcmp(argv[i], "--waypoint-file") == 0 ||
                    strcmp(argv[i], "--dat") == 0 ||
                    strcmp(argv[i], "--port-file") == 0 ||
                   strcmp(argv[i], "--boat-file") == 0) && i + 1 < argc) {
            country_argc += 2;
            i++;
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    if (!db_path || !coastline_file || !stations_file) {
        usage(argv[0]);
        return 1;
    }

    char **country_argv = (char**)calloc((size_t)country_argc + 2, sizeof(char*));
    if (!country_argv) {
        fprintf(stderr, "Out of memory\n");
        return 1;
    }

    int out = 0;
    country_argv[out++] = "gsp_prepare_routing_data.country";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--stations-file") == 0) {
            i++;
            continue;
        }
        country_argv[out++] = argv[i];
    }
    country_argv[out] = NULL;

    printf("=== GSP Prepare Routing Data ===\n");
    printf("Step 1/4: base country bootstrap\n");
    country_argv[out++] = "--skip-waypoints";
    country_argv[out] = NULL;
    if (country_bootstrap_run(out, country_argv) != 0) {
        free(country_argv);
        return 1;
    }

    {
        char *station_argv[] = {
            "gsp_prepare_routing_data.stations",
            (char*)stations_file,
            (char*)db_path,
            NULL
        };
        printf("Step 2/4: import stations\n");
        if (station_import_run(3, station_argv) != 0) {
            free(country_argv);
            return 1;
        }
    }

    {
        country_argv[out - 1] = "--waypoints-only";
        country_argv[out] = NULL;
        printf("Step 3/4: insert waypoint rows\n");
        if (country_bootstrap_run(out, country_argv) != 0) {
            free(country_argv);
            return 1;
        }
    }

    {
        char *distance_argv[] = {
            "gsp_prepare_routing_data.distance",
            "--db",
            (char*)db_path,
            NULL
        };
        printf("Step 4/4: build distances and report waypoint usage\n");
        if (distance_builder_run(3, distance_argv) != 0) {
            free(country_argv);
            return 1;
        }
    }

    free(country_argv);
    printf("Routing data preparation complete\n");
    return 0;
}
