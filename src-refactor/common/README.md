Common Utilities for Groundfish Survey Routing
================================================

Overview
--------

The `common/` module provides shared utilities and interfaces used across all pipeline stages (preprocessing, initialization, sweep, MIP). This includes:

- **Logging framework** — Consistent timestamp/level/module-prefixed logging
- **Database helpers** — SQLite connection pooling, query builders, result parsing
- **Data serialization** — JSON/CSV output functions
- **Math utilities** — Distance calculations (geodetic, Euclidean), coordinate conversion

Architecture
------------

```
common/
  ├── logging.c / logging.h
  │   ├── log_debug()
  │   ├── log_info()
  │   ├── log_warn()
  │   └── log_error()
  ├── db_helpers.c / db_helpers.h
  │   ├── db_open()
  │   ├── db_close()
  │   ├── db_exec()
  │   ├── db_query()
  │   └── db_*_table() (schema creation)
  ├── output.c / output.h
  │   ├── serialize_solution_json()
  │   ├── serialize_solution_csv()
  │   ├── serialize_trajectory_json()
  │   └── serialize_trajectory_csv()
  └── math.c / math.h
      ├── degmin_to_decimal()
      ├── decimal_to_radians()
      ├── haversine_distance()
      └── euclidean_distance()
```

Logging Framework
-----------------

### Interface

```c
// logging.h

typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO = 1,
    LOG_WARN = 2,
    LOG_ERROR = 3
} log_level_t;

// Initialize logging (call once per program)
void log_init(const char *module_name, log_level_t level);

// Log functions (variadic, printf-style)
void log_debug(const char *fmt, ...);
void log_info(const char *fmt, ...);
void log_warn(const char *fmt, ...);
void log_error(const char *fmt, ...);

// Set/get current level
void log_set_level(log_level_t level);
log_level_t log_get_level(void);

// Close logging (flush, close file if applicable)
void log_close(void);
```

### Features

- **Timestamp prefix:** `[2024-02-10 16:45:30.123]`
- **Level indicator:** `[DEBUG]`, `[INFO]`, `[WARN]`, `[ERROR]`
- **Module prefix:** `[PREPROCESS]`, `[INIT]`, `[SWEEP]`, `[MIP]`
- **Thread-safe:** Uses mutex if compiled with `-DTHREAD_SAFE`
- **Log to stdout, file, or both**

### Usage Examples

```c
#include "common/logging.h"

int main(void) {
    log_init("INIT", LOG_INFO);
    
    log_info("Starting initialization with strategy: %s", strategy);
    log_debug("Distance matrix size: %d x %d", n, n);
    
    if (error_condition) {
        log_error("Failed to load database: %s", error_msg);
        log_close();
        return 1;
    }
    
    log_info("Initialization complete. Runtime: %.2f ms", elapsed);
    log_close();
    return 0;
}
```

### Output Example

```
[2024-02-10 15:32:45.123] [INIT]      [INFO]  Starting initialization with strategy: nn
[2024-02-10 15:32:45.124] [INIT]      [DEBUG] Distance matrix size: 150 x 150
[2024-02-10 15:32:45.156] [INIT]      [INFO]  Nearest Neighbor: starting from station 5
[2024-02-10 15:32:45.234] [INIT]      [DEBUG] Tour: [5, 12, 3, 45, ...]
[2024-02-10 15:32:45.235] [INIT]      [INFO]  Initialization complete. Runtime: 112.00 ms
```

Database Helpers
----------------

### Interface

```c
// db_helpers.h

typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;

// Connection management
sqlite3 *db_open(const char *path);
void db_close(sqlite3 *db);

// Schema creation (call once per pipeline)
int db_create_schema(sqlite3 *db);

// Query execution
int db_exec(sqlite3 *db, const char *sql, ...);
sqlite3_stmt *db_query(sqlite3 *db, const char *sql, ...);
void db_finalize(sqlite3_stmt *stmt);

// Result extraction
int db_get_int(sqlite3_stmt *stmt, int column);
double db_get_double(sqlite3_stmt *stmt, int column);
char *db_get_text(sqlite3_stmt *stmt, int column);

// Insert helpers
int db_insert_location(sqlite3 *db, char type, int lat, int lon, ...);
int db_insert_solution(sqlite3 *db, const char *name, const int *tour, int tour_len, double distance);
```

### Prepared Statements (for efficiency)

```c
// Example: Insert many locations
sqlite3_stmt *stmt = db_prepare(db, 
    "INSERT INTO locations (type, lat, lon, lat_decimal, lon_decimal) "
    "VALUES (?, ?, ?, ?, ?)");

for (int i = 0; i < num_locs; i++) {
    db_bind_text(stmt, 1, type_str);
    db_bind_int(stmt, 2, lat[i]);
    db_bind_int(stmt, 3, lon[i]);
    db_bind_double(stmt, 4, lat_decimal[i]);
    db_bind_double(stmt, 5, lon_decimal[i]);
    db_step(stmt);
    db_reset(stmt);
}
db_finalize(stmt);
db_commit(db);
```

### Transaction Support

```c
db_begin(db);
// ... perform multiple INSERTs/UPDATEs ...
if (error) {
    db_rollback(db);
} else {
    db_commit(db);
}
```

Output Serialization
--------------------

### JSON Serialization

