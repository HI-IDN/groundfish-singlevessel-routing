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
make -C src survey
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

## Route Plotting

![boat1.png](boat1.png)
![boat2.png](boat2.png)
![boat3.png](boat3.png)
![boat4.png](boat4.png)

## Segment Summary

### Boat 1: Bjarni Sæmundsson

|   Segment |  Length | Stations |     Catch | Distance (nm) |
|----------:|--------:|---------:|----------:|--------------:|
|         1 |      38 |       36 |     24176 |        392.96 |
|         2 |      20 |       18 |     10744 |        218.42 |
|         3 |      44 |       42 |     15896 |        633.66 |
|         4 |      17 |       15 |     13065 |        310.19 |
| **Total** | **119** |  **111** | **63881** |   **1555.23** |

### Boat 2: Árni Friðriksson

|   Segment |  Length | Stations |      Catch | Distance (nm) |
|----------:|--------:|---------:|-----------:|--------------:|
|         1 |      22 |       20 |      36573 |        384.70 |
|         2 |       5 |        3 |        199 |         40.18 |
|         3 |      49 |       47 |      36889 |        458.88 |
|         4 |      50 |       48 |      35658 |        541.47 |
|         5 |      43 |       41 |      31211 |        571.49 |
|         6 |       7 |        5 |       3585 |        222.24 |
| **Total** | **176** |  **164** | **144115** |   **2218.98** |

### Boat 3: Gullver

|   Segment |  Length | Stations |     Catch | Distance (nm) |
|----------:|--------:|---------:|----------:|--------------:|
|         1 |      73 |       71 |     20040 |        833.70 |
|         2 |      82 |       80 |     52715 |        982.03 |
| **Total** | **155** |  **151** | **72755** |   **1815.73** |

### Boat 4: Breki

|   Segment |  Length | Stations |      Catch | Distance (nm) |
|----------:|--------:|---------:|-----------:|--------------:|
|         1 |      89 |       87 |     147573 |       1005.21 |
|         2 |      68 |       67 |      99741 |        803.28 |
| **Total** | **157** |  **154** | **247314** |   **1808.48** |

### Fleet Summary

|    ID     | Boat              | Segments |   Nodes | Stations | Total Catch | Distance (nm) | Feasible  |
|:---------:|-------------------|---------:|--------:|---------:|------------:|--------------:|-----------|
|     1     | Bjarni Sæmundsson |        4 |     119 |      111 |       63881 |       1555.23 | false[^1] |
|     2     | Árni Friðriksson  |        6 |     176 |      164 |      144115 |       2218.98 | true      |
|     3     | Gullver           |        2 |     155 |      151 |       72755 |       1818.73 | true      |
|     4     | Breki             |        2 |     157 |      154 |      247314 |       1808.48 | true      |
| **Total** |                   |   **14** | **607** |  **580** |  **528065** |   **7401.42** |           |

[^1]: Capacity violations: segment 1 and segment 3 exceed capacity `14000`.

-----
