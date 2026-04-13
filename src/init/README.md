# `init`

Initialization logic for the survey-routing workflow.

This directory owns:

- heuristic construction methods
- init-side orchestration
- conversion from ordered solver output into `segment.json`
- shared local segment post-optimization used after initialization

It does not own the exact MIP formulations themselves. Those belong in `mip/`.

Main Components
---------------

```text
init/
|-- gsp_init_mode.c         entrypoint for heuristic construction and segment generation
|-- construction_nn.c       nearest-neighbor construction
|-- construction_ge.c       greedy-edge construction
|-- construction_ci.c       cheapest-insertion construction
|-- segment_from_order.c    converts an ordered route such as sol/noport/construction.json into segment.json
|-- noport_order_main.c     standalone executable for the no-port ordering model
|-- fixedport_order_main.c  standalone executable for the fixed-port capacity model
`-- segment_postopt.c       shared local post-optimization over fixed segment boundaries
```

Construction Families
---------------------

Direct heuristic construction methods:

- `nn`
- `ge`
- `ci`

MIP-driven construction-related paths:

- `noport`
  solves an expensive ordering model first, then converts the result into `segment.json`
- `fixedport`
  solves a fixed-port capacity model from survey-derived port candidates

Local Post-Optimization
-----------------------

All heuristic construction methods share a final local segment post-optimization step.
That step re-solves each segment independently while keeping the segment
boundaries fixed.

This phase is reported as:

- `mip.phase = "1seg"`

Configuration comes from:

- `gurobi.time_limit_seconds.1seg`
- `gurobi.haul_distance_scale.1seg`

`haul_distance_scale.1seg` controls how strongly haul distance enters the local
segment MIP objective:

- `0.0`
  removes haul distance entirely
- `1e-5`
  keeps haul as a weak tie-breaker
- `1.0`
  uses full haul distance

The current recommended setting for local post-optimization is `1e-5`.

Outputs
-------

Typical outputs from this part of the pipeline:

- `sol/nn/construction.json`
- `sol/nn/segment.json`
- `sol/ge/construction.json`
- `sol/ge/segment.json`
- `sol/ci/construction.json`
- `sol/ci/segment.json`
- `sol/noport/construction.json`
- `sol/noport/segment.json`
- `sol/fixedport/construction.json`
- `sol/fixedport/segment.json`

Boundary
--------

- `init/` decides how to build the construction and segmented baseline
- `mip/` solves the exact subproblems used by init
- shared reporting and helper logic should stay out of `init/` when it can live in `common/`
