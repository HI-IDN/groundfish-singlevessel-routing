# GSP Solver Configuration

## Overview

The GSP solver uses a single configuration file: **`gsp_solver.yaml`**

This minimal YAML file defines solver parameters, while most data is loaded from the database at runtime:

- **Boat parameters** (capacity, home port) → loaded from `boats` table
- **Instance parameters** (stations, catch, segments) → computed from `stations` table
- **Strategy settings** → YAML configuration
- **Sweep parameters** → YAML configuration
- **Gurobi settings** → YAML configuration

## Configuration File: `gsp_solver.yaml`

### Boat Configuration

```yaml
boat:
  id: 2                          # boat_id in database
  name: "Árni Friðriksson"       # Display name
  # capacity and home_port loaded from database
```

**Why minimal?** Capacity and home port are stored in the database and loaded at runtime. This ensures consistency and avoids duplication.

### Phase 0: Initialization Configuration

```yaml
init:
  strategies:
    - opt      # Optimal NP-MIP (expensive, 2 hours)
    - nn       # Nearest Neighbor (fast)
    - ge       # Greedy Edge (fast)
    - ci       # Cheapest Insertion (moderate)
  
  opt:
    time_limit_seconds: 7200   # 2 hours for full dataset (580 stations)
```

**Why 7200 seconds (2 hours)?**
- The OPT run should take about an hour
- Better to have generous limit than risk timeout

### Phase 1: Matheuristic Sweep Configuration

```yaml
sweep:
  l2seg_values: [60, 120, 180, 240, 300, 360, 420, 480]  # L2SEG: segment length in stations
  
  l1seg: 0                        # L1SEG: time limit per segment (0 = no limit for pure TSP)
  
  max_iterations: 100             # Max iterations per sweep
  max_stall_iterations: 20        # Stop if no improvement for N iterations
  log_interval: 1                 # Log every iteration
```

**L2SEG (Segment Length)**:
- Tests 8 different segment sizes: 60, 120, 180, 240, 300, 360, 420, 480 stations
- Larger L2SEG = larger segments = slower solve but more reordering potential
- Smaller L2SEG = faster solves but limited improvements

**L1SEG (Time Limit per Segment)**:
- 0 = no time limit
- Per-segment TSP has no capacity constraints, so it's pure TSP
- Should solve quickly without artificial limits
- Can set to positive value if needed (e.g., 120 seconds)

### Gurobi Configuration

```yaml
gurobi:
  env_log_file: null           # null = no log file
  threads: 0                   # 0 = auto-detect CPU count
  mip_focus: 0                 # 0 = balanced search
  seed: -1                     # -1 = random seed (reproducible per run)
```

### Database Configuration

```yaml
database:
  path: "sol/experiments.db"
  auto_create_schema: true     # Auto-create tables if missing
```

### Output Configuration

```yaml
output:
  verbose: true                # Print progress to stdout
  json_format: true            # Save results as JSON
```

## Data Loading at Runtime

When the solver starts, it loads:

```sql
-- Boat info
SELECT capacity FROM boats WHERE id = 2;
SELECT l.lat, l.lon FROM boats b 
  JOIN locations l ON b.location_id = l.id 
  WHERE b.id = 2;

-- Instance parameters
SELECT COUNT(*) FROM stations;
SELECT SUM(amount) FROM stations;
```

This means:
- ✅ No duplication between YAML and database
- ✅ Boat/instance data always in sync
- ✅ Easy to switch boats (just change `boat.id: 2` to `boat.id: 1`)
- ✅ YAML stays clean and minimal

## Performance Expectations

### Phase 0 (INIT)
- OPT: 7200 seconds (2 hours) max → actual ~425-600 seconds
- NN: <1 second
- GE: 5-10 seconds
- CI: 30-60 seconds
- **Total: ~2-3 hours** (OPT dominates)

### Phase 1 (MH Sweep)
- 8 L2SEG values × 100 iterations each
- Per-segment TSP fast (no capacity, no time limit)
- **Expected total: ~24 hours** for 8 sweeps

### Complete Pipeline
- Phase 0: ~2-3 hours
- Phase 1: ~24 hours
- **Total: ~26-27 hours**

## Monitoring Configuration

Check which values are being used:

```bash
# Show all YAML settings
cat config/gsp_solver.yaml

# Show database values at runtime
sqlite3 dat/gsp_data.db "SELECT capacity FROM boats WHERE id=2;"
sqlite3 dat/gsp_data.db "SELECT COUNT(*) FROM stations;"

# Show Gurobi environment
gurobi_cl --license
```

## References

- Gurobi Parameters: https://www.gurobi.com/documentation/




