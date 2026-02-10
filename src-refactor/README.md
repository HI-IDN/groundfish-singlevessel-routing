Groundfish Survey Routing - Refactored Pipeline
================================================

Overview
--------

This refactored codebase implements a modular preprocessing, initialization, and sweep matheuristic pipeline for the Groundfish Survey Routing problem. The architecture separates concerns into:

1. **Preprocessing** (`preprocess/`) — Parse raw `.dat` files into SQLite database
2. **Initialization** (`init/`) — Generate initial solutions using heuristics (NN, CI, GE) or exact methods (opt/no-port MIP)
3. **MIP Models** (`mip/`) — Centralized Gurobi-based optimization models (capacity-aware, no-port, end-paired TSP)
4. **Sweep** (`sweep/`) — Matheuristic sweep over L2seg parameter ranges using initialized solutions
5. **Common** (`common/`) — Shared utilities (logging, I/O, SQLite helpers)

Build Prerequisites
-------------------

Same as parent project:
- C toolchain (GCC/Clang with `-std=c11`)
- SQLite development headers (`libsqlite3-dev`)
- Gurobi 13.0+ with headers, libs, and valid license
- Set `GUROBI_HOME` environment variable (see parent README.md)

Quick Build
-----------

From this directory:

```bash
make clean
make all
```

This builds:
- `bin/preprocess` — converts `.dat` → SQLite
- `bin/init` — generates initial solution with `--strategy {nn|ci|ge|opt}`
- `bin/sweep` — runs matheuristic sweep over L2seg values

Directory Structure
-------------------

```
src-refactor/
  ├── README.md                 (this file)
  ├── Makefile                  (master build rules)
  ├── mip/
  │   ├── README.md             (MIP model documentation)
  │   ├── Makefile              (compile MIP object files)
  │   ├── include/
  │   │   ├── mip_capacity_aware.h
  │   │   ├── mip_noport.h
  │   │   └── mip_endpaired_tsp.h
  │   ├── capacity_aware.c      (capacity-aware MIP solver)
  │   ├── noport.c              (no-port MIP solver)
  │   └── endpaired_tsp.c       (end-paired TSP MIP solver)
  ├── init/
  │   ├── README.md             (initialization strategies documentation)
  │   ├── Makefile              (compile init binary)
  │   ├── init.c                (CLI entry point, strategy dispatcher)
  │   ├── strategy_nn.c         (Nearest Neighbor heuristic)
  │   ├── strategy_ci.c         (Cheapest Insertion heuristic)
  │   ├── strategy_ge.c         (Greedy Edge heuristic)
  │   └── strategy_opt.c        (calls no-port MIP)
  ├── preprocess/
  │   ├── README.md             (preprocessing documentation)
  │   ├── Makefile              (compile preprocess binary)
  │   └── parse_dat_to_sqlite.c (DAT parser → SQLite)
  ├── sweep/
  │   ├── README.md             (sweep matheuristic documentation)
  │   ├── Makefile              (compile sweep binary)
  │   └── sweep.c               (main sweep loop, calls MIP models)
  ├── common/
  │   ├── README.md             (common utilities documentation)
  │   ├── Makefile              (compile common object files)
  │   ├── logging.c             (debug/info/warn logging macros)
  │   └── db_helpers.c          (SQLite utility functions)
  └── include/
      ├── db_schema.h           (SQLite table schemas)
      ├── data_types.h          (shared structs: init_result_t, solution_t, etc.)
      ├── logging.h             (logging interface)
      └── db_helpers.h          (database interface)
```

Pipeline Usage
--------------

### Stage 1: Preprocess Raw Data

```bash
./bin/preprocess \
  --input dat/data2023spring.dat \
  --db dat/parsed_data.sqlite \
  --log-level info
```

Output: SQLite database with `locations`, `ports`, `boats`, `stations`, `waypoints` tables.

### Stage 2: Initialize Solution

```bash
./bin/init \
  --db dat/parsed_data.sqlite \
  --strategy nn \
  --output json \
  --log-level debug \
  > init_nn.json
```

Strategies: `nn`, `ci`, `ge`, `opt`
Output: JSON with tour, total_distance, runtime_ms, logs embedded.

### Stage 3: Run Sweep Matheuristic

```bash
./bin/sweep \
  --db dat/parsed_data.sqlite \
  --init-solution init_nn.json \
  --l2seg-seconds 60 \
  --log-level info \
  > sweep_output.json
```

Output: JSON with sweep trajectory, final solution, logs.

Key Design Decisions
--------------------

1. **SQLite as central data format** — All stages read/write to a single SQLite database for 
   consistency and easy downstream analysis.

2. **MIP models in `mip/` subfolder** — Gurobi solvers (capacity-aware, no-port, end-paired TSP) 
   are built as object files and linked into both init and sweep binaries as library functions. 
   This avoids code duplication and eases maintenance.

3. **Init strategies report runtime** — Each strategy (NN, CI, GE, opt) measures and reports 
   wall-clock runtime and final total distance. Opt additionally reports the MIP solver status 
   and gap (if applicable).

4. **JSON output format** — Solutions and sweep results are serialized to JSON for easy 
   ingestion into downstream Python/R analysis scripts.

5. **Comprehensive logging** — All binaries support `--log-level {debug|info|warn|error}` with 
   timestamps and module prefixes (e.g., `[PREPROCESS] ...`).

Environment Variables
---------------------

- `GUROBI_HOME` — Path to Gurobi installation (required for MIP models)
- `GSP_LOG_LEVEL` — Default log level if not specified via CLI (default: `info`)
- `GSP_DATA_DIR` — Default data directory (default: `../../dat`)

Documentation
--------------

- See `mip/README.md` for MIP model specifications and solver parameters.
- See `init/README.md` for initialization strategy details and algorithm pseudocode.
- See `preprocess/README.md` for DAT parser documentation.
- See `sweep/README.md` for sweep matheuristic workflow.
- See `common/README.md` for logging and database helper APIs.

License & Attribution
---------------------

This codebase builds on the implementation described in the paper "Groundfish Survey Routing: A 
Scalable Matheuristic" that includes pseudo code for algorithms and problem formulation.

