# Historical Survey Outputs

This folder contains the exported historical survey routes:

- `boat1.json` to `boat4.json`
- `boat1.png` to `boat4.png`
- `boat1.log` to `boat4.log`
- `multivessel.png`
- `multivessel.log`

The original ordered survey input is:

- `dat/survey2023spring.dat`

The database used to resolve `boat_id`, `station_id`, and `port_id` is:

- `dat/gsp.db`

The JSON files here are produced by:

```bash
make -C src survey
```

That workflow:

1. reads `dat/survey2023spring.dat`
2. rebuilds the `survey` table from existing boats, stations, and ports already in `dat/gsp.db`
3. exports `boat*.json`

To regenerate a PNG from one of the JSON files:

```bash
Rscript R/plot_single_vessel_route.R sol/survey/boat1.json
```

That command also writes a sidecar summary log next to the PNG.

Examples:

```bash
Rscript R/plot_single_vessel_route.R sol/survey/boat2.json
Rscript R/plot_single_vessel_route.R sol/survey/boat3.json
Rscript R/plot_single_vessel_route.R sol/survey/boat4.json
Rscript R/plot_multi_vessel_route.R "sol/survey/boat*.json" sol/survey/multivessel.png dat/gsp.db
```

The corresponding PNG is written next to the JSON file, for example:

- `sol/survey/boat1.png`
- `sol/survey/boat1.log`

The combined survey plot writes:

- `sol/survey/multivessel.png`
- `sol/survey/multivessel.log`

-----

## Route Plotting

![boat1.png](boat1.png)
![boat2.png](boat2.png)
![boat3.png](boat3.png)
![boat4.png](boat4.png)

## Segment Summary

### Boat 1: Bjarni Sæmundsson

|   Segment |  Length | Stations |     Catch | Transit (nm) | Total (nm) |
|----------:|--------:|---------:|----------:|-------------:|-----------:|
|         1 |      38 |       36 |     24176 |       444.57 |     585.60 |
|         2 |      20 |       18 |     10744 |       147.05 |     218.42 |
|         3 |      44 |       42 |     15896 |       480.17 |     633.66 |
|         4 |      17 |       15 |     13065 |       257.82 |     310.19 |
| **Total** | **119** |  **111** | **63881** |  **1329.60** | **1747.87** |

### Boat 2: Árni Friðriksson

|   Segment |  Length | Stations |      Catch | Transit (nm) | Total (nm) |
|----------:|--------:|---------:|-----------:|-------------:|-----------:|
|         1 |      22 |       20 |      36573 |       305.72 |     384.70 |
|         2 |       5 |        3 |        199 |        32.71 |      40.18 |
|         3 |      49 |       47 |      36889 |       273.85 |     458.88 |
|         4 |      50 |       48 |      35658 |       356.36 |     541.47 |
|         5 |      43 |       41 |      31211 |       416.85 |     571.49 |
|         6 |       7 |        5 |       3585 |       206.38 |     222.24 |
| **Total** | **176** |  **164** | **144115** |  **1591.86** | **2218.98** |

### Boat 3: Gullver

|   Segment |  Length | Stations |     Catch | Transit (nm) | Total (nm) |
|----------:|--------:|---------:|----------:|-------------:|-----------:|
|         1 |      73 |       71 |     20040 |       551.55 |     833.70 |
|         2 |      82 |       80 |     52715 |       672.02 |     982.03 |
| **Total** | **155** |  **151** | **72755** |  **1223.57** | **1815.73** |

### Boat 4: Breki

|   Segment |  Length | Stations |      Catch | Transit (nm) | Total (nm) |
|----------:|--------:|---------:|-----------:|-------------:|-----------:|
|         1 |      89 |       87 |     147573 |       661.38 |    1005.21 |
|         2 |      68 |       67 |      99741 |       541.80 |     803.28 |
| **Total** | **157** |  **154** | **247314** |  **1203.17** | **1808.48** |

### Fleet Summary

This table is taken from `multivessel.log`.

|    ID     | Boat              | Segments |   Nodes | Stations | Total Catch | Transit (nm) | Total (nm) | Feasible  |
|:---------:|-------------------|---------:|--------:|---------:|------------:|-------------:|-----------:|-----------|
|     1     | Bjarni Sæmundsson |        4 |     120 |      111 |       63881 |      1329.60 |    1747.87 | false[^1] |
|     2     | Árni Friðriksson  |        6 |     177 |      164 |      144115 |      1591.86 |    2218.98 | true      |
|     3     | Gullver           |        2 |     156 |      151 |       72755 |      1223.57 |    1815.73 | true      |
|     4     | Breki             |        2 |     157 |      154 |      247314 |      1203.17 |    1808.48 | true      |
| **Total** |                   |   **14** | **610** |  **580** |  **528065** |  **5348.20** | **7591.06** |           |

[^1]: The exported survey route is retained for plotting even though at least one boat exceeds capacity.

![Combined Route](multivessel.png)

-----