```c
// output.h

typedef struct {
    int *tour;
    int tour_length;
    double total_distance;
    double runtime_seconds;
    const char *strategy;
    const char *timestamp;
    // ... other fields ...
} solution_t;

// Serialize solution to JSON string (caller must free)
char *serialize_solution_json(const solution_t *sol);

// Write JSON to file
int write_solution_json(const char *filepath, const solution_t *sol);

// Append log entry to JSON logs array
int append_log_to_json(char **json_str, const char *log_entry);
```

### Usage Example

```c
solution_t sol = {
    .tour = tour_array,
    .tour_length = 150,
    .total_distance = 12345.67,
    .runtime_seconds = 0.042,
    .strategy = "nn",
    .timestamp = "2024-02-10T15:32:45Z"
};

char *json_str = serialize_solution_json(&sol);
write_solution_json("output.json", &sol);
free(json_str);
```

### CSV Serialization

```c
// Flatten solution to CSV row
char *serialize_solution_csv(const solution_t *sol);

// Example output:
// 2024-02-10T15:32:45Z,nn,150,12345.67,0.042,"[5,12,3,45,...]"
```

### Trajectory Serialization

```c
typedef struct {
    int l2seg;
    int iteration;
    double total_distance;
    double improvement;
    double elapsed_seconds;
} trajectory_point_t;

// Serialize array of trajectory points to JSON
char *serialize_trajectory_json(
    const trajectory_point_t *traj,
    int traj_length,
    const solution_t *best_sol
);

// Append trajectory point to CSV file
int append_trajectory_csv(
    const char *filepath,
    const trajectory_point_t *point
);
```

Math Utilities
--------------

### Coordinate Conversion

```c
// math.h

// Degmin (integer) → Decimal degrees
double degmin_to_decimal(int degmin_value);

// Decimal degrees → Radians
double decimal_to_radians(double degrees);

// Radians → Decimal degrees
double radians_to_decimal(double radians);
```

### Distance Calculation

```c
// Haversine distance (great-circle distance on sphere)
// lat/lon in decimal degrees
double haversine_distance(double lat1, double lon1, double lat2, double lon2);

// Euclidean distance (2D plane)
double euclidean_distance(double x1, double y1, double x2, double y2);

// Distance between two points given as (lat, lon) pairs
double point_distance(const point_t *p1, const point_t *p2);
```

### Examples

```c
#include "common/math.h"

// Convert degmin coordinates
int degmin_lat = 632700;  // 63°27'00"
double lat_decimal = degmin_to_decimal(degmin_lat);  // ~63.45°

// Compute great-circle distance
double dist = haversine_distance(63.45, -20.5, 64.0, -21.0);
// Returns distance in same units as Earth radius (default: km)
```

Makefile
--------

```makefile
# common/Makefile

COMMON_OBJS = logging.o db_helpers.o output.o math.o

common.a: $(COMMON_OBJS)
	ar rcs common.a $^

logging.o: logging.c logging.h
	$(CC) $(CFLAGS) -c logging.c -o logging.o

db_helpers.o: db_helpers.c db_helpers.h
	$(CC) $(CFLAGS) -c db_helpers.c -o db_helpers.o

output.o: output.c output.h
	$(CC) $(CFLAGS) -c output.c -o output.o

math.o: math.c math.h
	$(CC) $(CFLAGS) -c math.c -o math.o

clean:
	rm -f $(COMMON_OBJS) common.a

.PHONY: clean
```

### Master Build Integration

From `../Makefile`:

```makefile
# Build common utilities first
common/common.a:
	make -C common all

# Link into init, sweep, etc.
bin/init: init/init.c common/common.a
	$(CC) $(CFLAGS) -I./include -I./common init/init.c \
	    common/common.a -o bin/init $(LDFLAGS)
```

Testing
-------

```bash
make -C common test
```

Runs:
- Unit tests for coordinate conversion accuracy
- Distance calculation verification (known reference points)
- JSON serialization round-trip tests
- SQLite schema validation

Example: Haversine Distance Test

```c
// Reykjavik to Akureyri (Iceland)
double dist = haversine_distance(64.1466, -21.9426, 65.6829, -18.0894);
assert(fabs(dist - 299.5) < 1.0);  // Expected ~299.5 km
```

Performance Notes
-----------------

1. **Logging:** Buffered I/O, minimal overhead when level is below threshold
2. **Database:** Prepared statements and transactions recommended for bulk inserts (10–100x speedup vs. individual queries)
3. **Distance calculation:** Haversine involves transcendental functions (sin, cos, atan2); optimize with caching if calling millions of times
4. **JSON serialization:** String concatenation may be slow for large tours; consider streaming or pre-allocated buffers

Thread Safety
-------------

All functions are **not thread-safe by default**. To enable:

```bash
make -C common THREAD_SAFE=1
```

This compiles with mutex protection around logging and database access.

Troubleshooting
---------------

- **SQLite "database is locked":** Ensure all transactions are closed. Use `db_close()` properly.
- **Coordinate conversion incorrect:** Verify degmin format assumption (see preprocess/README.md).
- **Distance calculation mismatch:** Ensure lat/lon are in decimal degrees, not radians or degmin.
- **JSON serialization crash:** Check that solution struct fields are initialized (non-NULL pointers, valid values).

References
----------

- SQLite C API: https://www.sqlite.org/appfn.html
- JSON format: RFC 7158
- Haversine formula: https://en.wikipedia.org/wiki/Haversine_formula

