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

JSON notes:

- `solution.init` is the starting segmented solution loaded from Phase 0
- later sweep states are written as `solution.pass1`, `solution.pass2`, ...
- `tour_segments_station_ids` stores the signed station visit order for each segment (minus 
  implies reversed order, i.e., from end to start).
- `tour_segments_station_mutation_ids` stores only the stations that changed relative to the
  previous sweep state
- in `tour_segments_station_mutation_ids`, `-station_id` means the station left that segment and
  `+station_id` means the station entered that segment
