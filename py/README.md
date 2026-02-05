Python utilities for plotting and summarizing matheuristic runs
===============================================================

This folder contains small Python scripts used to analyze and visualize solutions produced by the C binaries in `src/` (the matheuristic runs). The main files are:

- `plot_solution.py`  — read a C "plot bundle" (solution plot text) and draw the route on a map (optionally save PNG or export TikZ).
- `survey_utils.py`   — helper functions used by `plot_solution.py` (distance matrix helpers, drawing utilities, and a ctypes wrapper for a small native helper library).
- `cap_stats.py`      — summarize capacity‑MIP runs (cap_*.csv / capmut_*.csv) and emit LaTeX tables and time summaries.
- `requirements.txt`  — requirements for the Python scripts (numpy + matplotlib).

This README explains what each script does, runtime requirements, and shows copy/paste examples for common workflows.

Requirements
------------
- Python 3.8+ (the repository uses plain stdlib + numpy + matplotlib).
- numpy and matplotlib must be installed (pip):

```bash
python -m pip install -r requirements.txt
```

- The plotting code can optionally call a small native helper library (`lib/utils.*`):
  - On Windows the module expects `lib/utils.dll` or `lib/libutils.dll` in the repository `lib/` folder.
  - On macOS it looks for `lib/libutils.dylib` and on Linux `lib/libutils.so`.
  - If the shared library is missing you can still use `plot_solution.py` in many cases, but the `DistanceLink` functionality will attempt to use the compiled helper; ensure `lib/` is populated by running `make -C src utils`.

- `plot_solution.py` uses matplotlib. When running in a headless environment or when writing images use the `--save` option; the script sets a local `MPLCONFIGDIR` inside the working tree if not present so matplotlib can write its runtime config files (this avoids permission problems writing to your home directory).

Files and behavior
------------------

plot_solution.py
    - Purpose: read a C plot bundle file (created by the C binaries) and render the route.
    - Input: a `plot bundle` text file created by the C code (the `--write-dat` output contains a plot bundle; examples in `sol/` include `capmut_60.txt` etc.).
    - Features:
      - Compute or reuse stored distance / feasible matrices (recompute with `--recompute`).
      - Print per‑segment summaries (stations, distance, amount).
      - Export a PNG (with `--save`) or show interactively.
      - Export TikZ code (`--tikz` / `--tikz-out`).
      - Print edge-by-edge distances (`--edges` / `--edges-full`).
      - Optional legend labels based on ship capacity (`--ship-cap`) or always show legend with `--legend`.

## Example usages:

```bash
# render a solution and show it (interactive)
python py/plot_solution.py --sol sol/capmut_60.txt

# render and save to a PNG (headless)
python py/plot_solution.py --sol sol/capmut_60.txt --save figs/capmut_60.png

# recompute the full distance matrix (may be slow) before plotting and save
python py/plot_solution.py --sol sol/capmut_60.txt --recompute --save figs/capmut_60_recomputed.png

# print an edge-by-edge summary instead of drawing
python py/plot_solution.py --sol sol/capmut_60.txt --edges

# export TikZ to file
python py/plot_solution.py --sol sol/capmut_60.txt --tikz-out figs/capmut_60.tikz
```

Notes about matplotlib config
----------------------------
`plot_solution.py` uses a local `.mplconfig` directory (created in the current working directory) if `MPLCONFIGDIR` is not already set in the environment. This prevents matplotlib from trying to write config files to your home directory (useful on shared systems or in CI).

survey_utils.py
---------------
- Purpose: low‑level utilities used by `plot_solution.py`:
  - `DistanceLink(...)` — computes distance and feasible (waypoint-aware) matrices when a precomputed matrix is not present in the plot bundle.
  - `drawTour(...)` / `drawTour_tikz(...)` — higher‑level drawing helpers used to render the tour.
  - `Dijkstra_(...)` — small wrapper around a native dijkstra implementation (exposed through the C helper lib) used for rerouting when a direct link crosses land.
  - Coordinate conversion helpers (`degmin2rad`, `deg2point`, `arcdist`).

- Notes:
  - `survey_utils` tries to load a native shared library from `../lib` (relative to the repo). If that library is missing you will see a FileNotFoundError coming from ctypes (this is expected if you haven't built the `utils` library). Build it with:

```bash
make -C src utils
```

- Example quick usage (from Python REPL):

```python
from py.survey_utils import DistanceLink
# Suppose you already read a plot bundle and have Type, LatLonRad, StartEnd, Size, SelectedSize
DistMtrx, FsbleMtrx = DistanceLink(Type[:Size], LatLonRad, StartEnd, Size, SelectedSize)
```

cap_stats.py
------------
- Purpose: analyze `cap_*.csv` or `capmut_*.csv` summary CSV files produced by the matheuristic runs and produce a LaTeX table (and optional per‑pass tables).
- Behavior:
  - If you pass specific CSV files on the command line the script will analyze those; otherwise it scans `--sol-dir` (default `sol/`) for `cap_*.csv` or `capmut_*.csv` files using the `--base` value.
  - Produces a LaTeX table summarizing: baseline, final total distance, number of passes, average/median/max segment changes, capacity MIP solves and gap statistics, plus run time.
  - Supports `--per-pass` to emit a detailed per-pass table for each run.
  - `--time-summary` prints an estimate of the MIP upper bound vs actual run times.

- Example usage:

```bash
# Analyze all capmut_*.csv files in ./sol and print a LaTeX table to stdout
python py/cap_stats.py --base capmut --sol-dir sol

# Create a LaTeX file and include per-pass tables
python py/cap_stats.py --base capmut --sol-dir sol --per-pass --out capmut_summary.tex

# Print time estimate summary to stderr
python py/cap_stats.py --base capmut --sol-dir sol --time-summary

# Analyze specific CSV files
python py/cap_stats.py sol/capmut_60.csv sol/capmut_120.csv --out cap_table.tex
```

How these scripts fit together
-----------------------------
Typical workflow:

1. Run a matheuristic binary (e.g., `bin/matcapmutheur` or `bin/matcapmutheur_v3`) to produce `sol/*.dat`/`sol/*.txt` plot bundles and `sol/*.csv` run summaries.
2. Use `py/cap_stats.py` to aggregate the CSV results into LaTeX tables for the paper.
3. Use `py/plot_solution.py` to visualize interesting solutions and save PNG/TikZ for figures.

Troubleshooting
---------------
- If `plot_solution.py` fails with a ctypes FileNotFoundError pointing to `lib/utils.*`, build the helper library in `src/`:

```bash
make -C src utils
```

- If matplotlib fails to initialize, ensure you either run with `--save` (non-interactive) or have a working DISPLAY / GUI when invoking without `--save`. The script will create a `.mplconfig` subdirectory in the current working directory as needed.

- If `plot_solution.py --recompute` is very slow, you can compute the distance matrix once and reuse it (the C binaries sometimes include precomputed matrices in the plot bundle).
