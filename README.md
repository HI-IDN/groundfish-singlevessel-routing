# Matheuristic Optimization

This repository contains C implementations of optimization models and matheuristics, plus Python utilities to visualize routes and report results. The C binaries use commercial solvers (Gurobi or SCIP) and generate plot bundles that the Python scripts can render. For the current workflow, focus on `capacity.c` and `matcapmutheur.c`; other C files are kept for historical comparison only.

## Repository Layout
- `src/`: C sources and the `Makefile` (primary entry points: `capacity.c` and `matcapmutheur.c`; other C files are historical).
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

Build and place binaries in `bin/`:
```sh
make -C src cap matcapmutheur
```

Run the capacity MIP directly:
```sh
./bin/cap dat/data2023spring.dat 1
```

Run the capacity-boundary matheuristic with mutation:
```sh
./bin/matcapmutheur dat/data2023spring.dat 1 --cap-time-limit 180 --write-dat sol/capmut_1.dat --verbose-init
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

## Algorithm Overview (Capacity + Boundary Mutation)
- **Initialization**: solve a no‑port TSP to get a station order, compute the **minimum** segment count as `ceil(total_amount / ship_capacity)`, then walk the order and **greedily fill to ship capacity**, inserting the nearest port **before** a station if `load + amt > ship_cap` (and load > 0) or **after** a station if `load >= ship_cap` (and more stations remain). This always respects capacity; it may create **extra segments** above the minimum when the fixed order would otherwise overflow.
- **Capacity MIP (`capacity.c`)**: a full MIP with capacity flow constraints and waypoint‑aware distances. Used for benchmarking and for small subproblems.
- **Boundary sweep (`matcapmutheur.c`)**: iterate over adjacent segment pairs and solve a **time‑limited capacity MIP** to reassign stations across the boundary port. Accept only if the **real two‑segment distance** (after re‑optimizing each segment) improves and no segment is infeasible (distance ≥ 100000).
- **Mutation fallback**: if the boundary MIP rejects a change, attempt a **single‑station injection** from outside the pair. Choose the station closest to any station in the pair, insert it into the closest of the two segments (capacity‑feasible), and accept only if the **net change across (left + right + donor)** segments improves.
- **Termination**: stop after a full pass with no accepted changes.

For reproducibility, record solver versions and any run flags (e.g., time limits) in your experiment notes.

## Notes
- `sol/` is intended for generated outputs; keep large artifacts only when needed for paper figures.
- The plotting scripts rely on `py/survey_utils.py` and may recompute distance matrices if requested.
