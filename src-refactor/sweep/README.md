# `sweep`

Phase 1 matheuristic logic.

This folder is for the improvement phase described in the paper: take an existing segmented
solution, run sweep-style local improvement, and call the necessary exact subsolvers from `mip/`.

Intended boundary:

- `sweep/` decides what neighborhood to explore and when to stop
- `mip/` solves the exact subproblems

This folder should not become a second home for standalone solver formulations.

Current direction:

- `gsp_sweep_mode.c`
  rebuilds the paper-style sweep mode around segmented boundary improvements and incremental JSON
  snapshots
- older files in this folder may still reflect earlier experiments and should be treated as
  transitional until the refactor settles

