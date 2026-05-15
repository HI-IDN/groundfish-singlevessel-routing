# `sweep`

Refinement (matheuristic sweep) logic for the survey-routing workflow.

This directory owns the phase that starts from an existing segmented baseline
and improves it through repeated local boundary re-optimization.

Main Component
--------------

```text
sweep/
|-- gsp_sweep_mode.c      entrypoint for matheuristic refinement
|-- gsp_sweep_fallback.c  optional donor-segment fallback extension
`-- gsp_sweep_fallback.h  fallback interface used by the sweep entrypoint
```

Role In The Workflow
--------------------

Refinement (matheuristic sweep) starts from an existing `segment.json` and explores improvements
around
adjacent segment boundaries. The high-level orchestration lives here:

- choose which boundary neighborhood to examine
- build the two-segment subproblem with the two adjacent segment station sets
- call the fixed-port capacity MIP in `mip/`, using the current boundary port as the single fixed unload
- accept or reject the incumbent improvement
- emit JSON snapshots and run summaries

This phase is reported as:

- `mip.phase = "2seg"`

Configuration
-------------

Refinement uses:

- `gurobi.time_limit_seconds.2seg`
- `sweep.max_iterations`
- `sweep.fallback_enabled`

The boundary MIP minimises transit distance only; haul arcs are excluded from
the objective.

Fallback Extension
------------------

The default refinement path is the two-segment boundary re-optimization used by
the main experiments. The donor-segment fallback is deliberately separated into
`gsp_sweep_fallback.c` and is disabled by default (`sweep.fallback_enabled:
false`).

The fallback is an optional extension hook for future experiments. If enabled,
it can try a donor station from a non-adjacent segment after the standard
two-segment candidate fails to improve the boundary. This behavior is not part
of the default paper flow and should be enabled only for targeted exploratory
runs:

```bash
make -C src refinement METHOD=nn SWEEP_FALLBACK=true
```

Keeping the fallback in its own module keeps the main sweep control flow focused
on the reported two-segment method and leaves a clear place for future variants,
such as three-segment re-optimization.

JSON Notes
----------

- `solution.init`
  the starting segmented route loaded from the segment phase
- `solution.pass1`, `solution.pass2`, ...
  later accepted refinement states
- `tour_segments_station_ids`
  signed station visit order for each segment
- `tour_segments_station_mutation_ids`
  only the station changes relative to the previous refinement state
- `mip`
  refinement-level solve details and aggregate MIP reporting

Boundary
--------

- `sweep/` decides which neighborhoods to explore and when to stop during refinement (matheuristic
  sweep)
- `sweep/gsp_sweep_fallback.c` owns optional fallback behavior and should remain
  independent from the default two-segment sweep path
- `mip/` solves the exact subproblems
- JSON formatting and generic helpers should live in `common/`, not be duplicated here
