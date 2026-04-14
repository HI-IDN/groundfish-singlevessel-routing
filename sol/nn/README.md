# Nearest-Neighbor (NN) Construction

The Nearest-Neighbor (NN) construction builds an initial ordering of the survey stations by
repeatedly selecting, from the set of unvisited stations, the closest feasible next station
according to the waypoint-aware distance matrix. At each step, the algorithm evaluates whether
adding the next station would exceed the vessel's remaining capacity within the current segment. If
capacity would be violated, a port is inserted, the load is reset, and the process continues from
that port.

Because NN expands the route by always choosing the locally nearest feasible option, it is fast to
compute and scales easily to large station sets. However, its purely greedy, distance-driven
behavior can lead to noisy or irregular segmentations, especially in regions with clustered
stations or sharp load gradients. Despite this, NN provides a lightweight deterministic baseline
that is useful for benchmarking more sophisticated construction strategies.

-----

## Route Plotting

<table><tr>
<td align="center"><img src="init.png" width="460"/><br><sub>Segment — transit 6,264.57 nm · total 8,507.42 nm</sub></td>
<td align="center"><img src="sweep.png" width="460"/><br><sub>Refinement (matheuristic sweep, pass 4) — transit 5,932.39 nm · total 8,175.24 nm</sub></td>
</tr></table>

## Segment Summary

### Segment (capacity feasible)

|   Segment |   Length | Stations |      Catch | Transit (nm) | Distance (nm) |
|----------:|---------:|---------:|-----------:|-------------:|--------------:|
|         1 |       97 |       48 |      43996 |       348.46 |        522.48 |
|         2 |      123 |       61 |      43989 |       335.94 |        572.59 |
|         3 |      165 |       82 |      43992 |       609.21 |        923.12 |
|         4 |      219 |      109 |      43998 |       676.95 |       1101.59 |
|         5 |       81 |       40 |      43989 |       520.65 |        679.37 |
|         6 |       77 |       38 |      44000 |       824.61 |        975.95 |
|         7 |      103 |       51 |      43988 |       371.53 |        566.19 |
|         8 |       73 |       36 |      43965 |       303.81 |        440.49 |
|         9 |       81 |       40 |      43958 |       663.88 |        822.84 |
|        10 |       95 |       47 |      43654 |       728.37 |        909.27 |
|        11 |       49 |       24 |      43466 |       421.81 |        518.03 |
|        12 |        7 |        3 |      37074 |       283.48 |        295.48 |
|        13 |        2 |        1 |       7996 |       175.86 |        180.02 |
| **Total** | **1172** |  **580** | **528065** |  **6264.57** |   **8507.42** |

### Refinement (matheuristic sweep, capacity feasible)

|   Segment |  Length | Stations |      Catch | Transit (nm) |  Total (nm) |
|----------:|--------:|---------:|-----------:|-------------:|------------:|
|         1 |      55 |       54 |      43517 |       317.94 |      517.73 |
|         2 |      73 |       72 |      44379 |       425.85 |      695.30 |
|         3 |      64 |       63 |      37482 |       459.45 |      706.68 |
|         4 |     108 |      107 |      43429 |       672.83 |     1091.45 |
|         5 |      44 |       43 |      43855 |       520.80 |      689.41 |
|         6 |      37 |       36 |      44643 |       733.33 |      876.79 |
|         7 |      53 |       52 |      44048 |       395.96 |      594.62 |
|         8 |      36 |       35 |      43624 |       304.49 |      437.29 |
|         9 |      43 |       42 |      44411 |       656.79 |      823.61 |
|        10 |      46 |       45 |      42818 |       689.36 |      862.34 |
|        11 |      24 |       23 |      40365 |       351.00 |      443.17 |
|        12 |       4 |        3 |      12472 |       233.18 |      245.17 |
|        13 |       5 |        5 |      43022 |       171.42 |      191.69 |
| **Total** | **592** |  **580** | **528065** |  **5932.39** | **8175.24** |
