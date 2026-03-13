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

-----

## Route Plotting

![boat1.png](boat1.png)
![boat2.png](boat2.png)
![boat3.png](boat3.png)
![boat4.png](boat4.png)

## Segment Summary

### Boat 1: Bjarni Sæmundsson

|   Segment |  Length | Stations |     Catch | Distance (nm) |
|----------:|--------:|---------:|----------:|--------------:|
|         1 |      38 |       36 |     24176 |        728.22 |
|         2 |      20 |       18 |     10744 |        405.93 |
|         3 |      44 |       42 |     15896 |       1178.30 |
|         4 |      17 |       15 |     13065 |        582.27 |
| **Total** | **119** |  **111** | **63881** |   **2894.72** |

### Boat 2: Árni Friðriksson

|   Segment |  Length | Stations |      Catch | Distance (nm) |
|----------:|--------:|---------:|-----------:|--------------:|
|         1 |      22 |       20 |      36573 |        711.41 |
|         2 |       5 |        3 |        199 |         74.46 |
|         3 |      49 |       47 |      36889 |        848.20 |
|         4 |      50 |       48 |      35658 |       1009.09 |
|         5 |      43 |       41 |      31211 |       1063.20 |
|         6 |       7 |        5 |       3585 |        411.13 |
| **Total** | **176** |  **164** | **144115** |   **4117.49** |

### Boat 3: Gullver

|   Segment |  Length | Stations |     Catch | Distance (nm) |
|----------:|--------:|---------:|----------:|--------------:|
|         1 |      73 |       71 |     20040 |       1544.99 |
|         2 |      82 |       80 |     52715 |       1829.34 |
| **Total** | **155** |  **151** | **72755** |   **3374.33** |

### Boat 4: Breki

|   Segment |  Length | Stations |      Catch | Distance (nm) |
|----------:|--------:|---------:|-----------:|--------------:|
|         1 |      89 |       87 |     147573 |       1864.47 |
|         2 |      68 |       67 |      99741 |       1490.56 |
| **Total** | **157** |  **154** | **247314** |   **3355.02** |

### Fleet Summary

|   Boat ID | Boat              | Segments |   Nodes | Stations | Total Catch | Distance (nm) |
|----------:|-------------------|---------:|--------:|---------:|------------:|--------------:|
|         1 | Bjarni Sæmundsson |        4 |     119 |      111 |       63881 |       2894.72 |
|         2 | Árni Friðriksson  |        6 |     176 |      164 |      144115 |       4117.49 |
|         3 | Gullver           |        2 |     155 |      151 |       72755 |       3374.33 |
|         4 | Breki             |        2 |     157 |      154 |      247314 |       3355.02 |
| **Total** |                   |   **14** | **607** |  **580** |  **528065** |  **13741.56** |