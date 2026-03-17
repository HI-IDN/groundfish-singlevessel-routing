# GSP Solver Scripts

Automated bash scripts for running the GSP solver pipeline.

## Quick Start

```bash
# Phase 0: Run all 4 initialization strategies (once, ~10 minutes)
bash scripts/run_phase0_init.sh

# Phase 1: Run MH sweep on cached init solutions (~10-12 hours)
bash scripts/run_phase1_sweep.sh

# Or: Complete pipeline at once (Phase 0 + Phase 1, ~10-13 hours)
bash scripts/batch_all.sh
```

## Scripts

### `run_phase0_init.sh` - Initialize (OPT, NN, GE, CI)

Generates 4 initialization strategies and caches them in the database.

**Run once.** Results are reused by Phase 1.

```bash
bash scripts/run_phase0_init.sh
```

**What it does**:
1. Verifies the solver has been built with `make -C src build`
2. Verifies database exists at `dat/gsp_data.db`
3. Runs 4 strategies in sequence:
   - `opt` - Optimal NP-MIP (~7-8 minutes, expensive but optimal)
   - `nn` - Nearest Neighbor (<1 second, fast heuristic)
   - `ge` - Greedy Edge (~5-10 seconds, fast construction)
   - `ci` - Cheapest Insertion (~1-2 minutes, moderate heuristic)
4. Displays results summary

**Output**:
```
============================================================
Phase 0: Initialization (OPT, NN, GE, CI)
============================================================

Boat: Árni Friðriksson (ID=2)
Database: dat/gsp_data.db
Config: config/gsp_solver.yaml

Running: opt
[HH:MM:SS] Starting OPT initialization (expensive, ~7-8 minutes)...
[OPT] ...solver progress...
✓ opt completed successfully

Running: nn
[HH:MM:SS] Starting NN initialization (fast)...
✓ nn completed successfully

... (ge, ci)

============================================================
Phase 0 Complete!
============================================================
Total Time: 10 minutes 45 seconds

Results Summary:
strategy  distance_nm  num_stations  num_segments  runtime_sec
ci        8654.32      580           13            87.4
ge        8698.15      580           13            15.3
nn        8721.45      580           13            8.2
opt       8742.15      580           13            425.3

Next step: Run Phase 1 matheuristic sweep
  bash scripts/run_phase1_sweep.sh
```

**Times**:
- Total Phase 0: ~10 minutes
  - OPT: 425.3 seconds (7.1 min)
  - NN: 8.2 seconds
  - GE: 15.3 seconds
  - CI: 87.4 seconds

### `run_phase1_sweep.sh` - Matheuristic Sweep

Improves cached init solutions through iterative segment refinement.

**Requires**: Phase 0 must be run first (to populate `init_runs`)

```bash
bash scripts/run_phase1_sweep.sh
```

**What it does**:
1. Looks up cached OPT initialization from database
2. Runs MH sweep on 5 L2SEG values:
   - L2SEG = 60 (~90-120 minutes)
   - L2SEG = 120 (~110-140 minutes)
   - L2SEG = 180 (~120-150 minutes)
   - L2SEG = 240 (~100-130 minutes)
   - L2SEG = 300 (~80-110 minutes)
3. Reports improvement from init solution for each L2SEG

**Output**:
```
============================================================
Phase 1: Matheuristic Sweep Improvement
============================================================

Using Init: opt (init_run_id=1)
Database: dat/gsp_data.db
L2SEG Values: 60 120 180 240 300

Initial Distance: 8742.15 nm (from opt)

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Sweep: L2SEG=60 STRIDE=30
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
[HH:MM:SS] Starting MH sweep with L2SEG=60...
[SOLVING] Iteration 1 | Distance: 8742.15 | Best: 8742.15 | Δ: 0.00 | Time: 0.1s
[SOLVING] Iteration 2 | Distance: 8698.45 | Best: 8698.45 | Δ: 43.70 | Time: 62.3s
... (100 iterations)
✓ L2SEG=60 completed
  Final Distance: 8342.22 nm
  Improvement: 399.93 nm (4.58%)

... (L2SEG=120, 180, 240, 300)

============================================================
Phase 1 Complete!
============================================================
Total Time: 542 minutes (9 hours 2 minutes)
Total Improvement: 429.70 nm

Results Summary (all L2SEG values):
l2seg  stride  best_distance_nm  final_num_segments  iterations_completed  improvement_pct  runtime_min
60     30      8342.22           13                  100                   4.58             105.3
120    60      8312.45           13                  100                   4.91             114.2
180    90      8298.15           13                  87                    5.08             127.5
240    120     8324.56           13                  65                    4.77             118.4
300    150     8351.23           13                  52                    4.48             95.7
```

