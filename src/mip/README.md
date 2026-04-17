# `mip`

Gurobi-backed model implementations.

This directory should contain solver formulations only. It is not the place for
workflow entrypoints, JSON writers, or phase-specific orchestration.

Current Models
--------------

```text
mip/
├── noport.c            no-port ordering model
├── endpaired_tsp.c     exact segment solver used by segment local post-optimization and refinement
├── fixedport.c         fixed-port capacity model
├── mip_common.c        shared Gurobi environment, parameter, and status helpers
├── mip_paired_tour.c   shared paired-tour extraction and callback support
└── include/            model-specific headers and shared MIP interfaces
```

Objective Scaling
-----------------

The models in this directory use `haul_distance_scale` to control how haul legs
enter the MIP objective.

Purpose:

- `0.0`
  removes haul distance from the objective
- a small positive value such as `1e-5`
  keeps haul distance only as a tie-breaker while preserving its relative scale
- `1.0`
  uses full haul distance

Recommended phase settings in the current workflow:

- `0seg = 1e-5`
  for `noport`
- `1seg = 1e-5`
  for segment local post-optimization
- `2seg = 1.0`
  for refinement (matheuristic sweep)
- `Xseg = 1e-5`
  for fixed-port

Boundary
--------

What belongs here:

- exact MIP formulations
- model-specific callbacks
- shared Gurobi utilities

What does not belong here:

- CLI entrypoints
- construction, segment, or refinement orchestration
- report formatting
- JSON assembly

Those responsibilities should stay in `init/`, `sweep/`, `tools/`, or `common/`.
