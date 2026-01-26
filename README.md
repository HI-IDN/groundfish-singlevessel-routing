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
- **Initialization**: solve a no-port TSP to get a station order, compute the number of segments as `ceil(total_amount / ship_capacity)`, set `target_cap = total_amount / segments`, then insert ports greedily so each segment load stays near `target_cap`.
- **Mutation**: apply a chain of station moves between adjacent segments; after the first move, only segments already touched in this chain can supply stations. Optionally attempt a port swap or port merge (both only kept if the stitched pair distance improves; merge also requires combined load within capacity).
- **Evaluation & selection**: re-solve each segment TSP, stitch the tour, and accept the offspring only if the total distance improves.

For reproducibility, record solver versions and any run flags (e.g., time limits) in your experiment notes.

## Notes
- `sol/` is intended for generated outputs; keep large artifacts only when needed for paper figures.
- The plotting scripts rely on `py/survey_utils.py` and may recompute distance matrices if requested.
