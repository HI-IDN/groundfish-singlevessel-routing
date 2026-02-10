Shared Headers for Groundfish Survey Routing
==============================================

Overview
--------

This folder contains all shared header files used across the pipeline. Each header defines interfaces, data structures, and constants for use by preprocessing, initialization, sweep, MIP, and common modules.

Headers
-------

### `db_schema.h`

SQLite database table schemas and initialization.

**Contents:**
- SQL DDL statements for all tables (locations, ports, boats, stations, waypoints, solutions, sweep_runs, metadata)
- Foreign key relationships
- Index definitions for performance
- Default data constraints

**Usage:**
```c
#include "include/db_schema.h"

sqlite3 *db = db_open("data.sqlite");
db_create_schema(db);  // Initializes all tables
```

**Tables defined:**
- `locations` — All geographic points
- `ports` — Harbor locations
- `boats` — Vessel definitions
- `stations` — Survey stations
- `waypoints` — Navigational aids
- `solutions` — Stored solutions (tours, distances, metadata)
- `sweep_runs` — Sweep optimization trajectories
- `metadata` — Pipeline state and run info

---

### `data_types.h`

Shared data structures used throughout the pipeline.

**Structures:**

#### `location_t`
```c
typedef struct {
    int id;
    char type;              // 'P' (port), 'S1'/'S2' (station), 'W' (waypoint)
    int lat_degmin;         // Integer degmin format
    int lon_degmin;
    double lat_decimal;     // Decimal degrees
    double lon_decimal;
} location_t;
```

#### `station_t`
```c
typedef struct {
    int id;
    int ext_id;             // External identifier
    int subid;
    int flag;
    location_t *start;      // S1 location
    location_t *end;        // S2 location
    int bottom_depth_cast;  // Extracted from comment, botndypi_kastad
    int bottom_depth_haul;  // Extracted from comment, botndypi_hift    
    char *remark;
} station_t;
```

#### `boat_t`
```c
typedef struct {
    int id;
    location_t *start;
    location_t *end;
    int capacity;
    int c1, c2, c3, c4, c5, c6;  // Extra columns
    char *name;
} boat_t;
```

#### `instance_t`
```c
typedef struct {
    int num_nodes;
    int num_stations;
    int num_ports;
    int num_waypoints;
    
    location_t *nodes;          // All locations (indexed)
    station_t *stations;        // Survey stations
    boat_t *boats;
    
    double *dist_matrix;        // [num_nodes][num_nodes]
    int *feasible_matrix;       // [num_nodes][num_nodes] (binary)
    double vessel_capacity;
    
    char *timestamp;
    char *data_source;
} instance_t;
```

#### `init_result_t`
```c
typedef struct {
    char *strategy;             // "nn", "ci", "ge", "opt"
    int *tour;                  // [tour_length]
    int tour_length;
    double total_distance;
    double total_distance_no_return;
    double runtime_seconds;
    int mip_status;             // If strategy == "opt"
    double mip_gap;
    int mip_iterations;
    char **logs;                // Array of log strings
    int num_logs;
    char *timestamp;
} init_result_t;
```

#### `trajectory_point_t`
```c
typedef struct {
    int l2seg;
    int iteration;
    double total_distance;
    double improvement;
    double improvement_percent;
    int num_segments_tested;
    int num_segments_improved;
    double elapsed_seconds;
    char *timestamp;
} trajectory_point_t;
```

#### `sweep_result_t`
```c
typedef struct {
    init_result_t *initial_solution;
    int *best_tour;
    int best_tour_length;
    double best_distance;
    int best_l2seg;
    int best_iteration;
    
    trajectory_point_t *trajectory;
    int trajectory_length;
    
    double total_runtime_seconds;
    int total_mip_calls;
    double total_improvement;
    double total_improvement_percent;
    
    char **logs;
    int num_logs;
    char *timestamp;
} sweep_result_t;
```

---

### `logging.h`

Logging interface (see `common/README.md` for details).

**Key exports:**
```c
typedef enum { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR } log_level_t;

void log_init(const char *module_name, log_level_t level);
void log_debug(const char *fmt, ...);
void log_info(const char *fmt, ...);
void log_warn(const char *fmt, ...);
void log_error(const char *fmt, ...);
void log_close(void);
```

---

### `db_helpers.h`

SQLite database connection and query helpers (see `common/README.md` for details).

**Key exports:**
```c
sqlite3 *db_open(const char *path);
void db_close(sqlite3 *db);
int db_create_schema(sqlite3 *db);
int db_exec(sqlite3 *db, const char *sql, ...);
sqlite3_stmt *db_query(sqlite3 *db, const char *sql, ...);
```

---

### `output.h`

Output serialization (JSON, CSV) (see `common/README.md` for details).

**Key exports:**
```c
char *serialize_solution_json(const init_result_t *sol);
char *serialize_solution_csv(const init_result_t *sol);
char *serialize_trajectory_json(const trajectory_point_t *traj, int len, const int *best_tour);
int write_solution_json(const char *filepath, const init_result_t *sol);
int append_trajectory_csv(const char *filepath, const trajectory_point_t *point);
```

