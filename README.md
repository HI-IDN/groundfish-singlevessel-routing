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
- **Initialization**: solve a no-port TSP to get a station order, compute the **minimum** segment count as `ceil(total_amount / ship_capacity)`, then walk the order and **greedily fill to ship capacity**, inserting the nearest port **before** a station if `load + amt > ship_cap` (and load > 0) or **after** a station if `load >= ship_cap` (and more stations remain). This always respects capacity; it may create **extra segments** above the minimum when the fixed order would otherwise overflow.
- **Mutation**: for each hillclimb iteration, attempt a **chain** of station moves (up to `mutations`, stopping early with probability `1 - mut_prob`). The first move chooses any segment; subsequent moves may only take stations from segments touched so far. A source/destination pair is admissible only if the minimum cross‑segment station distance <= `max(tolA, tolB)` where each `tol` is the mean nearest‑neighbor distance inside that segment (segments with 0–1 station use `+inf`). If no feasible move exists (after limited restarts), the iteration is **retried** without advancing counters or writing CSV. After a successful station chain, a quick **port swap/merge** may be tested; it is kept only if it shortens the stitched pair (merge also requires combined load <= capacity).
- **Port cleanup**: after a mutation chain, optionally move a station across a boundary if it **shortens the port-to-first/last station leg** and still respects capacity (a quick local fix for long deadhead legs).
- **Merge refinement**: after a mutation chain, greedily remove adjacent ports when the **combined load** stays within capacity and the merged segment **shortens total distance** (evaluated via segment solves). This reduces segment count when possible without breaking the capacity rule.
- **Boundary refinement (debug pass)**: using the current optimized tour order, iterate over adjacent segment pairs and re‑position the boundary port to minimize the **sum of the two segment distances**, subject to capacity. This sweep repeats for a few iterations or until no improvement, and prints each accepted boundary change.
- **Evaluation & selection**: re-solve each segment TSP, stitch the tour, and accept the offspring only if it improves. Each iteration applies a chain of up to `mutations` station moves (stopping early with probability `1 - mut_prob`).

For reproducibility, record solver versions and any run flags (e.g., time limits) in your experiment notes.

## Notes
- `sol/` is intended for generated outputs; keep large artifacts only when needed for paper figures.
- The plotting scripts rely on `py/survey_utils.py` and may recompute distance matrices if requested.
