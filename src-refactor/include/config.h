#ifndef GSP_CONFIG_H
#define GSP_CONFIG_H

#include <sqlite3.h>

/* Configuration structure for GSP solver */
typedef struct {
    /* Boat configuration */
    int boat_id;
    char *boat_name;
    int capacity_kgs;

    /* Instance parameters */
    int total_stations;
    int total_catch_kgs;
    int min_segments;
    int default_segments;

    /* Sweep parameters */
    int l2seg_value;
    int l1seg_value;

    /* MIP solver parameters */
    int mip_verbose;

    /* Iteration control */
    int max_iterations;
    int log_interval;

    /* Gurobi parameters */
    char *gurobi_env_log_file;
    int gurobi_threads;
    int gurobi_mip_focus;
    int gurobi_seed;

    /* Database configuration */
    char *db_path;
    int auto_create_schema;

    /* Output */
    int verbose;
    int log_progress_interval;
    int json_format;


} gsp_config_t;

/* Load configuration from YAML file */
int config_load_from_yaml(const char *yaml_path, gsp_config_t *config);

/* Load configuration from database metadata table */
int config_load_from_db(sqlite3 *db, int boat_id, const char *run_type, gsp_config_t *config);

/* Save configuration to database metadata table */
int config_save_to_db(sqlite3 *db, const gsp_config_t *config, const char *run_type, const char *run_name);

/* Free configuration resources */
void config_free(gsp_config_t *config);

/* Print configuration to stdout */
void config_print(const gsp_config_t *config, const char *phase);

#endif

