# `common`

Shared infrastructure that does not belong to a specific init strategy, sweep pass, or MIP model.

This folder is the right home for:

- shared database and data-loading support
- distance and waypoint-routing support
- shared feasibility checks
- reusable parsing and utility code
- future shared JSON / route-expansion helpers

This folder is not meant to hold:

- strategy-specific init logic
- sweep search logic
- Gurobi model implementations
- standalone application entrypoints unless they are truly generic utilities

The stable target is:

- reusable implementation stays in `common/`
- shared interfaces stay in `include/`
- executable entrypoints live outside `common/`
