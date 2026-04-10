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

### Global Configuration

```yaml
global_time_limit_seconds: 172800  # 48 hours
```

`global_time_limit_seconds` is a project-level wall-clock cap for long-running workflows. Sweep
returns when the cap is reached even if `sweep.max_iterations` has not been reached.

### Boat Configuration

```yaml
boat:
  id: 2                          # boat_id in database
  # name of boat, its capacity and home_port is loaded from database
```

**Why minimal?** Capacity and home port are stored in the database and loaded at runtime. This ensures consistency and avoids duplication.

### Phase 0: Initialization Configuration

```yaml
init:
  strategies:
    - noport   # No-port MIP station ordering
    - fixedport # Fixed port-order MIP
    - nn       # Nearest Neighbor (fast)
    - ge       # Greedy Edge (fast)
    - ci       # Cheapest Insertion (moderate)

  noport: {}
  fixedport: {}
  nn: {}
  ge: {}
  ci: {}
```

### Phase 1: Matheuristic Sweep Configuration

```yaml
sweep:
  max_iterations: 100             # Max iterations per sweep
```

### Gurobi Configuration

```yaml
gurobi:
  l0seg: 0                    # No-port MIP time limit (0 = no limit)
  l1seg: 0                    # Per-segment TSP time limit (0 = no limit)
  l2seg: 0                    # Two-segment sweep MIP time limit (0 = no limit)
  lXseg: 86400                # Fixed-port MIP time limit after incumbent exists
  env_log_file: null           # null = no log file
  threads: 0                   # 0 = auto-detect CPU count
  mip_focus: 0                 # 0 = balanced search
  seed: -1                     # -1 = random seed (reproducible per run)
```

**Segment Time Limits**:
- `l0seg` controls the no-port MIP solve.
- `l1seg` controls single-segment TSP solves.
- `l2seg` controls two-segment sweep MIP solves.
- `lXseg` controls the full fixed-port-order MIP solve after an incumbent exists.

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

## References

- Gurobi Parameters: https://www.gurobi.com/documentation/




