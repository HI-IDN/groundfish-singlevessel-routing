MIP Models for Groundfish Survey Routing
==========================================

Overview
--------

This folder contains all Gurobi-based integer linear programming (MIP) formulations for the Groundfish Survey Routing problem. Each model is implemented as a C library that can be called from `init/` (for initialization strategies) and `sweep/` (for the matheuristic loop).

Models
------

### 1. Capacity-Aware MIP (`capacity_aware.c`)

Full TSP with capacity constraints on vessel routes.

**Formulation:**
- Decision variables: Arc flow x_ij (binary), subtour elimination constraints
- Objective: Minimize total distance
- Constraints:
  - One incoming, one outgoing arc per node (degree-2)
  - Subtour elimination (lazy callback)
  - Capacity limits per vessel
  - (Optional) port return constraints

**Function signature:**
```c
int solve_mip_capacity_aware(
    const mip_instance_t *instance,
    const mip_params_t *params,
    mip_solution_t *solution
);
```

**Parameters:**
- `time_limit_seconds` — MIP solver time limit
- `thread_count` — Gurobi thread limit (0 = auto)
- `verbose` — Log Gurobi solver output

**Returns:**
- `MIP_STATUS_OPTIMAL` — Optimal solution found
- `MIP_STATUS_TIME_LIMIT` — Time limit reached (heuristic solution available)
- `MIP_STATUS_INFEASIBLE` — No feasible solution exists

### 2. No-Port MIP (`noport.c`)

Anchored no-port paired-end TSP used as an OPT preprocessing step.

**Formulation:**
- Solves the directed paired-end TSP over all stations
- Uses one fixed anchor port chosen by the entry code
- Produces the no-port ordering written to `sol/opt/noport.json`
- That ordering is later consumed by `init_opt`, which inserts ports for capacity feasibility

**Function signature:**
```c
int solve_mip_noport(
    const mip_noport_instance_t *instance,
    const mip_noport_params_t *params,
    mip_noport_solution_t *solution
);
```

The standalone preprocessing executable is `noport_opt.c`, which:
- reads YAML
- loads boat/ports/stations/distances
- chooses the anchor port nearest to the boat start location
- calls `solve_mip_noport(...)`
- writes `sol/opt/noport.json`

### 3. End-Paired TSP (`endpaired_tsp.c`)

TSP with start and end locations fixed (e.g., same port).

**Formulation:**
- Same as No-Port but with mandatory start/end node pair
- Useful for validating solutions or enforcing depot returns

**Function signature:**
```c
int solve_mip_endpaired_tsp(
    const mip_instance_t *instance,
    const mip_params_t *params,
    int start_node,
    int end_node,
    mip_solution_t *solution
);
```

**Parameters:**
- `start_node`, `end_node` — Fixed depot nodes (same or different)
- Other params as above

**Returns:** Same as above.

Build
-----

```bash
make -C mip all
```

This compiles each model to an object file (`.o`) and archives them into `libmip.a` for linking.

Dependencies
------------

- Gurobi 13.0+ C API (`gurobi_c.h`)
- `include/mip_*.h` headers (define interfaces)
- `../include/data_types.h` (shared structs)

Common Data Structures
----------------------

All models share the following interfaces (defined in `include/mip_*.h`):

### `mip_instance_t`

```c
typedef struct {
    int num_nodes;
    int num_edges;
    double *dist_matrix;        /* [num_nodes][num_nodes] */
    int *feasible_matrix;       /* [num_nodes][num_nodes] binary */
    int *node_type;             /* array of node types (PORT, STAT, WAYP, etc.) */
    double *demand;             /* [num_nodes] capacity demand (if applicable) */
    double vessel_capacity;     /* total capacity of vessel */
} mip_instance_t;
```

### `mip_params_t`

```c
typedef struct {
    double time_limit_seconds;
    int thread_count;
    int verbose;
    double mip_gap;             /* optimality gap tolerance */
    int heuristic_only;         /* skip exact branch-and-bound */
} mip_params_t;
```

### `mip_solution_t`

```c
typedef struct {
    int *tour;                  /* [num_nodes] node order in tour */
    int tour_length;
    double total_distance;
    double obj_value;
    int status;                 /* MIP_STATUS_* constant */
    double gap;                 /* optimality gap (if known) */
    double runtime_seconds;
    int solver_iterations;
} mip_solution_t;
```

Solver Parameters
-----------------

Gurobi parameters that each model respects:

- `TimeLimit` — Wall-clock time limit (seconds)
- `Threads` — Thread count for parallel branch-and-bound
- `MIPGap` — Relative optimality gap for early termination
- `OutputFlag` — 0 = silent, 1 = verbose
- `LogToConsole` — Write solver log to stdout

Example from `sweep.c`:

```c
GRBsetdblparam(GRBgetenv(model), "TimeLimit", 120.0);
GRBsetintparam(GRBgetenv(model), "Threads", 4);
GRBsetdblparam(GRBgetenv(model), "MIPGap", 0.01);
```

Lazy Subtour Elimination
------------------------

All three models use a **lazy callback** to add subtour elimination constraints dynamically during branch-and-bound (more efficient than adding all constraints upfront for large instances).

The callback:
1. Queries current LP solution
2. Detects cycles not including all nodes
3. Adds SEC constraints for isolated subtours
4. Repeats until LP solution is a single Hamiltonian cycle

Testing
-------

Each model has a lightweight smoke test:

```bash
make -C mip test
```

This runs minimal correctness checks (e.g., 5-node toy instance) to ensure Gurobi integration is working.

Performance Notes
-----------------

1. **Memory:** Distance matrix is O(n²). For n=1000, expect ~8MB. For larger instances, consider edge-indexed sparse representations.

2. **Time:** MIP solvers are NP-hard. Expect:
   - n < 50: seconds to minutes (exact)
   - n = 50–200: minutes to hours (exact or near-optimal)
   - n > 200: heuristic solutions within time limits

3. **Capacity vs. No-Port:** Capacity-aware model is typically harder (more constraints) but more realistic. Use no-port for initialization and validation.

Troubleshooting
---------------

- **Gurobi license error:** Ensure `GUROBI_HOME` is set and license file is valid.
- **Infeasible model:** Check distance matrix for isolated nodes or capacity conflicts.
- **Slow solver:** Reduce `time_limit_seconds` or set `heuristic_only=1` to skip exact branch-and-bound.

References
----------

- Gurobi C API: https://www.gurobi.com/documentation/current/refman/c_api_overview.html
- Paper: "Groundfish Survey Routing: A Scalable Matheuristic"
