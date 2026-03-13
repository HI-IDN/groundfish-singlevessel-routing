# Cheapest‑Insertion (CI) Initialization

The Cheapest‑Insertion (CI) initialization constructs an initial ordering by iteratively inserting
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
default initialization for downstream refinement.

-----

## Route Plotting

![CI Initialization](init.png)

## Segment Summary

|   Segment |   Length | Stations |      Catch | Distance (nm) |
|----------:|---------:|---------:|-----------:|--------------:|
|         1 |       71 |       35 |      43347 |        902.54 |
|         2 |      127 |       63 |      43548 |       1413.97 |
|         3 |      177 |       88 |      43825 |       2327.79 |
|         4 |       65 |       32 |      38138 |        871.57 |
|         5 |      115 |       57 |      44921 |       1479.65 |
|         6 |      155 |       77 |      44170 |       1880.90 |
|         7 |      163 |       81 |      44751 |       1734.65 |
|         8 |       89 |       44 |      39689 |       1291.75 |
|         9 |        9 |        4 |      41179 |        427.79 |
|        10 |       49 |       24 |      44762 |        684.34 |
|        11 |       49 |       24 |      44053 |        587.94 |
|        12 |       89 |       44 |      44836 |       1194.42 |
|        13 |       14 |        7 |      10846 |        507.35 |
| **Total** | **1172** |  **580** | **528065** |  **15304.65** |