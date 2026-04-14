# `src`

Core implementation for the groundfish survey routing workflow described in the
paper.

The code is organized by responsibility:

```text
src/
|-- init/      initialization logic and init-facing executables
|-- sweep/     refinement logic starting from an existing segment.json
|-- mip/       Gurobi-backed model implementations only
|-- common/    shared infrastructure used across the pipeline
|-- include/   shared non-MIP headers
`-- tools/     standalone utilities outside the main init/sweep solve loop
```

Pipeline Overview
-----------------

The main flow is:

1. prepare routing data
2. export observed survey routes
3. generate a construction
4. build a segmented baseline
5. improve that segmented baseline with refinement

Construction methods currently used in the paper:

- `nn`
  nearest-neighbor heuristic
- `ge`
  greedy-edge heuristic
- `ci`
  cheapest-insertion heuristic
- `noport`
  expensive MIP ordering, followed by conversion into `segment.json`
- `fixedport`
  expensive fixed-port capacity MIP based on survey-derived candidate port visits

The heuristic methods `nn`, `ge`, and `ci` currently produce the construction
artifact directly from the `gsp --mode init` path. The `noport` and `fixedport`
paths are separate because they start with more expensive MIP solves.

Current Executables And Entry Points
------------------------------------

- `gsp`
  non-Gurobi build of the main construction / refinement entrypoint
- `gsp_gurobi`
  Gurobi-enabled build of the main construction / refinement entrypoint
- `gsp_segment_from_construction`
  converts an ordered route, such as `construction.json`, into `segment.json`
- `gsp_noport`
  standalone no-port ordering executable
- `gsp_fixedport`
  standalone fixed-port capacity model executable
- `historical_survey`
  exports observed survey routes to JSON
- `survey_fixedport_candidates`
  derives candidate fixed-port visit sequences from survey JSON
- `gsp_country`, `gsp_stations`, `gsp_distances`, `gsp_prepare_routing_data`
  routing-data preprocessing utilities

JSON Conventions
----------------

The JSON emitted by the solver paths is converging on a shared structure:

- `summary`
  shared status, distance, runtime, and MIP summary fields
- `solution.<variant>`
  route data for a named solution state such as `presolve`, `final`, or `pass1`
- `distance_nm.segment`
  per-segment `transit`, `haul`, and `total`
- `distance_nm.grand_total`
  overall `transit`, `haul`, and `total`
- `mip`
  phase-specific solve-detail tuples and aggregate MIP reporting

For `segment.json`, the local post-optimization phase is reported as `mip.phase = "1seg"`.
For `refinement.json`, the boundary improvement phase is reported as `mip.phase = "2seg"`.

Configuration Notes
-------------------

The main solver configuration lives in `config/gsp_solver.yaml`.

Two Gurobi settings matter across multiple phases:

- `gurobi.time_limit_seconds.{0seg,1seg,2seg,Xseg}`
  per-phase MIP time limits
- `gurobi.haul_distance_scale.{0seg,1seg,2seg,Xseg}`
  objective scaling for haul distance inside the MIP

`haul_distance_scale` controls whether haul distance is effectively removed from
the objective or retained as a tie-breaker:

- `0.0`
  removes haul distance from the MIP objective
- a very small positive value such as `1e-5`
  keeps haul distance only as a weak tie-breaker
- `1.0`
  uses full haul distance in the objective

Recommended settings in the current workflow:

- `0seg = 1e-5`
  for `noport`, so haul distance is almost ignored but still breaks ties
- `1seg = 1e-5`
  for local post-optimization during segmentation
- `2seg = 1.0`
  for refinement, where the improvement phase should use real haul distance
- `Xseg = 1e-5`
  for the fixed-port full MIP path

Module Boundaries
-----------------

- workflow orchestration belongs in `init/` and `sweep/`
- exact solver formulations belong in `mip/`
- shared reusable logic belongs in `common/`
- shared interfaces belong in `include/`
- standalone utilities belong in `tools/`

That split keeps solver formulations separate from JSON/reporting logic and
keeps the high-level workflow readable.
