# `src-refactor`

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
- `mip/` contains the no-port model and placeholders for the other Gurobi models.
- `sweep/` is being rebuilt around the paper’s boundary-sweep phase.
- some JSON writing and waypoint-expansion code is still duplicated across modules and should be centralized later.

Design intent:

- initialization strategies live in `init/`
- solver formulations live in `mip/`
- sweep orchestration lives in `sweep/`
- shared plumbing should move toward `common/` rather than being reimplemented per executable
