Repository overview
===================

This folder is the root for the build and test workflow described below. It contains the
survey-routing matheuristic codebase and a small, pure-C toolchain for preprocessing,
initialization, and sweep evaluation.

This README documents the environment checks you should run first, how to build the minimal
smoke-tests (SQLite + Gurobi), and how to proceed once the environment is confirmed. These checks
ensure the system has the required C development libraries and solver available before you perform
larger refactors.

Layout
------

- `src/` - pure-C implementation and Makefile (build targets and tests live here).
- `src-old/` - previous source tree (kept as reference).
- `dat/` - raw .dat files (e.g. `singleboatdata2023spring.dat`).
- `sol/` - solutions and outputs produced by runs.
- `bin/` - legacy binaries (kept for compatibility).
- `tools/` - small helper programs and environment smoke-tests.
- `config/` - YAML experiment configuration files.

Prerequisites
-------------
You need a C toolchain (GCC/Clang), the SQLite development headers and library, and Gurobi (headers,
libs and a valid license). These instructions assume Linux/macOS or Windows under MSYS2/WSL.

1) C toolchain

- Linux (Debian/Ubuntu): `sudo apt install build-essential`
- macOS (Homebrew): `brew install gcc`
- Windows (MSYS2/MinGW): use the MSYS2 package manager to install `mingw-w64-x86_64-gcc`.

2) SQLite (development headers)

- Linux (Debian/Ubuntu): `sudo apt install libsqlite3-dev`
- macOS (Homebrew): `brew install sqlite`
- Windows (MSYS2): `pacman -S mingw-w64-x86_64-sqlite3`

3) Gurobi

- Download and install Gurobi for your platform from https://www.gurobi.com/downloads/.
- Ensure you have a functioning license (for example, a `gurobi.lic` file or `GRB_LICENSE_FILE`
  pointing to a license).
- Set `GUROBI_HOME` to the installation directory. Example you provided in MSYS:
  ```bash
  export GUROBI_HOME="/c/gurobi1301/win64"
  export PATH="$GUROBI_HOME/bin:$PATH"
  export LD_LIBRARY_PATH="$GUROBI_HOME/lib:${LD_LIBRARY_PATH:-}"
  ```
- Ensure the compiler can find headers at `$GUROBI_HOME/include` and the linker at
  `$GUROBI_HOME/lib`.

Smoke-tests (verify environment)
--------------------------------
Two tiny C programs and a shell runner validate SQLite and Gurobi are available and linkable from
your C toolchain.

Run the tests from this directory (the project root):

```bash
make -C tools test-env
```

Expected output (both tests must PASS):

- `SQLITE_TEST: PASS` — SQLite headers and library are usable.
- `GUROBI_TEST: PASS - status=<status> elapsed=<s> s` — Gurobi headers and basic env allocation
  succeed.

If those tests pass, you can proceed to building and running the full pipeline.

Troubleshooting
---------------

- If the sqlite test fails: ensure `sqlite3.h` is installed and the `-lsqlite3` library is
  available. On Debian/Ubuntu install `libsqlite3-dev`.
- If the Gurobi test fails: ensure `GUROBI_HOME` is set and points to your Gurobi installation (
  headers under `$GUROBI_HOME/include` and libs under `$GUROBI_HOME/lib`). Also verify your Gurobi
  license is valid.

Building the GSP Solver
-----------------------

Once environment tests pass, build the solver:

```bash
make -C src build
```

The compiled binaries will be placed under `build/`.

Groundfish Survey Routing Solver: Complete Workflow
====================================================

The GSP solver optimizes groundfish survey routes for Icelandic research vessels using a two-phase
approach:

- **Phase 0 (INIT)**: Generate 4 initialization strategies (OPT, NN, GE, CI). Run once, solutions
  cached in database.
- **Phase 1 (MH)**: Improve from cached init solutions using matheuristic sweep. Reusable for
  different L2SEG parameters.

### Problem Instance

**Vessel**: Árni Friðriksson (boat_id=2)

- **Capacity**: 45 tonnes maximum cargo
- **Home Port**: Hafnarfjörður
- **Survey Stations**: 580 total
- **Total Expected Catch**: ~528 tonnes
- **Minimum Segments**: N_min = 12 (ceil(528/45))
- **Target Segments**: N = 13

### Phase 0: Generate Initialization Solutions (Run Once)

All 4 initialization strategies must be run once. Results are cached in the database and reused by
Phase 1.

#### Single Strategy

```bash
make -C src noport-opt
make -C src init_opt
make -C src init_nn
make -C src init_ge
make -C src init_ci
```

#### Batch: All 4 Strategies

```bash
bash scripts/run_phase0_init.sh
```

