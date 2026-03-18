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

## Segment Summary

|   Segment |   Length | Stations |      Catch | Distance (nm) |
|----------:|---------:|---------:|-----------:|--------------:|
|         1 |       97 |       48 |      43996 |        563.54 |
|         2 |      123 |       61 |      43989 |        633.56 |
|         3 |      165 |       82 |      43992 |       1066.74 |
|         4 |      219 |      109 |      43998 |       1230.25 |
|         5 |       81 |       40 |      43989 |        738.85 |
|         6 |       77 |       38 |      44000 |       1055.08 |
|         7 |      103 |       51 |      43988 |        589.31 |
|         8 |       73 |       36 |      43965 |        498.99 |
|         9 |       81 |       40 |      43958 |        862.24 |
|        10 |       95 |       47 |      43654 |        981.78 |
|        11 |       49 |       24 |      43466 |        523.32 |
|        12 |        7 |        3 |      37074 |        295.48 |
|        13 |        2 |        1 |       7996 |        180.02 |
| **Total** | **1172** |  **580** | **528065** |   **9219.15** |

