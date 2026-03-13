dat Directory
=============

This folder contains the input `.dat` files, the generated SQLite database, and preview plots used by the refactored workflow in `src-refactor/`.

Files
-----

- `gsp_data.db`
  Current working database used by `gsp_country` and the plotting scripts.

- `island.bin`
  Coastline source file used by `gsp_country` via `--coastline-file`.

- `waypoints.dat`
  Manual `WAYP` seed file. Each line uses DAT degmin storage, for example:
  `WAYP 660004 182318`

- `ports.dat`
  Dedicated `PORT` input file.

- `boats.dat`
  Dedicated `BOAT` input file.

- `stations.dat`
  Dedicated `STAT` input file.

- `data2023spring.dat`
  Legacy combined survey DAT file kept for reference.

- `coastline_waypoints_ports.png`
  Preview image generated from `gsp_data.db`. This is the main static reference plot for coastline, inferred waypoints, ports, and boat start/end locations.

Main Workflow
-------------

1. Build and run the country bootstrap:

```bash
make -C src-refactor country
```

This populates `dat/gsp_data.db` using:
- `dat/island.bin`
- `dat/waypoints.dat`
- `dat/ports.dat`
- `dat/boats.dat`

2. Generate the static preview plot:

```bash
Rscript R/plot_country.R dat/gsp_data.db dat/coastline_waypoints_ports.png
```

3. For interactive waypoint picking, use:

```bash
Rscript R/click_country_points.R
```

Notes
-----

- The DAT coordinate format is not projected meters. It uses degmin storage:
  `DDMM.mm * 100`
- Longitude values in the DAT files are stored as positive west values.
- The helper tool and parser now use that same encoding when generating `WAYP easting northing` lines.
