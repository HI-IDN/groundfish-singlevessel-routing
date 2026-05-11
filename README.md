# Repository Overview

This repository accompanies the paper  
**_A Scalable Matheuristic for Routing Capacity-Constrained Groundfish Surveys_**  
(Ingimundardóttir et al., LION 2026).

It contains the full pipeline used in the study, including:

- data preprocessing
- construction heuristics
- MIP-based optimization
- refinement (matheuristic sweep)
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

- `construction`  
  Builds a route ordering or route template

- `segment`  
  Converts a construction into a capacity-feasible segmented baseline

- `refinement`  
  Improves a segmented baseline via the matheuristic sweep

---

## Paper

- **Title:** A Scalable Matheuristic for Routing Capacity-Constrained Groundfish Surveys
- **Authors:** Helga Ingimundardóttir, Margrét Vala Þórisdóttir, Bjarki Elvarsson, Thomas Philip
  Runarsson
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
> Bottom trawl groundfish surveys are planned around a fixed set of sampling stations, but the order
> of visits and the timing of port returns strongly affect total travel distance and operational
> feasibility. Although mixed-integer programming (MIP) models can encode capacity and port-call
> constraints directly, solving the full model to optimality is often impractical at realistic scales.
>
> We propose a matheuristic that combines fast tour construction with repeated calls to a
> time-limited capacity MIP subproblem. Starting from an initial segmented tour, the algorithm
> iteratively examines boundaries between adjacent segments separated by a port visit and solves a
> restricted two-segment capacity MIP on the union of stations in the two segments, with fixed
> endpoints and a single intermediate port call.
>
> The MIP reallocates stations between segments subject to capacity, and the best incumbent found
> within a time limit is accepted if it improves total distance. Across initialization strategies and
> time limits, the method consistently improves baseline plans and provides a fast, reproducible tool
> for scenario analysis.

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
├── dat/        # raw inputs and generated databases (gsp.db, solution.db)
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

Before the full build, you can verify that SQLite, GEOS, and Gurobi are all
correctly linked on your system:

```bash
make -C tools test-env
```

This compiles and runs three small smoke-tests (`test_sqlite`, `test_geos`,
`test_gurobi`) and reports PASS/FAIL for each. Fix any failures before
proceeding — the main pipeline will not build or run correctly without all
three libraries.

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
    - `sol/fixedport/construction.json`

4. Segmentation

   Constructs a capacity-feasible segmented solution using either:
    - MIP-based input (`noport`, `fixedport`)
    - Heuristic ordering:
        - `nn` (nearest neighbor)
        - `ci` (cheapest insertion)
        - `ge` (greedy edge)
    ```bash
    make -C src segment METHOD=noport|nn|ge|ci|fixedport
    ```
   Outputs:
    ```bash 
    sol/<strategy>/segment.json
    ```

5. Refinement (matheuristic sweep)

   Performs local re-optimization over adjacent segments
    ```bash
    make -C src refinement METHOD=noport|nn|ge|ci
    ```
   Outputs:
    ```bash 
    sol/<strategy>/refinement.json
    ```

6. Generate plots:

    ```bash
    make -C src plot
    ```

   This recreates `dat/solution.db` from the current JSON files under `sol/`,
   then runs the static R plotting scripts. The generated figures include the
   survey overview, waypoint check, multivessel survey routes, construction and
   segmentation panels, refinement panels, refinement sweep summaries, and MIP
   solve diagnostics.

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
- construction outputs
- segment outputs
- refinement outputs

Then generate figures with:

```bash
make -C src plot
```

Normalized Solution Database
----------------------------

`make -C src build` builds the C target `solution_db_export`.
`make -C src solution_db` recreates `dat/solution.db` from the current
`sol/**/*.json` files, and `make -C src plot` runs that normalization before
generating figures. The database stores solution output only; static geography
remains in `dat/gsp.db`.
For plotting routes, join `solution.db.location_segments.location_id` to
`gsp.db.locations.id`.

Main tables:

- `runs`
  One row per exported solution state. The lineage is construction ->
  segmentation -> refinement. Refinement JSONs may contribute several rows
  (`init`, `pass1`, ...), with `parent_run_id` linking the chain.

- `location_segments`
  Ordered route location IDs: `run_id`, `segment`, `sequence`, `location_id`,
  and `point_type` (`BOAT`, `PORT`, `WAYP`, `STAT`). Latitude/longitude are
  not duplicated here. `route_locations` is kept as a compatibility view over
  this table.

- `station_segments`
  Ordered station membership per segment: `run_id`, `segment`, `sequence`,
  `signed_station_id`, and absolute `station_id`.

- `distance`
  Distance by run and segment. `segment IS NULL` is the grand total row;
  numbered segments are per-segment rows. Columns are `transit_nm` and
  `total_nm`.

- `mip_solves`
  Generic solve-level table for runtime analysis. It intentionally has no
  `run_id`; it stores only `phase_code` (`C`, `S`, `R`), `segment_model`
  (`0seg`, `1seg`, `2seg`, `Xseg`), `station_count`, `node_count`,
  `model_variable_count`, `model_constraint_count`, `runtime_seconds`, and
  `gap_percent`.

- `refinement_passes`
  One row per refinement pass, keyed by grouped refinement `run_id` and
  `pass_number`. For example, `ci:refinement:l2seg=10:pass1` in `runs` is stored
  as `run_id = ci:refinement:l2seg=10`, `pass_number = 1`, with
  `solution_run_id` pointing back to the concrete `runs.run_id`. It stores
  stations moved, boundary attempts/changes, MIP solve count, and runtime.

- `refinement_solve_context`
  Refinement-only context rows keyed back to `(run_id, pass_number)`. This keeps
  boundary index, candidate split index, segment, and moved-station count tied
  to a specific pass. Generic model-size/runtime/gap fields stay in
  `mip_solves`.

- `refinement_station_mutations`
  Station-level moved/mutated station IDs from
  `tour_segments_station_mutation_ids`, keyed by grouped refinement `run_id`,
  `pass_number`, segment, and sequence.

Configuration
-------------

The solver reads configuration from:

- `config/gsp_solver.yaml`

Current Gurobi phase parameters are organized by segment model:

```yaml
gurobi:

  time_limit_seconds:
    0seg: 0
    1seg: 0
    2seg: 0
    Xseg: 86400
```

Where:

- `0seg` = noport
- `1seg` = segment local post-optimization
- `2seg` = refinement (matheuristic sweep) boundary optimization
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
- Long MIP runs can be expensive. Prefer targeted runs over rebuilding or rerunning the full
  pipeline.
- Route JSON files under `sol/` are normalized into `dat/solution.db` before plotting and result summaries.
