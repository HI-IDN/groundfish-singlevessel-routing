# GSP Tools

Utility tools for the refactored workflow.

## gsp_country

Bootstrap `gsp_data.db` with coastline, inferred waypoints, ports, and boats.
Implementation source: `tools/infer_waypoints_from_coastline.c`.

### Usage

```bash
gsp_country --db dat/gsp_data.db --coastline-file dat/island.bin [options]
```

### Common options

- `--waypoint-file dat/waypoints.dat`
- `--port-file dat/ports.dat`
- `--boat-file dat/boats.dat`
- `--preserve-all-seeds`
- `--seed-hints-only`
- `--min-points N --max-points N --target-points N`

### Static preview

```bash
Rscript R/plot_country.R dat/gsp_data.db dat/coastline_waypoints_ports.png
```

### Interactive waypoint picking

```bash
Rscript R/click_country_points.R
```

## gsp_stations

Import stations into the existing database using the new `DataSet` parser path.
Implementation source: `common/station_import.c`.

### Usage

```bash
gsp_stations dat/stations.dat dat/gsp_data.db
```

This stage assumes `gsp_country` has already populated the database with coastline, waypoints, ports, and boats.

## gsp_distances

Recompute distances from the existing database.
Implementation source: `common/distance_builder.c`.

### Usage

```bash
gsp_distances --db dat/gsp_data.db
```

This stage uses waypoint nodes for routing, but writes only rows whose endpoints are non-waypoint locations.

## gsp_stations_with_distance

Legacy combined importer/distance tool kept as backup during the split away from `preprocessing.c`.
Implementation source: `common/preprocessing.c`.

## historical_survey

Export the historical survey already stored in `gsp_data.db` to JSON for plotting and inspection.
Implementation source: `tools/export_survey_json.c`.

### Usage

```bash
historical_survey <database.db> <output_prefix> [boat_id]
```

### Examples

```bash
./historical_survey dat/gsp_data.db sol/survey
./historical_survey dat/gsp_data.db sol/survey 2
```

## Workflow

```bash
make country
make stations
make distance
make survey
```
