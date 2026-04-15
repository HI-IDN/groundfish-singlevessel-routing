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
<td align="center"><img src="construction.png" width="300"/><br><sub>Construction — nearest-neighbor ordering</sub></td>
<td align="center"><img src="segment.png" width="300"/><br><sub>Segment — transit 6,264.57 nm · total 8,507.42 nm</sub></td>
<td align="center"><img src="refinement.png" width="300"/><br><sub>Refinement (matheuristic sweep, pass 4) — transit 5,932.39 nm · total 8,175.24 nm</sub></td>
</tr></table>

## Segment Summary

### Construction (capacity infeasible)

|   Segment |   Length | Stations |      Catch | Transit (nm) |   Total (nm) |
|----------:|---------:|---------:|-----------:|-------------:|-------------:|
|         1 |     1160 |      580 |     528065 |     5,013.40 |     7,256.25 |
| **Total** | **1160** |  **580** | **528065** | **5,013.40** | **7,256.25** |

### Segment (capacity feasible)

|   Segment |   Length | Stations |      Catch | Transit (nm) |   Total (nm) |
|----------:|---------:|---------:|-----------:|-------------:|-------------:|
|         1 |       93 |       46 |      43175 |       302.59 |       468.48 |
|         2 |       67 |       33 |      43044 |       260.69 |       385.61 |
|         3 |       41 |       20 |      44950 |       244.73 |       323.55 |
|         4 |       77 |       38 |      43844 |       297.66 |       448.96 |
|         5 |       23 |       11 |      44910 |       193.54 |       234.00 |
|         6 |       99 |       49 |      44138 |       398.23 |       591.79 |
|         7 |      113 |       56 |      43080 |       343.57 |       566.51 |
|         8 |      143 |       71 |      41579 |       536.90 |       808.13 |
|         9 |      135 |       67 |      44166 |       443.76 |       706.85 |
|        10 |      169 |       84 |      44784 |       704.10 |     1,030.56 |
|        11 |       23 |       11 |      44394 |       201.99 |       246.72 |
|        12 |      185 |       92 |      44540 |     1,205.66 |     1,557.07 |
|        13 |        4 |        2 |       1461 |       366.86 |       374.90 |
| **Total** | **1172** |  **580** | **528065** | **5,500.29** | **7,743.15** |

### Refinement (matheuristic sweep, pass 4, capacity feasible)

|   Segment |  Length | Stations |      Catch | Transit (nm) |   Total (nm) |
|----------:|--------:|---------:|-----------:|-------------:|-------------:|
|         1 |      45 |       44 |      36576 |       295.63 |       453.42 |
|         2 |      34 |       33 |      43044 |       260.69 |       385.61 |
|         3 |      21 |       20 |      44950 |       244.73 |       323.55 |
|         4 |      39 |       38 |      43844 |       297.63 |       448.93 |
|         5 |      12 |       11 |      44910 |       193.54 |       234.00 |
|         6 |      50 |       49 |      44138 |       398.23 |       591.79 |
|         7 |      64 |       63 |      44816 |       364.08 |       613.91 |
|         8 |      65 |       64 |      39843 |       511.11 |       755.45 |
|         9 |      68 |       67 |      44166 |       443.76 |       706.85 |
|        10 |      84 |       83 |      43862 |       701.73 |     1,023.97 |
|        11 |      11 |       10 |      27133 |       184.64 |       225.51 |
|        12 |      13 |       12 |      34449 |       263.81 |       312.30 |
|        13 |      86 |       86 |      36334 |     1,030.42 |     1,357.58 |
| **Total** |  **592** |  **580** | **528065** | **5,190.01** | **7,432.86** |
