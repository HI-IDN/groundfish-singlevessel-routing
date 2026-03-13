# Phase 0: Initialization Strategies

Common entry point for all initialization heuristics. Each strategy produces
a capacity-feasible segmented tour through all survey stations, starting and
ending at the boat location, with port visits inserted whenever the vessel
reaches capacity.

---

## Usage

```bash
./build/gsp --mode init \
            --strategy <nn|gi|ci> \
            --database dat/gsp_data.db \
            --config config/gsp_solver.yaml \
            --output sol/<strategy>/init.json
```

The boat ID is read from `config/gsp_solver.yaml` (`boat.id`).

---

## Strategies

### `nn` — Nearest Neighbor

Greedy construction starting from the boat start location.

**Algorithm:**
1. From the current position, pick the nearest unvisited station that fits
   within remaining capacity (using min-pair distance over station
   entry/exit endpoints).
2. If all stations are blocked by capacity, retry ignoring capacity
   (fallback to empty-load behaviour).
3. If no station is reachable, divert to the nearest port, reset load,
   open a new segment, and repeat.
4. After adding a station, if load has reached capacity and stations
   remain, immediately divert to the nearest port.
5. Continue until all stations are visited; return to boat end location.

**Distance metric:** minimum over all four entry/exit endpoint combinations
between two nodes (`min_dist_node_pair`).

**Output:** `sol/nn/init.json`

---

### `ge` — Greedy Edge (path-based)

Builds a station ordering by cheapest path insertion anchored to the boat
start and end locations, then applies capacity-aware segmentation.

**Algorithm (ordering phase):**
1. Seed with the station closest to boat start.
2. For each remaining station, evaluate all insertion positions in the
   current path and choose the (station, position) pair with the
   minimum detour cost `d(prev→cand) + d(cand_inside) + d(cand→next) − d(prev→next)`.
3. Repeat until all stations are ordered.

**Algorithm (segmentation phase):**
Same capacity-aware port insertion as NN: pre-station overflow trigger (divert
to nearest port before adding a station that would exceed capacity) and
post-station trigger (divert immediately after a station fills the vessel, if
stations remain).

**Output:** `sol/ge/init.json`

---

### `ci` — Cheapest Insertion (cycle-based)

Builds a station ordering by cheapest cycle insertion seeded from the
closest station pair, then applies capacity-aware segmentation.

**Algorithm (ordering phase):**
1. Seed with the closest pair of stations (min-pair distance over all
   station pairs).
2. For each remaining station, evaluate all insertion positions in the
   current cycle and choose the (station, position) pair with the
   minimum detour cost `d(i→u) + d(u→j) − d(i→j)`.
3. Repeat until all stations are ordered.

**Algorithm (segmentation phase):**
Pre-station overflow trigger: if adding the next station would exceed
capacity, divert to nearest port first. Post-station trigger: if load
reaches capacity exactly and stations remain, divert to nearest port.

**Output:** `sol/ci/init.json`

---

## Output JSON Format

All strategies produce the same JSON schema (example from `sol/nn/init.json`):

```json
{
  "metadata": {
    "solver_version": "init_nn_1.0",
    "timestamp": "<unix timestamp>",
    "mode": "init_nn",
    "strategy": "nn",
    "boat_id": 2,
    "boat_name": "Árni Friðriksson",
    "home_port": {"lat": 64.046333, "lon": -22.139000},
    "boat_location_ids": [2, 2]
  },
  "problem": {
    "num_nodes": 1171,
    "num_stations": 580,
    "capacity": 45000
  },
  "solution": {
    "tour_segments_location_ids": [
      [2, 1008, 1009, ...],
      [464, 1206, 240, ...],
      ...
    ],
    "dock_location_ids": [2, 464, 1176, 75, 197, ...],
    "unique_waypoint_location_ids": [1204, 1206, 1203, ...],
    "tour_segments_station_ids": [
      [500, 501, 499, ...],
      [118, 119, 120, ...],
      ...
    ],
    "tour_length": [101, 49, 171, ...],
    "segment_count": 12,
    "segment_catch_amount": [44991, 44994, 45000, ...],
    "segment_distance_nm": [1326.44, 1032.99, 1995.48, ...],
    "total_distance_nm": 321957.56,
    "feasible": true
  },
  "solver_stats": {
    "status": "init_complete",
    "preprocessing_seconds": 0.518293,
    "runtime_seconds": 0.030164,
    "method": "nearest_neighbor"
  }
}
```

### Field Descriptions

| Field | Description |
|-------|-------------|
| `boat_location_ids` | `[start_location_id, end_location_id]` from `boats` table |
| `tour_segments_location_ids` | One array per segment — ordered location IDs including waypoints for land-crossing legs |
| `dock_location_ids` | Ordered list of all port/boat location IDs visited (start, intermediate ports, end) |
| `unique_waypoint_location_ids` | Deduplicated list of all waypoint location IDs used across all segments |
| `tour_segments_station_ids` | One array per segment — `stations.id` values visited in that segment (no ports, no waypoints) |
| `tour_length` | Number of location nodes in each segment's tour (including waypoints) |
| `segment_count` | Total number of segments |
| `segment_catch_amount` | Total catch (integer, kg) accumulated in each segment; must not exceed `capacity` |
| `segment_distance_nm` | Total travel distance in nautical miles for each segment |
| `total_distance_nm` | Sum of all segment distances plus return leg to boat end |
| `feasible` | `true` if all segments are within capacity and no station is visited twice |
| `preprocessing_seconds` | Time to load nodes, distance matrix, and boat info from database |
| `runtime_seconds` | Time spent in the heuristic solver only (excludes feasibility check) |

---

## Feasibility Check

A solution is marked `feasible: true` if:
- Every station appears exactly once across all `tour_segments_station_ids`
- Every `segment_catch_amount[i] <= capacity`

---

## Notes

- Distances come from the `distances` table in the database — no haversine
  calculation is performed at solve time.
- Waypoints are expanded from `distances.waypoint_path` during JSON output
  for legs that cross land.
- Ports have `start_loc_id == end_loc_id` (single coordinate).
- Stations have distinct `start_loc_id` and `end_loc_id`.
