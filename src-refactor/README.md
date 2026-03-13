Groundfish Survey Routing - Refactored Workflow
===============================================

Overview
--------

The current user-facing workflow is:

1. `make country`
2. `make stations`
3. `make distance`
4. `make survey`

This keeps the stages separate:
- `country` builds the coastline, waypoints, ports, and boats base database
- `stations` imports the historical survey stations from the legacy survey DAT file
- `distance` recomputes the distance matrix from the existing database
- `survey` exports the historical survey already stored in the database to JSON

Quick Start
-----------

From `src-refactor/`:

```bash
make country
make stations
make distance
make survey
```

Current Build Targets
---------------------

- `gsp_country`
  Coastline bootstrap tool. Reads:
  - `dat/island.bin`
  - `dat/waypoints.dat`
  - `dat/ports.dat`
  - `dat/boats.dat`

- `gsp_stations`
  Station importer using the new `DataSet` parser path.

- `gsp_stations_with_distance`
  Legacy combined importer/distance tool kept as backup while the split is completed.

- `historical_survey`
  Exports the historical survey from `gsp_data.db` to JSON files under `sol/`.

- `gsp`
  Solver executable for the init/sweep stages.

Workflow Details
----------------

### Step 1: Country bootstrap

```bash
make country
```

This:
- loads the coastline from `dat/island.bin`
- loads manual waypoint seeds from `dat/waypoints.dat`
- imports ports from `dat/ports.dat`
- imports boats from `dat/boats.dat`
- writes the result to `dat/gsp_data.db`

Static preview:

```bash
Rscript R/plot_country.R dat/gsp_data.db dat/coastline_waypoints_ports.png
```

Interactive waypoint helper:

```bash
Rscript R/click_country_points.R
```

### Step 2: Historical survey stations

```bash
make stations
```

This imports the stations file with `gsp_stations`:

```text
dat/stations.dat
```

The stage assumes `make country` has already populated the database with coastline, waypoints, ports, and boats.

### Step 3: Distance matrix

```bash
make distance
```

This recomputes the distance matrix from the existing database after station import.

### Step 4: Historical survey export

```bash
make survey
```

This exports the historical survey already present in `dat/gsp_data.db` to JSON files under `sol/`.

Compatibility Aliases
---------------------

- `make stations-with-distance`
  Runs both `make stations` and `make distance`

- `make preprocessing`
  Alias for `make stations-with-distance`

- `make export_survey`
  Alias for `make survey`

Notes
-----

- `gsp_stations_with_distance` still exists as a backup path for the old combined implementation in `preprocessing.c`.
- `gsp_stations` is the new station-import stage for `dat/stations.dat` and does not use `ItemVec`.
- The historical survey step for `dat/survey2023spring.dat` still needs its own dedicated import/export path.
- The public workflow names are now based on the actual stages above.