**Expected Output (OPT strategy)**:

```
============================================================
GSP Solver - Phase 0: Initialization
============================================================
Boat: Árni Friðriksson (ID=2)
Capacity: 45 tonnes
Home Port: Hafnarfjörður
Database: dat/gsp.db

Strategy: OPT (Optimal via NP-MIP)
Solver: Gurobi 11.0
CPUs Available: 8 cores
Threads: 4 (Gurobi)
Time Limit: 600.0 seconds

[LOADING] Stations: 580 | Ports: 5 | Waypoints: 8
[LOADING] Distance matrix: 593x593 (2.82 MB)
[BUILDING] MIP model...
[SOLVING] NP-MIP (no capacity constraints, fixed start/end)
[PROGRESS] Status=OPTIMAL Gap=0.00% Nodes=45000 Runtime=425.3s

Results:
  Total Distance: 8742.15 nm
  Total Load: 528 tonnes (12 segments minimum)
  Stations: 580
  Segments: 13 (optimal segmentation)
  Runtime: 425.3 seconds (7.1 minutes)
  Solver Gap: 0.00%

[DATABASE] Storing solution to init_runs table...
[SUCCESS] Initialization complete! (init_run_id=1)
============================================================
```

#### Verify Phase 0 Completion

```bash
sqlite3 dat/gsp.db \
  "SELECT strategy, total_distance, num_stations, num_segments, runtime_seconds 
   FROM init_runs 
   WHERE boat_id = 2 
   ORDER BY total_distance;"
```

Sample output:

```
ci|8654.32|580|13|87.4
ge|8698.15|580|13|15.3
nn|8721.45|580|13|8.2
opt|8742.15|580|13|425.3
```

### Phase 1: Matheuristic Sweep (Reusable)

The MH sweep improves a cached Phase 0 solution through iterative segment refinement using
capacity-aware MIP solves.

#### Single Sweep

```bash
./build/gsp_gurobi --mode sweep \
  --strategy opt \
  --database dat/gsp.db \
  --config config/gsp_solver.yaml \
  --input sol/opt/init.json \
  --output sol/opt/sweep.json \
  --time-limit 120
```

Parameters:

- `--init-strategy opt` - Use OPT init (looks up boat_id + strategy in database)
- `--l2seg 120` - Segment size (120 stations per segment)
- `--stride 60` - Overlap stride (50% overlap = L2SEG/2)
- `--mip-time-limit 120` - Time limit per MIP solve (seconds)
- `--max-iterations 100` - Maximum iterations to run

#### Batch: All L2SEG Values

```bash
bash scripts/run_phase1_sweep.sh
```

Runs sweeps with L2SEG = 60, 120, 180, 240, 300 using cached OPT initialization.

**Expected Output (Sweep Progress)**:

```
============================================================
GSP Solver - Phase 1: Matheuristic Sweep
============================================================
Init Strategy: OPT (init_run_id=1)
Initial Distance: 8742.15 nm | Segments: 13

Configuration:
  L2SEG: 120 stations/segment
  Stride: 60 stations
  Overlap: 50%
  MIP Time Limit: 120.0 s per segment solve
  Max Iterations: 100
  CPUs Available: 8 cores
  Gurobi Threads: 4

[INIT] Loading cached init solution (OPT, boat_id=2)...
[INIT] Distance: 8742.15 nm | Segments: 13 | Load: 528 tonnes

[STARTING] Matheuristic iteration sweep...

Iteration   1 | Segments:  13 | Distance: 8742.15 | Best: 8742.15 | Δ:     0.00 | Time:  0.1s
Iteration   2 | Segments:  13 | Distance: 8698.45 | Best: 8698.45 | Δ:    43.70 | Time: 62.3s
Iteration   3 | Segments:  13 | Distance: 8698.45 | Best: 8698.45 | Δ:     0.00 | Time: 58.1s
Iteration   4 | Segments:  13 | Distance: 8651.22 | Best: 8651.22 | Δ:    47.23 | Time: 71.2s
...
Iteration 100 | Segments:  13 | Distance: 8312.45 | Best: 8312.45 | Δ:     0.00 | Time: 45.7s

[SUMMARY] Matheuristic Sweep Complete
Initial Distance:      8742.15 nm
Final Distance:        8312.45 nm
Total Improvement:     429.70 nm (4.91%)
Iterations:            100 (67 with improvement)
Stalled Iterations:    33
Final Segments:        13
Total Runtime:         6847.2 seconds (114.1 minutes)

[DATABASE] Storing results to mh_runs, mh_iterations tables...
[SUCCESS] Sweep complete! (mh_run_id=1)
============================================================
```

### Configuration

The solver reads configuration from `config/gsp_solver.yaml`:

