`dat`
=====

This folder holds the core input files, the generated SQLite database, and the static country plot
used by the current `src/` workflow.

Files
-----

- `gsp_data.db`
  Working database generated and consumed by the data preparation pipeline.

- `island.tsv`
  Coastline source file used by the country bootstrap.

- `waypoints.dat`
  Manual `WAYP` points in DAT degmin format.

- `ports.dat`
  Dedicated `PORT` input file.

- `boats.dat`
  Dedicated `BOAT` input file.

- `stations.dat`
  Dedicated `STAT` input file.

- `survey2023spring.dat`
  Historical survey assignment input used by the survey import/export path.

- `coastline_waypoints_ports.png`
  Static plot of the coastline, coarse coastline ring, buffered coastline support points, manual
  `WAYP` points, and ports.

- `survey_overview.png`
  Survey overview plot showing coastline, ports, boats, and trawl stations.

Current Workflow
----------------

1. Build the routing data database:

```bash
make -C src prepare-routing-data
```

This populates `dat/gsp_data.db` from:

- `dat/island.tsv`
- `dat/waypoints.dat`
- `dat/ports.dat`
- `dat/boats.dat`
- `dat/stations.dat`

2. Import/export the historical survey view if needed:

```bash
make -C src survey
```

3. Generate plots:

```bash
make -C src plot
```

That runs:

- `plot-country`
  writes `dat/coastline_waypoints_ports.png`
- `plot-overview`
  writes `dat/survey_overview.png`
- `plot-routes`
  writes route plots alongside solution JSON files under `sol/`

Notes
-----

- DAT coordinates use degmin storage: `DDMM.mm * 100`
- Longitudes in the DAT files are stored as positive west values


Data 
-----
![Coastline, coarse coastline ring, buffered coastline support points, manual WAYP points, and ports](coastline_waypoints_ports.png)
![Existing survey overview plot](survey_overview.png)
