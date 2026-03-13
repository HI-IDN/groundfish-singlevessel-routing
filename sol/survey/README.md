# Historical Survey Outputs

This folder contains the exported historical survey routes:

- `boat1.json` to `boat4.json`
- `boat1.png` to `boat4.png`

The original ordered survey input is:

- `dat/survey2023spring.dat`

The database used to resolve `boat_id`, `station_id`, and `port_id` is:

- `dat/gsp_data.db`

The JSON files here are produced by:

```bash
make -C src-refactor survey
```

That workflow:

1. reads `dat/survey2023spring.dat`
2. rebuilds the `survey` table from existing boats, stations, and ports already in `dat/gsp_data.db`
3. exports `boat*.json`

To regenerate a PNG from one of the JSON files:

```bash
Rscript R/plot_survey_route.R sol/survey/boat1.json
```

Examples:

```bash
Rscript R/plot_survey_route.R sol/survey/boat2.json
Rscript R/plot_survey_route.R sol/survey/boat3.json
Rscript R/plot_survey_route.R sol/survey/boat4.json
```

The corresponding PNG is written next to the JSON file, for example:

- `sol/survey/boat1.png`

-----

## Summary

| Boat ID   | Boat              | Capacity | Total Distance (nm) | Segments | Total Catch | Feasible  |
|-----------|-------------------|---------:|--------------------:|---------:|------------:|-----------|
| 1         | Bjarni Sæmundsson |    14000 |             2894.72 |        4 |       63881 | false[^1] |
| 2         | Árni Friðriksson  |    45000 |             4117.49 |        6 |           — | true      |
| 3         | Gullver           |   200000 |             3374.33 |        2 |           — | true      |
| 4         | Breki             |   200000 |             3355.02 |        2 |           — | true      |
| **Total** |                   |          |        **13741.56** |       14 |   **63881** |           |

[^1]: Capacity violations: segment 1 and segment 3 exceed capacity `14000`.

## Route Plotting

![boat1.png](boat1.png)
![boat2.png](boat2.png)
![boat3.png](boat3.png)
![boat4.png](boat4.png)
