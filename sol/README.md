This README explains the outputs in this folder, how they were produced, and how to combine and analyse them.

Purpose
-------
The `sol/` folder contains solution outputs produced by different runs of the capacity matheuristic experiments.
This document tells you what each file type is, how to reproduce the results, and how to create a combined experiment database for easier analysis.

Quick layout
------------
- `sol/*.txt`  — human-readable run logs and verbose output captured during runs.
- `sol/*.dat`  — data dump files produced by the solver.
- `sol/*.csv`  — tabular solution outputs (CSV) that are intended for programmatic analysis.

File naming conventions
----------------------
Filenames follow clear variants and parameter choices. Examples you will see in this folder:
- `capmut_<T>.*`        — original matheuristic runs (where `<T>` is capacity/time parameter)
- `capmut_v3_<T>.*`     — V3 variant runs
- `capmut_v3_cheapest_<T>.*` — V3 runs using the `cheapest` initialization strategy

Reproducing experiments (high-level)
-----------------------------------
The C sources live in `src/` and are built via the `Makefile` there. The usual workflow from the repository root is:

1) Build the specific binary you want (from repo root):

```bash
# build the original matcapmutheur binary
make -C src matcapmutheur

# build the V3 variant
make -C src matcapmutheur_v3
```

2) Run the corresponding script or command to produce files in `sol/`.
There are convenience scripts in `` (e.g. `script_capmut.sh`, `script_capmut_v3.sh`) that call the compiled binaries with standard parameters.

Example reproduction commands
-----------------------------
- Original run (example):
```bash
make -C src matcapmutheur
bash script_capmut.sh
```

- V3 run (example):
```bash
make -C src matcapmutheur_v3
bash script_capmut_v3.sh
```

- Single-case V3 with `cheapest` init (example):
```bash
make -C src matcapmutheur_v3
./bin/matcapmutheur_v3 dat/singleboatdata2023spring.dat 1 --cap-time-limit 60 --write-dat sol/capmut_v3_cheapest_60.dat --init-strategy cheapest --verbose-init > sol/capmut_v3_cheapest_60.txt
```

Combined experiment database
----------------------------
To make cross-run analysis easier the repository includes a small importer that groups paired files `X.cap.csv` and `X.csv` by prefix `X`, upserts a `configurations` row for each prefix, and imports data into three tables.

- Output DB path: `dat/solutions.sqlite`.

What it contains
----------------
- `configurations` — one row per file-prefix (X). Columns:
  - `id` INTEGER PRIMARY KEY
  - `file_prefix` TEXT UNIQUE (the common prefix X)
  - `file_summary` TEXT UNIQUE (path to `X.cap.csv`)
  - `file_extra` TEXT UNIQUE (path to `X.csv`)
  - `L2seg` INTEGER — parsed numeric suffix (extracted from the filename digits)
  - `initialization` TEXT — parsed initialization method (`NP` default, `CI` for "cheapest", `GE` for greedy edge variants)
  - `version` INTEGER — parsed from `capmut_v<number>` when present (e.g., `capmut_v3` → 3)

- `summary` — rows imported from `X.cap.csv` files; columns come from the header of the `.cap.csv` files and are stored as TEXT. Each row references `configurations.id` via `config_id`.

- `extra` — rows imported from `X.csv` (the aggregated/solution CSVs). Columns come from the headers and are stored as TEXT; each row references `configurations.id` via `config_id`.

- `logs` — parsed `.txt` logs (if present) linked to `configurations.id`. This table stores parsed metadata such as `ship`, `shipcap`, `initial_noport`, progress fields, `best_obj`, `best_bound`, `gap`, `seed`, and also the raw text in a `raw` column for full inspection.

Build & create the combined DB
------------------------------
1) Build the importer tool:

```bash
make -C src sols_to_sqlite
# produces: bin/sols_to_sqlite
```

2) Run the convenience make target to combine all CSVs in `sol/`:

```bash
make -C src sols_db
```
This runs the importer over `sol/*.csv` and writes `dat/solutions.sqlite`.

3) Recreate from scratch (recommended to avoid duplicates):

```bash
rm -f dat/solutions.sqlite
make -C src sols_db
```

Or run the binary directly (after building) to combine a specific set of CSVs:

```bash
bin/sols_to_sqlite sol/*.csv dat/solutions.sqlite
```

Caveats / assumptions
- The importer groups files by prefix `X` (expects `X.cap.csv` and/or `X.csv`).
- Column names in `summary` and `extra` are sanitized and stored as TEXT. If you want numeric typing, use the post-import CAST approach or ask for automatic type detection.
- The .txt log parser extracts commonly useful fields but also stores full `raw` content for complete inspection.
- Re-running the importer without deleting the DB will append rows; delete the DB first for a fresh import.

Inspecting and querying the DB
-----------------------------
Some useful sqlite commands (from repo root):

```bash
# list tables
sqlite3 dat/solutions.sqlite ".tables"

# show schema
sqlite3 dat/solutions.sqlite "PRAGMA table_info(solutions);"

# preview rows
sqlite3 -column -header dat/solutions.sqlite "SELECT file, * FROM solutions LIMIT 10;"
```

Caveats and assumptions
-----------------------
- The importer uses the header line of the first CSV to create columns. If other CSVs have different headers, missing columns are inserted as NULL.
- CSV parsing in the current importer is simple (comma-splitting, trimming, and handling simple quoted tokens). It does not support fields with embedded newlines inside quotes.
- Re-running the importer without removing the DB will append rows and may create duplicates; delete the DB first if you want a fresh import.