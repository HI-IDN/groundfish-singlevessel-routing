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
|         1 |       51 |       25 |      44705 |        650.96 |
|         2 |       53 |       26 |      37061 |        737.75 |
|         3 |       17 |        8 |      35262 |        406.20 |
|         4 |       37 |       18 |      42619 |        771.60 |
|         5 |       47 |       23 |      44306 |        660.16 |
|         6 |      101 |       50 |      43154 |       1191.67 |
|         7 |      169 |       84 |      43407 |       1905.31 |
|         8 |      117 |       58 |      33722 |       1319.24 |
|         9 |       59 |       29 |      44875 |        809.50 |
|        10 |      175 |       87 |      44428 |       2237.17 |
|        11 |      165 |       82 |      44952 |       1771.28 |
|        12 |      131 |       65 |      44819 |       1316.98 |
|        13 |       50 |       25 |      24755 |        651.83 |
| **Total** | **1172** |  **580** | **528065** |  **14429.66** |