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
|         1 |       83 |       41 |      44998 |        549.44 |
|         2 |      135 |       67 |      44994 |        645.48 |
|         3 |      117 |       58 |      44997 |        810.90 |
|         4 |      123 |       61 |      44986 |        661.12 |
|         5 |      187 |       93 |      44998 |       1543.69 |
|         6 |      127 |       63 |      44998 |        991.66 |
|         7 |      169 |       84 |      44992 |       1546.30 |
|         8 |       47 |       23 |      44883 |        838.72 |
|         9 |       65 |       32 |      44991 |        452.24 |
|        10 |       79 |       39 |      44777 |        513.43 |
|        11 |       23 |       11 |      44428 |        295.64 |
|        12 |       16 |        8 |      34023 |        391.29 |
| **Total** | **1171** |  **580** | **528065** |   **9235.71** |