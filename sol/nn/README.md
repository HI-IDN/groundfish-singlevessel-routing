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
make -C src-refactor init_nn
```

-----

## Route Plotting

![NN Initialization](init.png)

## Segment Summary

|   Segment |   Length | Stations |      Catch | Distance (nm) |
|----------:|---------:|---------:|-----------:|--------------:|
|         1 |       81 |       40 |      44000 |        716.31 |
|         2 |      125 |       62 |      43993 |        694.90 |
|         3 |      187 |       93 |      44000 |       1082.18 |
|         4 |       93 |       46 |      43998 |        797.03 |
|         5 |       49 |       24 |      43991 |        539.85 |
|         6 |      115 |       57 |      43993 |       1027.56 |
|         7 |      203 |      101 |      43993 |       1679.03 |
|         8 |      115 |       57 |      43982 |        744.30 |
|         9 |       77 |       38 |      43990 |        969.80 |
|        10 |       69 |       34 |      43841 |        485.41 |
|        11 |       47 |       23 |      43638 |        363.28 |
|        12 |        9 |        4 |      41508 |        314.75 |
|        13 |        2 |        1 |       3138 |        224.95 |
| **Total** | **1172** |  **580** | **528065** |   **9639.34** |