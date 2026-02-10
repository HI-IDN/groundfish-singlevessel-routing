Initialization Strategies for Groundfish Survey Routing
=========================================================

Overview
--------

This module provides four initialization strategies to generate an initial solution (tour + sequence of stations) for the Groundfish Survey Routing problem. Each strategy reports:
- The solution tour (station order)
- Total distance of the tour
- Runtime (wall-clock milliseconds)
- Strategy name
- Comprehensive logs

The `init` binary dispatches to the chosen strategy via the `--strategy` flag.

Strategies
----------

### 1. Nearest Neighbor (NN) — `strategy_nn.c`

**Algorithm:**
1. Start at a random (or specified) station
2. Greedily visit the nearest unvisited station
3. Repeat until all stations visited
4. Return to start station

**Properties:**
- **Runtime:** O(n²) 
- **Solution quality:** Often 25–40% above optimal
- **No-port solution:** NN inherently produces a closed tour; no additional processing needed
- **Always has a feasible solution** (assuming connected graph)

**Pros:**
- Fast and simple
- Good baseline for comparison

**Cons:**
- Greedy decisions can lead to bad local optima (poor final edges)

**Output fields:**
```json
{
  "strategy": "nn",
  "tour": [5, 12, 3, 45, ...],
  "total_distance": 12345.67,
  "runtime_ms": 42,
  "num_stations": 150,
  "start_station": 5
}
```

### 2. Cheapest Insertion (CI) — `strategy_ci.c`

**Algorithm:**
1. Start with a partial tour (e.g., the three farthest stations forming a triangle)
2. Repeatedly find the unvisited station closest to the current tour
3. Insert it at the position that increases tour length the least
4. Repeat until all stations visited

**Properties:**
- **Runtime:** O(n²) (or O(n³) with naive insertion search)
- **Solution quality:** Often 15–25% above optimal (better than NN)
- **No-port solution:** CI constructs a closed tour; all stations included in order

**Pros:**
- Better tour quality than NN
- Still relatively fast

**Cons:**
- More complex to implement
- Initial triangle choice can affect final solution

**Output fields:**
```json
{
  "strategy": "ci",
  "tour": [5, 12, 3, 45, ...],
  "total_distance": 11500.23,
  "runtime_ms": 156,
  "num_stations": 150,
  "initial_triangle": [5, 120, 88]
}
```

### 3. Greedy Edge (GE) — `strategy_ge.c`

**Algorithm:**
1. Sort all edges by distance (cheapest first)
2. Greedily add edges to the tour if they don't:
   - Create a degree-3 node (each node has degree ≤ 2)
   - Create a subtour (unless completing the final Hamiltonian cycle)
3. Continue until a single cycle includes all nodes

**Properties:**
- **Runtime:** O(m log m) where m = number of edges
- **Solution quality:** Competitive with CI, often 15–30% above optimal
- **No-port solution:** GE builds a Hamiltonian cycle by construction

**Pros:**
- Often produces competitive solutions
- Different heuristic than NN/CI (good for ensemble methods)

**Cons:**
- More complex implementation (degree tracking, subtour detection)
- Requires efficient union-find or cycle detection

**Output fields:**
```json
{
  "strategy": "ge",
  "tour": [5, 12, 3, 45, ...],
  "total_distance": 11200.50,
  "runtime_ms": 89,
  "num_stations": 150,
  "edges_added": 150
}
```

### 4. Optimization (OPT) — `strategy_opt.c` (calls NP-MIP)

**Algorithm:**
1. Formulate the "no-port" TSP: minimize total distance subject to:
   - Each station visited exactly once
   - Single cycle (Hamiltonian cycle via subtour elimination)
   - No explicit port/endpoint constraints
2. Solve via Gurobi MIP (exact branch-and-bound)
3. Extract tour from optimal MIP solution

**Properties:**
- **Runtime:** Depends on instance size; runs until completion
- **Solution quality:** Optimal
- **No-port solution:** MIP solver guarantees a single cycle through all stations
- **Requires Gurobi license**

**Pros:**
- Guaranteed optimal solution (within time/gap limits)
- Baseline for algorithm performance comparison

**Cons:**
- Significantly slower than NN/CI/GE
- Requires Gurobi installation and license

