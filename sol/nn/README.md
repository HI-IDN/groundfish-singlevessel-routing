# Nearest‑Neighbor (NN) Initialization

The Nearest‑Neighbor (NN) initialization constructs an initial ordering of the survey stations by
repeatedly selecting, from the set of unvisited stations, the closest feasible next station
according to the waypoint‑aware distance matrix. At each step, the algorithm evaluates whether
adding the next station would exceed the vessel's remaining capacity within the current segment. If
capacity would be violated, a port is inserted, the load is reset, and the process continues from
that port.

Because NN expands the route by always choosing the locally nearest feasible option, it is extremely
fast to compute and scales easily to large station sets. However, its purely greedy, distance‑driven
behavior can lead to noisy or irregular segmentations, especially in regions with clustered stations
or sharp load gradients. Despite this, NN provides a lightweight and fully deterministic baseline
that is often useful for benchmarking more sophisticated initialization strategies.

