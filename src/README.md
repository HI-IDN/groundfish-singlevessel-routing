# `src`

Work-in-progress refactor of the Groundfish Survey Routing codebase.

The intended split is:

- `init/`
  Phase 0 construction logic and init-facing entrypoints.
- `mip/`
  Gurobi-backed model implementations only.
- `sweep/`
  Phase 1 matheuristic logic that calls the required MIP/TSP subsolvers.
- `common/`
  shared non-solver infrastructure: database loaders, feasibility checks, routing support, and reusable helpers.
- `include/`
  shared headers used across modules.
- `tools/`
  standalone utilities that are not part of the main `gsp` solve loop.

Current status:

- `init/` contains the implemented construction heuristics `nn`, `ge`, `ci`, plus the OPT init path built from a no-port ordering.
- all init methods share a final local segment post-optimization step controlled by `init.local_post_opt.time_limit_seconds` in `config/gsp_solver.yaml`
- `mip/` contains the no-port model and placeholders for the other Gurobi models.
- `sweep/` is being rebuilt around the paper’s boundary-sweep phase.
- sweep now assumes imported `init.json` already contains that local post-optimized baseline and preserves imported `dock_location_ids` on load
- some JSON writing and waypoint-expansion code is still duplicated across modules and should be centralized later.

Design intent:

- initialization strategies live in `init/`
- solver formulations live in `mip/`
- sweep orchestration lives in `sweep/`
- shared plumbing should move toward `common/` rather than being reimplemented per executable

Common wrapper commands:

- `make -C src prepare-routing-data`
  runs the routing-data pipeline in one pass: coastline/waypoint bootstrap, station import, distance build, and unused-waypoint pruning
- `make -C src init_nn`
- `make -C src init_ge`
- `make -C src init_ci`
- `make -C src init_opt`
