# `common`

Shared implementation used across preprocessing, construction, segment, refinement, and reporting.

This directory is the right place for code that is reused across multiple parts
of the workflow and does not belong to a single solver phase.

Current Contents
----------------

```text
common/
├── coastline_db.c   coastline import and DB replacement helpers
├── dat_parser.c     DAT parsing utilities
├── distance.c       distance and routing support
├── feasibility.c    shared feasibility checks
├── json_utils.c     shared JSON writing helpers
├── mip_report.c     shared MIP reporting helpers
└── sql_utils.c      shared SQLite helpers
```

What Belongs Here
-----------------

- shared database and data-loading support
- distance and waypoint-routing support
- feasibility checks
- reusable JSON/statistics helpers
- shared MIP reporting helpers

What Does Not Belong Here
-------------------------

- strategy-specific construction logic
- refinement (matheuristic sweep) search logic
- model-specific Gurobi formulations
- standalone CLI entrypoints

Design Rule
-----------

If code is shared by more than one solver path and is not specific to a single
model, it should usually move into `common/`. The JSON writer helpers are a
good example: `survey`, `construction`, `segment`, `noport`, `fixedport`, and `refinement` should
use
shared utilities here rather than each assembling its own schema.
