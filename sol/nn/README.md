# Nearest-Neighbor (NN) Initialization

The Nearest-Neighbor (NN) initialization constructs an initial ordering of the survey stations by
repeatedly selecting, from the set of unvisited stations, the closest feasible next station
according to the waypoint-aware distance matrix. At each step, the algorithm evaluates whether
adding the next station would exceed the vessel's remaining capacity within the current segment. If
capacity would be violated, a port is inserted, the load is reset, and the process continues from
that port.

Because NN expands the route by always choosing the locally nearest feasible option, it is fast to
compute and scales easily to large station sets. However, its purely greedy, distance-driven
behavior can lead to noisy or irregular segmentations, especially in regions with clustered
stations or sharp load gradients. Despite this, NN provides a lightweight deterministic baseline
that is useful for benchmarking more sophisticated initialization strategies.

The current NN output is:

- `sol/nn/init.json`
- `sol/nn/init.png`

Produced by:

```bash
make -C src init_nn
```

-----

## Route Plotting

![NN Initialization](init.png)
![NN Sweep Adjustment](sweep.png)

## Segment Summary

### Initialization (capacity feasible)

|   Segment |   Length | Stations |      Catch | Distance (nm) |
|----------:|---------:|---------:|-----------:|--------------:|
|         1 |       97 |       48 |      43996 |        522.48 |
|         2 |      123 |       61 |      43989 |        572.59 |
|         3 |      165 |       82 |      43992 |        923.12 |
|         4 |      219 |      109 |      43998 |       1101.59 |
|         5 |       81 |       40 |      43989 |        679.37 |
|         6 |       77 |       38 |      44000 |        975.95 |
|         7 |      103 |       51 |      43988 |        566.19 |
|         8 |       73 |       36 |      43965 |        440.49 |
|         9 |       81 |       40 |      43958 |        822.84 |
|        10 |       95 |       47 |      43654 |        909.27 |
|        11 |       49 |       24 |      43466 |        518.03 |
|        12 |        7 |        3 |      37074 |        295.48 |
|        13 |        2 |        1 |       7996 |        180.02 |
| **Total** | **1172** |  **580** | **528065** |   **8507.42** |

### Sweep adjustment (capacity feasible)

|   Segment |  Length | Stations |      Catch | Distance (nm) |
|----------:|--------:|---------:|-----------:|--------------:|
|         1 |      55 |       54 |      43517 |        517.73 |
|         2 |      73 |       72 |      44379 |        695.30 |
|         3 |      64 |       63 |      37482 |        706.68 |
|         4 |     110 |      109 |      43998 |       1101.59 |
|         5 |      43 |       42 |      44581 |        684.91 |
|         6 |      36 |       35 |      43348 |        872.37 |
|         7 |      53 |       52 |      44048 |        594.62 |
|         8 |      36 |       35 |      43624 |        437.29 |
|         9 |      43 |       42 |      44411 |        823.61 |
|        10 |      46 |       45 |      42818 |        862.34 |
|        11 |      24 |       23 |      40365 |        443.17 |
|        12 |       4 |        3 |      12472 |        245.17 |
|        13 |       5 |        5 |      43022 |        191.69 |
| **Total** | **592** |  **580** | **528065** |   **8176.45** |