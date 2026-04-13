# `sweep`

Matheuristic improvement logic for the survey-routing workflow.

This directory owns the phase that starts from an existing initialization and
improves it through repeated local boundary re-optimization.

Main Component
--------------

```text
sweep/
└── gsp_sweep_mode.c  entrypoint for gsp --mode sweep
```

Role In The Workflow
--------------------

Sweep starts from an existing `init.json` and explores improvements around
adjacent segment boundaries. The high-level orchestration lives here:

- choose which boundary neighborhood to examine
- build the two-segment subproblem
- call the exact MIP solver in `mip/`
- accept or reject the incumbent improvement
- emit JSON snapshots and run summaries

This phase is reported as:

- `mip.phase = "2seg"`

Configuration
-------------

Sweep uses:

- `gurobi.time_limit_seconds.2seg`
- `gurobi.haul_distance_scale.2seg`
- `sweep.max_iterations`

`haul_distance_scale.2seg` controls how haul distance enters the boundary MIP objective:

- `0.0`
  removes haul distance from the sweep MIP objective
- `1e-5`
  keeps haul only as a weak tie-breaker
- `1.0`
  uses full haul distance

The recommended setting for sweep is `1.0`, so accepted improvements are judged
using the real haul-distance contribution in the local MIP objective.

JSON Notes
----------

- `solution.init`
  the starting segmented route loaded from the init phase
- `solution.pass1`, `solution.pass2`, ...
  later accepted sweep states
- `tour_segments_station_ids`
  signed station visit order for each segment
- `tour_segments_station_mutation_ids`
  only the station changes relative to the previous sweep state
- `mip`
  sweep-level solve details and aggregate MIP reporting

Boundary
--------

- `sweep/` decides which neighborhoods to explore and when to stop
- `mip/` solves the exact subproblems
- JSON formatting and generic helpers should live in `common/`, not be duplicated here
