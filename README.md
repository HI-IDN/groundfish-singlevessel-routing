# Repository Overview

This repository accompanies the paper  
**_A Scalable Matheuristic for Routing Capacity-Constrained Groundfish Surveys_**  
(Ingimundardóttir et al., LION 2026).

It contains the full pipeline used in the study, including:

- data preprocessing
- initialization heuristics
- MIP-based optimization
- sweep-based improvement
- plotting and reporting tools

The workflow is file-based:

- routing data is stored in `dat/gsp.db`
- solution outputs are written to `sol/`
- figures and summaries are generated from these outputs

---

## Workflow (TL;DR)

- `prepare-routing-data`  
  Builds the routing database used in all subsequent steps

- `survey`  
  Exports observed 2023 survey routes for comparison

- `init`  
  Constructs a capacity-feasible segmented baseline solution

- `sweep`  
  Improves an existing solution via local re-optimization of segment boundaries

---

## Paper

- **Title:** A Scalable Matheuristic for Routing Capacity-Constrained Groundfish Surveys
- **Authors:** Helga Ingimundardóttir, Margrét Vala Þórisdóttir, Bjarki Elvarsson, Thomas Philip Runarsson
- **Conference:** LION 2026 (accepted)

### Keywords

- Groundfish Survey Problem
- Multi-trip VRP
- Capacitated Routing with Replenishment
- Segment-Based Decomposition
- Matheuristics

### Summary

- Scalable matheuristic for single-vessel survey routing
- Builds and refines capacity-feasible tours
- Produces reproducible routing plans
- Achieves consistent distance reductions over strong baselines

> **Abstract**  
> Bottom trawl groundfish surveys are planned around a fixed set of sampling stations, but the order of visits and the timing of port returns strongly affect total travel distance and operational feasibility. Although mixed-integer programming (MIP) models can encode capacity and port-call constraints directly, solving the full model to optimality is often impractical at realistic scales.
>
> We propose a matheuristic that combines fast tour construction with repeated calls to a time-limited capacity MIP subproblem. Starting from an initial segmented tour, the algorithm iteratively examines boundaries between adjacent segments separated by a port visit and solves a restricted two-segment capacity MIP on the union of stations in the two segments, with fixed endpoints and a single intermediate port call.
>
> The MIP reallocates stations between segments subject to capacity, and the best incumbent found within a time limit is accepted if it improves total distance. Across initialization strategies and time limits, the method consistently improves baseline plans and provides a fast, reproducible tool for scenario analysis.

---

## How to Cite

If you use this repository, please cite:
> Ingimundardóttir et al. (2026), *A Scalable Matheuristic for Routing Capacity-Constrained 
> Groundfish Surveys*, LION Conference.

```bib 
@inproceedings{Ingimundardottir2026LION,
  author    = {Ingimundardóttir, Helga and Þórisdóttir, Margrét Vala and Elvarsson, Bjarki and Runarsson, Thomas Philip},
  title     = {A Scalable Matheuristic for Routing Capacity-Constrained Groundfish Surveys},
  booktitle = {Learning and Intelligent Optimization Conference (LION)},
  year      = {2026},
  note      = {Accepted for publication},
}
```


Repository Layout
------
```plaintext
.
├── src/        # core implementation (C + build system)
├── dat/        # raw inputs and generated database (gsp.db)
├── sol/        # outputs (solutions, logs, plots)
├── config/     # solver parameters (YAML)
├── R/          # analysis and visualization scripts
└── docs/       # documentation and experiment summaries
```

Prerequisites
-------------

You need:

- a C toolchain
- SQLite development headers and library
- GEOS
- Gurobi with a valid license for the MIP-based targets
- R for the plotting scripts

This workflow has been tested in an MSYS2 / MinGW environment on Windows. 
It is expected to also work cleanly in Unix-like shells.

Build
-----

Configure the build directory once:

```bash
make -C src config
```

Build everything:

```bash
make -C src build
```

The build products are written under `build/`.

Current Pipeline
----------------

1. Prepare routing data:

```bash
make -C src prepare-routing-data
```

This creates `dat/gsp.db` from:

- `dat/island.tsv`
- `dat/waypoints.dat`
- `dat/ports.dat`
- `dat/boats.dat`
- `dat/stations.dat`

2. Export observed survey routes:

```bash
make -C src survey
```

This writes the observed boat routes to:

- `sol/survey/boat1.json`
- `sol/survey/boat2.json`
- `sol/survey/boat3.json`
- `sol/survey/boat4.json`

3. Optional: run the expensive MIP-based preprocessing paths:

 - *No-port model*: Ignores capacity constraints to produce a global baseline: 
    
    ```bash
    make -C src noport
    ```
    Output: 
     - `sol/noport/noport.json`

 - *Fixed-port model*: Solves a capacity-constrained model with predefined port visits:

    ```bash
    make -C src fixedport_candidates
    make -C src fixedport
    ```

    Output:
     - `dat/candidate_ports.json` 
    - `sol/fixedport/fixedport.json`

4. Initialization

    Constructs a capacity-feasible segmented solution using either:
    - MIP-based input (`noport`, `fixedport`)
    - Heuristic ordering:
      - `nn` (nearest neighbor)
      - `ci` (cheapest insertion)
      - `ge` (greedy edge)
    ```bash
    make -C src init INIT=noport|nn|ge|ci|fixedport
    ```
    Outputs: 
    ```bash 
    sol/<strategy>/init.json
    ```

5. Sweep improvement

    Performs local re-optimization over adjacent segments
    ```bash
    make -C src sweep INIT=noport|nn|ge|ci|fixedport
    ```
    Outputs:
    ```bash 
    sol/<strategy>/sweep.json
    ```

6. Generate plots:

    ```bash
    make -C src plot
    ```

    This generates route figures and survey overview figures from the JSON files
    currently present under `sol/`.

Batch Run
---------

To run the current experiment chain in one command:

```bash
make -C src experiments
```

This runs:

- routing-data preparation
- survey export
- noport and fixedport presolve
- initialization outputs
- sweep outputs

Then generate figures separately with:

```bash
make -C src plot
```

Configuration
-------------

The solver reads configuration from:

- `config/gsp_solver.yaml`

Current Gurobi phase parameters are organized by segment model:

```yaml
gurobi:
  haul_distance_scale:
    0seg: 0.00001
    1seg: 0.00001
    2seg: 1.0
    Xseg: 0.00001

  time_limit_seconds:
    0seg: 0
    1seg: 0
    2seg: 0
    Xseg: 86400
```

Where:

- `0seg` = noport
- `1seg` = init local post-optimization
- `2seg` = sweep boundary optimization
- `Xseg` = fixed-port model

Useful Targets
--------------

```bash
make -C src help
```

Common targets:

- `make -C src build`
- `make -C src prepare-routing-data`
- `make -C src survey`
- `make -C src experiments`
- `make -C src plot`

Notes
-----

- `src/Makefile` is a lightweight wrapper around the CMake build in `build/`.
- Long MIP runs can be expensive. Prefer targeted runs over rebuilding or rerunning the full pipeline.
- Route JSON files under `sol/` are the current source of truth for plotting and result summaries.
