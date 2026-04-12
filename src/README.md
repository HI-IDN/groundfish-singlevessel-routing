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

- `init/` contains the implemented construction heuristics `nn`, `ge`, `ci`, plus the ordered-input init converter used by `noport`.
- all init methods share a final local segment post-optimization step controlled by `gurobi.time_limit_seconds.init` in `config/gsp_solver.yaml`
- `mip/` contains the no-port model and placeholders for the other Gurobi models.
- `sweep/` is being rebuilt around the paper’s boundary-sweep phase.
- sweep now assumes imported `init.json` already contains that local post-optimized baseline and preserves imported `dock_location_ids` on load
- some JSON writing and waypoint-expansion code is still duplicated across modules and should be centralized later.

JSON MIP solve-detail tuples:

- MIP solve details live in the top-level `mip` section, not in `summary`.
- `init.json` writes `mip.phase = "1seg"` for local segment post-optimization.
- `sweep.json` writes `mip.phase = "2seg"` for boundary-sweep segment reoptimization.
- `mip.timeout_seconds` is the per-MIP time limit used by that phase.
- `init.json` and `sweep.json` use the same solve tuple layout: `[node_count, mip_size, runtime_seconds, gap_percent]`.
- `node_count` is `stations + 2` because each segment MIP includes the segment stations plus start/end dock nodes.
- `mip_size` is `[num_vars, num_constraints]`. For example, `mip_size = [5184, 180]` means 5,184 binary decision variables and 180 linear constraints.

Design intent:

- initialization strategies live in `init/`
- solver formulations live in `mip/`
- sweep orchestration lives in `sweep/`
- shared plumbing should move toward `common/` rather than being reimplemented per executable

Common wrapper commands:

- `make -C src prepare-routing-data`
  runs the routing-data pipeline in one pass: coastline/waypoint bootstrap, station import, distance build, and unused-waypoint pruning
- `make -C src init INIT=<method>` where `method` is either `noport`, `nn`, `ci` or `ge`.
