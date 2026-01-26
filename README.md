# Matheuristic Optimization

This repository contains C implementations of optimization models and matheuristics, plus Python utilities to visualize routes and report results. The C binaries use commercial solvers (Gurobi or SCIP) and generate plot bundles that the Python scripts can render.

## Repository Layout
- `src/`: C sources and the `Makefile` (primary entry points: `matheur.c` and `capacity.c`; `main.c`/`main_scip.c` are demo speed comparisons).
- `bin/`: compiled executables and runtime assets (e.g., `island.bin`).
- `lib/`: shared libraries produced by `make` (e.g., `libutils.*`).
- `dat/`: input datasets (`*.dat`).
- `sol/`: solution outputs, plots, and exported tables.
- `py/`: plotting and analysis scripts.
- `tex/`: working paper and figures.

## Build and Run
The build expects local solver installs. Set environment variables as needed:
- `GUROBI_HOME=/Library/gurobi1300/macos_universal2`
- `SCIP_HOME=$HOME/.local/scipoptsuite-10.0.0`

Build all targets and place binaries in `bin/`:
```sh
make -C src all
```

Run a demo (Gurobi build, for comparison only):
```sh
./bin/demo dat/data2023spring.dat 1
```

Run the SCIP variant:
```sh
./bin/demo_scip dat/data2023spring.dat 1
```

## Plotting Results
C executables write a plot bundle (default `solution_plot.txt`) in the working directory. Render it with:
```sh
python3 py/plot_solution.py --sol solution_plot.txt --save sol/out.png
```

For convergence monitoring and hill-climb diagnostics:
```sh
python3 py/plot_hillclimb.py
```

Remark: the land mask `island.bin` is loaded relative to the current working directory. If plotting with `--recompute`, run from `bin/` so `island.bin` is found:
```sh
cd bin && ../py/plot_solution.py --sol ../sol/all_res_1.txt --recompute
```

## Algorithm Overview (ES(1+1) Hillclimb)
- **Initialization**: solve a no-port TSP to get a station order, compute the number of segments as `ceil(total_amount / ship_capacity)`, then walk the order using a **dynamic target** `target_cap = remaining_amount / remaining_segments`. Insert the nearest port **before** a station if `load + amt > target_cap` (and load > 0) and **after** a station if `load >= target_cap`, updating `remaining_amount` and `remaining_segments` each time a port is inserted. This keeps segment loads near the evolving average; segments may still exceed it when a single station is large or no port is available.
- **Mutation**: apply a chain of station moves between segments that are close enough (minimum cross‑segment station distance <= `max(tolA, tolB)`, where each `tol` is the mean nearest‑neighbor distance within the segment; segments with 0–1 station use `+inf`). After the first move, only touched segments can supply stations. Optionally attempt a port swap or port merge (both only kept if the stitched pair distance improves; merge also requires combined load within capacity).
- **Evaluation & selection**: re-solve each segment TSP, stitch the tour, and accept the offspring only if the total distance improves.

For reproducibility, record solver versions and any run flags (e.g., time limits) in your experiment notes.

## Notes
- `sol/` is intended for generated outputs; keep large artifacts only when needed for paper figures.
- The plotting scripts rely on `py/survey_utils.py` and may recompute distance matrices if requested.
