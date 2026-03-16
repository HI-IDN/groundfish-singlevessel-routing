# Greedy‑Edge (GE) Initialization

The Greedy‑Edge (GE) initialization constructs an initial station ordering by following a
Kruskal‑like edge‑selection process. At each step, the algorithm adds the shortest admissible edge
between two stations—subject to feasibility constraints—while ensuring that premature subtours do
not form. This creates a gradually expanding forest of short edges that ultimately forms a single
tour once all stations have been incorporated.

To maintain capacity feasibility, ports are inserted whenever extending the current segment would
exceed the vessel’s load limit. After each insertion, GE continues selecting the next shortest
eligible edge while preserving the no‑subtour rule.

Compared with NN and CI, GE often yields competitive and sometimes high‑quality initial orderings,
leveraging structural information in the underlying distance graph. However, because it builds the
tour based on edge lengths rather than full‑path structure, it can be less stable than CI,
particularly in regions where many similar‑length edges compete.
-----

## Route Plotting

![GE Initialization](init.png)

## Segment Summary

|   Segment |   Length | Stations |      Catch | Distance (nm) |
|----------:|---------:|---------:|-----------:|--------------:|
|         1 |       51 |       25 |      44705 |        351.27 |
|         2 |       53 |       26 |      37061 |        398.10 |
|         3 |       17 |        8 |      35262 |        219.19 |
|         4 |       37 |       18 |      42619 |        416.37 |
|         5 |       47 |       23 |      44306 |        356.24 |
|         6 |      101 |       50 |      43154 |        643.05 |
|         7 |      169 |       84 |      43407 |       1028.14 |
|         8 |      117 |       58 |      33722 |        711.89 |
|         9 |       59 |       29 |      44875 |        436.82 |
|        10 |      175 |       87 |      44428 |       1207.21 |
|        11 |      165 |       82 |      44952 |        955.81 |
|        12 |      131 |       65 |      44819 |        710.67 |
|        13 |       50 |       25 |      24755 |        351.74 |
| **Total** | **1172** |  **580** | **528065** |   **7786.50** |