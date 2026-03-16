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
|         1 |       97 |       48 |      43996 |        564.88 |
|         2 |      123 |       61 |      43989 |        635.55 |
|         3 |      165 |       82 |      43992 |       1086.93 |
|         4 |      219 |      109 |      43998 |       1233.69 |
|         5 |       81 |       40 |      43989 |        716.85 |
|         6 |       77 |       38 |      44000 |       1062.78 |
|         7 |      103 |       51 |      43985 |        851.27 |
|         8 |      117 |       58 |      43924 |       1006.25 |
|         9 |       41 |       20 |      43975 |        449.86 |
|        10 |       61 |       30 |      43755 |        624.76 |
|        11 |       71 |       35 |      43825 |        512.38 |
|        12 |       15 |        7 |      41520 |        258.02 |
|        13 |        2 |        1 |       3117 |        134.92 |
| **Total** | **1172** |  **580** | **528065** |   **9138.14** |