/*
 * Data Preparation Utility
 * Parse .dat file and prepare data for optimization
 * TODO: Write to SQLite database for efficient access
 */

#include "../include/dat_parser.h"
#include "../include/exdata.h"
#include "../include/distance.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <datafile.dat>\n", argv[0]);
        fprintf(stderr, "  Parses .dat file and prepares data for optimization\n");
        fprintf(stderr, "\nExample:\n");
        fprintf(stderr, "  %s ../../dat/data2023spring.dat\n", argv[0]);
        return 1;
    }

    const char *dat_file = argv[1];

    printf("=== GSP Data Preparation ===\n");
    printf("Input file: %s\n\n", dat_file);

    /* Parse entire .dat file (all boats) */
    printf("Parsing .dat file...\n");
    ItemVec all_items;
    item_vec_init(&all_items);
    read_dat_file_all_boats(dat_file, &all_items, 0);

    printf("  ✓ Loaded %d total items\n", all_items.n);

    /* List all boats found */
    char **boat_names = NULL;
    int n_boats = get_boat_names(&all_items, &boat_names);
    printf("\n=== Boats Found: %d ===\n", n_boats);
    for (int i = 0; i < n_boats; i++) {
        /* Get boat capacity */
        double cap = 0.0;
        for (int j = 0; j < all_items.n; j++) {
            if (all_items.a[j].Type == tSHIP &&
                strcmp(all_items.a[j].Name, boat_names[i]) == 0) {
                cap = all_items.a[j].BoatData[4];
                break;
            }
        }
        printf("  [%d] %-30s (capacity: %.0f)\n", i, boat_names[i], cap);
    }

    if (n_boats == 0) {
        printf("  ✗ No boats found in file!\n");
        item_vec_free(&all_items);
        return 1;
    }

    /* Count items by type across all boats */
    int total_stat = 0, total_port = 0, total_wayp = 0;
    int total_port_selected = 0;
    for (int i = 0; i < all_items.n; i++) {
        switch (all_items.a[i].Type) {
            case tSTAT: total_stat++; break;
            case tPORT:
                total_port++;
                if (all_items.a[i].PortSelected) total_port_selected++;
                break;
            case tWAYP: total_wayp++; break;
        }
    }

    printf("\n=== Data Summary ===\n");
    printf("  Boats:       %d\n", n_boats);
    printf("  Stations:    %d\n", total_stat);
    printf("  Ports:       %d (%d selected)\n", total_port, total_port_selected);
    printf("  Waypoints:   %d\n", total_wayp);
    printf("  Total items: %d\n", all_items.n);

    /* Show statistics per boat */
    printf("\n=== Per-Boat Statistics ===\n");
    fflush(stdout);

    for (int boat_idx = 0; boat_idx < n_boats; boat_idx++) {
        ItemVec boat_items;
        item_vec_init(&boat_items);
        double ship_cap = 0.0;

        printf("  Processing boat [%d]...\n", boat_idx);
        fflush(stdout);

        filter_items_by_boat(&all_items, boat_idx, &boat_items, &ship_cap);

        int count_stat = 0, count_port = 0, count_port_sel = 0;
        for (int i = 0; i < boat_items.n; i++) {
            if (boat_items.a[i].Type == tSTAT) count_stat++;
            else if (boat_items.a[i].Type == tPORT) {
                count_port++;
                if (boat_items.a[i].PortSelected) count_port_sel++;
            }
        }

        printf("  [%d] %s\n", boat_idx, boat_names[boat_idx]);
        printf("      Capacity: %.0f\n", ship_cap);
        printf("      Stations: %d\n", count_stat);
        printf("      Ports:    %d (%d selected)\n", count_port, count_port_sel);
        fflush(stdout);

        item_vec_free(&boat_items);
    }

    printf("\n=== Next Steps ===\n");
    fflush(stdout);
    printf("  TODO: Write parsed data to SQLite database\n");
    printf("  TODO: Compute distance matrices for each boat\n");
    printf("  TODO: Cache results for fast access by init/sweep modules\n");
    printf("\nData preparation complete.\n");
    printf("Use this data with init/ and sweep/ modules to solve routing.\n");
    fflush(stdout);

    /* Cleanup */
    item_vec_free(&all_items);
    for (int i = 0; i < n_boats; i++) {
        free(boat_names[i]);
    }
    free(boat_names);

    return 0;
}

