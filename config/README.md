CONFIG_README
=============

Purpose
-------
This repository-level configuration file (`config.yaml`) centralizes parameters, path
templates and variant-specific settings used by `scripts/` and binaries under `src/`.
The goal is to remove hard-coded values from shell scripts and Makefiles and have a
single authoritative source of truth.

Location
--------
- `experiments.yaml` (root `config/`) — main config storing global and per-variant settings.

Key fields explained
--------------------
- `global.repo_root`: top-level repo path. Useful when resolving relative paths.
- `global.code_dir`: where C sources live (usually `src`).
- `global.dat_dir`: directory containing input `.dat` files.
- `global.bin_dir`: directory for compiled binaries.
- `global.cap_time_limits`: list of capacity timelimits used in experiment runs.
- `variants.<name>.binary`: path to the executable for that variant (relative to repo root).
- `scripts.run_template`: command template showing how scripts should invoke binaries.

How to use (next steps)
-----------------------
1. Update `scripts/Makefile` and `scripts/make_matrices.sh` to read `config.yaml`
   (for example with `python -c 'import yaml,sys;print(yaml.safe_load(open("config.yaml"))...)'`)
   or use a small shell helper that extracts keys with `yq`/`python`.

2. Replace hard-coded values (like time limits, dataset paths, log paths) with
   template substitutions that pull values from the config.

3. When adding a new variant, add an entry under `variants` with `binary` and
   other per-variant customizations.