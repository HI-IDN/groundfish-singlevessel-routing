# TODO

## Initialization parity fixes

- [ ] Refactor init pipeline so CI/GE/OPT follow the original 2-phase flow:
  1) build no-port station ordering (pure ordering objective),
  2) apply capacity-aware port insertion/segmentation afterwards.
- [ ] Report both distances in init outputs for relevant methods:
  - no-port ordering distance,
  - capacity-segmented route distance.
- [ ] Keep NN as capacity-aware greedy construction (single-pass behavior), but keep shared segmentation utilities modular.

## Distance table / waypoint_path data quality

- [ ] Investigate `distances` rows where `waypoint_path IS NULL` but route should be solvable.
- [ ] Recompute/fill missing waypoint paths for routable pairs so they do not remain `NULL`.
- [ ] Add a validation query/check to flag suspicious rows, e.g. `crosses_land=1` with missing path while reverse/related pairs are routable.



## Suggested verification

- [ ] Compare strategy outputs against original src behavior for NN, CI, GE, OPT.
- [ ] Verify JSON fields reflect intended metrics (`runtime_seconds`, feasibility, both distance measures where applicable).
- [ ] Spot-check problematic location pairs in `distances` after recomputation.



TODO: Coastline Validation + Waypoint Sanity Checks
1. Coastline Geometry Validation (on import from .bin)
   Before inserting the coastline polygon into the database:

Detect invalid geometries (e.g., self-intersections or other topology errors).
Apply a geometric repair step, e.g. buffer(0) or another robust polygon-fixing routine, and verify:

The resulting geometry is valid,
The structure remains a single polygon (or a known multipolygon),
No geometry collapses to an empty or degenerate shape.


Log warnings such as:
“⚠ Warning: Coastline polygon invalid at <coords> → Fixing with buffer(0)… → Valid after fix.”</coords>

This ensures that downstream routing, waypoint generation, and distance calculations never operate on broken coastal boundaries.

2. Waypoint Coverage Sanity Check (after loading waypoints)
   After loading the waypoint set used to prevent land‑crossing in shortest‑path routing:

Verify that the number and placement of waypoints is sufficient to trace a simple polygonal boundary around the coastline (i.e., a coarse envelope that always stays seaward).
Check that:

Connecting waypoints in sequence yields a simple, non‑self‑intersecting polygon.
This polygon should fully contain the coastline but never intersect it.
Distances between consecutive waypoints are small enough that the path can “walk around” all major coastline curvature without cutting corners inland.


Fail or warn if:

The waypoint envelope does not fully contain the coastline,
The polygon is self‑intersecting,
The waypoint count is below a minimum threshold needed for a valid enclosure (e.g., fewer than the number needed to resolve major coastal concavities).



This ensures that the waypoint graph guarantees legal navigation paths (no land‑crossing arcs) and that the routing engine always has a valid corridor around the coast.