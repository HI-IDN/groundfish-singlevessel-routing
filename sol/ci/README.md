# Cheapest‑Insertion (CI) Construction

The Cheapest‑Insertion (CI) construction builds an initial ordering by iteratively inserting
the unvisited station whose inclusion causes the smallest marginal increase in the total tour
length. At each iteration, the algorithm evaluates all possible insertion positions in the current
partial tour and selects the station–position pair that yields the minimal additional travel
distance.

Compared with nearest‑neighbor, CI invests slightly more computation per step in order to build
smoother, more globally consistent orderings while remaining lightweight and scalable. When a
prospective insertion would violate vessel capacity in the current segment, the algorithm inserts a
port, resets accumulated load, and then continues inserting stations using the same
marginal‑increase rule.

Overall, CI strikes a balance between computational efficiency and route quality: it tends to
produce higher‑quality initial orderings than NN while maintaining low overhead, making it a strong
default construction for downstream segmenting and refinement.

-----

## Route Plotting

<table><tr>
<td align="center"><img src="construction.png" width="300"/><br><sub>Construction — transit 4,848.69 nm · total 7,091.54 nm (capacity infeasible)</sub></td>
<td align="center"><img src="segment.png" width="300"/><br><sub>Segment — transit 5,172.83 nm · total 7,415.68 nm</sub></td>
<td align="center"><img src="refinement.png" width="300"/><br><sub>Refinement (matheuristic sweep, pass 3) — transit 5,047.77 nm · total 7,290.62 nm</sub></td>
</tr></table>

## Segment Summary

### Construction (capacity infeasible)

|   Segment |   Length | Stations |      Catch | Transit (nm) |   Total (nm) |
|----------:|---------:|---------:|-----------:|-------------:|-------------:|
|         1 |     1160 |      580 |     528065 |     4,848.69 |     7,091.54 |
| **Total** | **1160** |  **580** | **528065** | **4,848.69** | **7,091.54** |

### Segment (capacity feasible)

|   Segment |   Length | Stations |      Catch | Transit (nm) |   Total (nm) |
|----------:|---------:|---------:|-----------:|-------------:|-------------:|
|         1 |       71 |       35 |      43347 |       311.30 |       441.72 |
|         2 |      127 |       63 |      43548 |       465.39 |       702.21 |
|         3 |      177 |       88 |      43825 |       786.74 |     1,131.01 |
|         4 |       65 |       32 |      38138 |       333.76 |       456.85 |
|         5 |      115 |       57 |      44921 |       515.45 |       742.64 |
|         6 |      155 |       77 |      44170 |       621.54 |       919.70 |
|         7 |      163 |       81 |      44751 |       450.60 |       767.22 |
|         8 |       89 |       44 |      39689 |       436.20 |       595.57 |
|         9 |        9 |        4 |      41179 |       214.21 |       230.12 |
|        10 |       49 |       24 |      44762 |       237.02 |       333.40 |
|        11 |       49 |       24 |      44053 |       207.67 |       304.67 |
|        12 |       89 |       44 |      44836 |       378.04 |       548.56 |
|        13 |       14 |        7 |      10846 |       214.92 |       242.00 |
| **Total** | **1172** |  **580** | **528065** | **5,172.83** | **7,415.68** |

### Refinement (matheuristic sweep, pass 3, capacity feasible)

|   Segment |  Length | Stations |      Catch | Transit (nm) |   Total (nm) |
|----------:|--------:|---------:|-----------:|-------------:|-------------:|
|         1 |      32 |       31 |      42512 |       298.25 |       417.69 |
|         2 |      69 |       68 |      44904 |       472.28 |       724.51 |
|         3 |      89 |       88 |      44835 |       784.89 |     1,129.04 |
|         4 |      31 |       30 |      35971 |       318.81 |       434.08 |
|         5 |      57 |       56 |      44429 |       512.74 |       736.09 |
|         6 |      79 |       78 |      44662 |       621.77 |       923.77 |
|         7 |      81 |       80 |      44665 |       447.73 |       761.37 |
|         8 |      44 |       43 |      38817 |       431.36 |       586.99 |
|         9 |       7 |        6 |      42137 |       215.97 |       238.60 |
|        10 |      25 |       24 |      44762 |       237.02 |       333.40 |
|        11 |      25 |       24 |      44053 |       207.67 |       304.67 |
|        12 |      15 |       14 |      18766 |       137.92 |       194.68 |
|        13 |      38 |       38 |      37552 |       361.38 |       505.72 |
| **Total** |  **592** |  **580** | **528065** | **5,047.77** | **7,290.62** |