**Output fields:**
```json
{
  "strategy": "opt",
  "tour": [5, 12, 3, 45, ...],
  "total_distance": 10800.12,
  "runtime_ms": 45231,
  "num_stations": 150,
  "mip_status": "OPTIMAL",
  "mip_gap": 0.0,
  "mip_iterations": 12345,
  "solver_log_excerpt": "..."
}
```

CLI Usage
---------

### Basic Usage

```bash
./bin/init \
  --db dat/parsed_data.sqlite \
  --strategy nn \
  --output json \
  --log-level info
```

### Arguments

- `--db <path>` — SQLite database (required; must contain parsed locations/stations)
- `--strategy {nn|ci|ge|opt}` — Initialization strategy (required)
- `--output {json|csv}` — Output format (default: json)
- `--log-level {debug|info|warn|error}` — Logging verbosity (default: info)
- `--mip-threads <n>` — Gurobi thread count for opt (default: auto)
- `--seed <n>` — Random seed for NN/CI initial selection (optional)
- `--save-to-db` — Insert result into SQLite `solutions` table (optional)

### Examples

**Nearest Neighbor, verbose logging:**
```bash
./bin/init --db data.sqlite --strategy nn --log-level debug
```

**Cheapest Insertion, output to CSV:**
```bash
./bin/init --db data.sqlite --strategy ci --output csv > init_ci.csv
```

**Exact optimization (no time limit):**
```bash
./bin/init --db data.sqlite --strategy opt --mip-threads 4
```

**Save all results to database:**
```bash
for s in nn ci ge opt; do
  ./bin/init --db data.sqlite --strategy $s --save-to-db --log-level info
done
```

Output Format
-------------

### JSON Example

```json
{
  "metadata": {
    "timestamp": "2024-02-10T15:32:45Z",
    "strategy": "nn",
    "database": "dat/parsed_data.sqlite",
    "log_level": "info"
  },
  "instance": {
    "num_stations": 150,
    "num_ports": 2,
    "num_waypoints": 8
  },
  "solution": {
    "tour": [5, 12, 3, 45, 67, ..., 5],
    "tour_length": 151,
    "total_distance": 12345.67,
    "total_distance_no_return": 12100.00
  },
  "performance": {
    "runtime_ms": 42,
    "runtime_seconds": 0.042,
    "iterations": 150
  },
  "logs": [
    "[INFO] Loading database: dat/parsed_data.sqlite",
    "[INFO] Loaded 150 stations, 2 ports, 8 waypoints",
    "[INFO] Running Nearest Neighbor initialization...",
    "[INFO] Starting from station 5",
    "[INFO] Tour length: 151 nodes",
    "[INFO] Total distance: 12345.67 nm",
    "[INFO] Runtime: 42 ms",
    "[INFO] Initialization complete"
  ]
}
```

### CSV Example

```csv
timestamp,strategy,num_stations,num_ports,total_distance,runtime_ms,tour
2024-02-10T15:32:45Z,nn,150,2,12345.67,42,"5,12,3,45,67,..."
```

Data Flow
---------

1. **Input:** SQLite database (from preprocessing stage)
   - Tables: `locations`, `stations`, `ports`, `waypoints`
   - Columns: node id, lat/lon, type, demand (if applicable)

2. **Processing:** Strategy-specific algorithm
   - Builds distance matrix (Euclidean or geodetic)
   - Computes tour via heuristic or MIP solver
   - Measures runtime

3. **Output:** JSON or CSV
   - Tour (node sequence)
   - Total distance
   - Runtime
   - Metadata and logs

4. **Optional:** Insert into SQLite `solutions` table
   - Enables easy downstream querying and analysis

References
----------

- Nearest Neighbor: Classic greedy heuristic for TSP
- Cheapest Insertion: Rosenkrantz et al., "Approximate algorithms for traveling salesman problem"
- Greedy Edge: Kruskal-like edge-based construction
- Paper: "Groundfish Survey Routing: A Scalable Matheuristic"

Troubleshooting
---------------

- **Database not found:** Ensure preprocessing stage ran first; check `--db` path
- **No stations in database:** Check that parser correctly loaded data
- **OPT strategy slow:** For large instances, consider using a heuristic strategy or running on more powerful hardware
- **NN/CI/GE produce identical tours:** May indicate small instance or poor heuristic seed; try `--seed` variations