**Times** (per L2SEG, 100 iterations each):
- L2SEG=60: ~105 minutes
- L2SEG=120: ~114 minutes
- L2SEG=180: ~128 minutes
- L2SEG=240: ~118 minutes
- L2SEG=300: ~96 minutes
- **Total Phase 1**: ~560 minutes (~9-10 hours)

### `batch_all.sh` - Complete Pipeline

Runs Phase 0 + Phase 1 sequentially with confirmation prompt.

**Requires**: Build exists from `make -C src build`

```bash
bash scripts/batch_all.sh
```

**What it does**:
1. Shows time estimate and asks for confirmation
2. Runs `run_phase0_init.sh`
3. If Phase 0 succeeds, runs `run_phase1_sweep.sh`
4. Reports final status

**Times**:
- Phase 0: ~10 minutes
- Phase 1: ~560 minutes (~9-10 hours)
- **Total**: ~10-13 hours

## Configuration

All scripts read configuration from:
- `config/gsp_solver.yaml` - Main settings
- `dat/gsp_data.db` - Metadata table for time limits and parameters

### Example: Change time limits

Edit `config/gsp_solver.yaml`:

```yaml
sweep:
  mip:
    time_limit_seconds: 60  # Reduce from 120 to 60 seconds per MIP
  max_iterations: 50        # Reduce from 100 to 50 iterations
```

Then re-run Phase 1 (Phase 0 cached results will be reused):

```bash
bash scripts/run_phase1_sweep.sh
```

## Monitoring Progress

Watch progress in real-time:

```bash
# Monitor database growth
watch -n 5 "sqlite3 dat/gsp_data.db 'SELECT COUNT(*) FROM mh_iterations;'"

# Check latest iteration progress
sqlite3 dat/gsp_data.db "SELECT * FROM mh_iterations ORDER BY id DESC LIMIT 10;"

# Monitor solver status
tail -f log/gurobi.log
```

## Troubleshooting

**Error: "Solver not found"**
```bash
make -C src build
```

**Error: "Database not found"**
```bash
# Run preprocessing to create database
# (See preprocessing section in root README)
```

**Error: "No OPT initialization found"**
```bash
# Run Phase 0 first
bash scripts/run_phase0_init.sh
```

**Kill long-running sweep**
```bash
# Gracefully stop current iteration
killall gsp_solver

# Or force kill
killall -9 gsp_solver

# Results from completed iterations are still saved in database
```

## Performance Tuning

### Fast mode (lower quality, ~2-3 hours for Phase 1)

Edit `config/gsp_solver.yaml`:
```yaml
sweep:
  l2seg_values: [120]           # Test single value
  max_iterations: 20             # Reduce iterations
  mip:
    time_limit_seconds: 30       # Reduce MIP time
```

Then run:
```bash
bash scripts/run_phase1_sweep.sh
```

### Slow mode (higher quality, ~15-20 hours for Phase 1)

Edit `config/gsp_solver.yaml`:
```yaml
sweep:
  max_iterations: 200            # More iterations
  mip:
    time_limit_seconds: 240      # More MIP time
  gurobi:
    mip_focus: 2                 # Focus on optimality
```

## Notes

- Phase 0 caches results permanently in database
- Each Phase 1 run is independent and creates new `mh_runs` entry
- Results are never lost (stored in database)
- Can safely interrupt and resume Phase 1 (use same L2SEG to retry)
- To completely restart, delete from `init_runs` and `mh_runs` tables

## References

- Main README: `../README.md`
- Configuration: `../config/gsp_solver.yaml`
- Database schema: `../src/include/solution_db.h`

