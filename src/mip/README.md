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

Objective
---------

All models minimise transit distance only. Haul arcs (the intra-station
entry/exit leg) are excluded from the MIP objective; station orientation is
determined by the solver as a free choice with no distance penalty.

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