```yaml
boat:
  id: 2
  name: "Árni Friðriksson"
  # capacity, home_port loaded from database

init:
  strategies: [ opt, nn, ge, ci ]
  opt:
    time_limit_seconds: 7200          # 2 hours for OPT initialization

sweep:
  l2seg_values: [ 60, 120, 180, 240, 300, 360, 420, 480 ]  # L2SEG: segment length
  l1seg: 0                            # L1SEG: time limit per segment (0 = no limit)

  max_iterations: 100
  max_stall_iterations: 20
```

### Querying Results

**Compare all INIT strategies:**

```bash
sqlite3 dat/gsp.db \
  "SELECT 
     strategy,
     total_distance,
     num_segments,
     runtime_seconds
   FROM init_runs
   WHERE boat_id = 2
   ORDER BY total_distance ASC;"
```

**Best MH result for each L2SEG:**

```bash
sqlite3 dat/gsp.db \
  "SELECT 
     init.strategy,
     mh.l2seg,
     mh.final_distance,
     ROUND(100.0*(init.total_distance - mh.final_distance) / init.total_distance, 2) AS improvement_pct,
     mh.iterations_completed,
     ROUND(mh.total_runtime_seconds/60.0, 1) AS runtime_min
   FROM mh_runs mh
   JOIN init_runs init ON mh.init_run_id = init.id
   WHERE init.boat_id = 2 AND init.strategy = 'opt'
   ORDER BY mh.l2seg;"
```

**Track convergence:**

```bash
sqlite3 dat/gsp.db \
  "SELECT 
     iteration,
     total_distance,
     best_distance,
     num_changed,
     ROUND(elapsed_seconds/60.0, 2) AS elapsed_min
   FROM mh_iterations
   WHERE mh_run_id = 1
   ORDER BY iteration;"
```

### Shell Scripts

**`scripts/run_phase0_init.sh`** - Run all 4 initialization strategies:

```bash
bash scripts/run_phase0_init.sh
```

**`scripts/run_phase1_sweep.sh`** - Run MH sweeps (L2SEG = 60, 120, 180, 240, 300):

```bash
bash scripts/run_phase1_sweep.sh
```

**`scripts/batch_all.sh`** - Complete pipeline (Phase 0 → Phase 1):

```bash
bash scripts/batch_all.sh
```

### Performance Expectations

Typical runtimes on 8-core system (Árni Friðriksson, 580 stations, 45 tonne capacity):

**Phase 0 (INIT):**

- OPT: 400-500 seconds (~7-8 minutes)
- NN: <1 second
- GE: 5-10 seconds
- CI: 45-60 seconds
- **Total Phase 0**: ~10 minutes

**Phase 1 (MH, per L2SEG):**

- L2SEG=60: 90-120 minutes (100 iterations)
- L2SEG=120: 110-140 minutes (100 iterations)
- L2SEG=180: 120-150 minutes (100 iterations)
- L2SEG=240: 100-130 minutes (100 iterations)
- L2SEG=300: 80-110 minutes (100 iterations)
- **Total Phase 1 (5 L2SEG)**: ~10-12 hours

**Complete Pipeline**: ~10-13 hours total

### Database Tables

Solution tracking uses 8 tables in `dat/gsp.db`:

- `init_runs` - One entry per boat/strategy combination
- `init_tours` - Tour waypoints for each init solution
- `init_segments` - Segments created during initialization
- `mh_runs` - Configuration and final results for each sweep
- `mh_iterations` - Per-iteration progress tracking
- `mh_iteration_segments` - Per-segment stats for each iteration
- `mh_capacity_solves` - Gurobi MIP solver details
- `mh_improvements` - Solution snapshots on improvement
- `metadata` - Configuration and runtime parameters

### Troubleshooting

**Problem: "init_run_id not found"**

- Solution: Run Phase 0 first: `bash scripts/run_phase0_init.sh`

**Problem: "No OPT solution available"**

- Solution: OPT takes ~7 minutes. Use `--init-strategy nn` or `--init-strategy ge` for quick
  testing.

**Problem: Phase 1 taking very long**

- Solution: Reduce `--max-iterations` or decrease `--mip-time-limit` for faster (lower quality)
  results.

**Problem: "Gurobi license error"**

- Solution: Verify `GUROBI_HOME` is set and license is valid: `gurobi_cl --license`

### References

- Configuration: `config/gsp_solver.yaml`
- Boat info: `SELECT id, name, capacity FROM boats;`
- Station count: `SELECT boat_id, COUNT(*) FROM survey_2023 WHERE location_type=1 GROUP BY boat_id;`
- Total catch: `SELECT SUM(amount) FROM stations;`