---

### `math.h`

Math utilities for coordinate conversion and distance calculation (see `common/README.md` for details).

**Key exports:**
```c
double degmin_to_decimal(int degmin_value);
double decimal_to_radians(double degrees);
double haversine_distance(double lat1, double lon1, double lat2, double lon2);
double euclidean_distance(double x1, double y1, double x2, double y2);
```

---

### `constants.h`

Global constants and enumerations.

**Contents:**
```c
// Node types
enum {
    NODE_TYPE_PORT = 'P',
    NODE_TYPE_STATION_1 = 'S',  // First coord of station
    NODE_TYPE_STATION_2 = 'T',  // Second coord of station
    NODE_TYPE_WAYPOINT = 'W',
    NODE_TYPE_BOAT = 'B'
};

// Log levels (duplicated from logging.h for convenience)
enum {
    LOG_DEBUG = 0,
    LOG_INFO = 1,
    LOG_WARN = 2,
    LOG_ERROR = 3
};

// MIP statuses
enum {
    MIP_STATUS_OPTIMAL = 2,
    MIP_STATUS_SUBOPTIMAL = 9,
    MIP_STATUS_TIME_LIMIT = 9,
    MIP_STATUS_INFEASIBLE = 3
};

// Maximum sizes
#define MAX_NODES 10000
#define MAX_STATIONS 5000
#define MAX_WAYPOINTS 1000
#define MAX_LOG_LINES 10000
#define MAX_TOUR_LENGTH 10000

// Default parameters
#define DEFAULT_MIP_TIME_LIMIT 3600  // 1 hour
#define DEFAULT_L2SEG_MIN 60
#define DEFAULT_L2SEG_MAX 480
#define DEFAULT_L2SEG_STEP 60
#define DEFAULT_SEGMENT_STRIDE 30
```

---

### `mip_capacity_aware.h`

Interface for capacity-aware MIP solver (see `mip/README.md` for details).

**Key exports:**
```c
typedef struct { /* fields */ } mip_instance_t;
typedef struct { /* fields */ } mip_solution_t;

int solve_mip_capacity_aware(
    const mip_instance_t *instance,
    const mip_params_t *params,
    mip_solution_t *solution
);
```

---

### `mip_noport.h`

Interface for no-port MIP solver (see `mip/README.md` for details).

**Key exports:**
```c
int solve_mip_noport(
    const mip_instance_t *instance,
    const mip_params_t *params,
    mip_solution_t *solution
);
```

---

### `mip_endpaired_tsp.h`

Interface for end-paired TSP solver (see `mip/README.md` for details).

**Key exports:**
```c
int solve_mip_endpaired_tsp(
    const mip_instance_t *instance,
    const mip_params_t *params,
    int start_node,
    int end_node,
    mip_solution_t *solution
);
```

---

Building & Usage
----------------

### Include Paths

All source files include common headers via:

```c
#include "include/db_schema.h"
#include "include/data_types.h"
#include "include/logging.h"
#include "common/logging.h"  // Implementation
```

### Compiler Flags

Ensure `-I./include` is in CFLAGS:

```makefile
CFLAGS = -O2 -std=c11 -Wall -Wextra -I./include
```

### Header Dependencies

```
data_types.h
  ├─ constants.h (enums, max sizes)
  └─ (No other dependencies)

db_schema.h
  └─ (sqlite3.h is included at compile time)

logging.h
  └─ (No dependencies)

db_helpers.h
  ├─ logging.h (for error logging)
  └─ (sqlite3.h at compile time)

output.h
  ├─ data_types.h (solution_t, etc.)
  └─ logging.h (for errors)

math.h
  ├─ constants.h (for PI, etc.)
  └─ (math.h C library)

mip_*.h
  ├─ data_types.h (mip_instance_t, etc.)
  ├─ logging.h (for debug output)
  └─ (gurobi_c.h at compile time)
```

Forward Declarations
--------------------

To avoid circular dependencies, critical headers use forward declarations:

```c
// In data_types.h
typedef struct instance instance_t;  // Forward declare
// Defined later in db_helpers.h
```

Extending Headers
------------------

### Adding a New Data Structure

1. Add typedef to `data_types.h`
2. Update documentation in this README
3. Update `constants.h` if adding new enum values
4. Rebuild: `make clean && make all`

### Adding a New MIP Model

1. Create `mip_*.h` in this folder
2. Export solver function and param/result structs
3. Add `#include "include/mip_*.h"` to relevant source files
4. Update `mip/` Makefile to compile new model
5. Update this README

Testing & Validation
--------------------

```bash
# Compile header syntax check (no code gen)
cc -I. -fsyntax-only include/*.h

# Run unit tests for data structures
make -C .. test-headers
```

References
----------

- All `.h` files are documented in their corresponding module READMEs:
  - `preprocess/README.md` — schema definitions
  - `init/README.md` — init_result_t usage
  - `sweep/README.md` — trajectory_point_t, sweep_result_t usage
  - `mip/README.md` — MIP-related structs
  - `common/README.md` — logging, db, output, math interfaces

