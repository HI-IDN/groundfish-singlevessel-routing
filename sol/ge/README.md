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
|         1 |      175 |       87 |      43418 |       1132.15 |
|         2 |      117 |       58 |      43833 |        646.73 |
|         3 |      223 |      111 |      43366 |       1263.98 |
|         4 |       87 |       43 |      42365 |        579.40 |
|         5 |       37 |       18 |      42413 |        266.43 |
|         6 |       67 |       33 |      43160 |        385.89 |
|         7 |       17 |        8 |      43286 |        208.05 |
|         8 |       75 |       37 |      37457 |        465.25 |
|         9 |       41 |       20 |      43796 |        294.15 |
|        10 |       75 |       37 |      40980 |        431.74 |
|        11 |       83 |       41 |      43652 |        362.45 |
|        12 |      119 |       59 |      43785 |        518.68 |
|        13 |       56 |       28 |      16554 |        497.83 |
| **Total** | **1172** |  **580** | **528065** |   **7052.74** |