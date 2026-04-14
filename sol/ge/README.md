# Greedy‑Edge (GE) Construction

The Greedy‑Edge (GE) construction builds an initial station ordering by following a
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

<table><tr>
<td align="center"><img src="init.png" width="460"/><br><sub>Segment — transit 4,809.89 nm · total 7,052.74 nm</sub></td>
<td align="center"><img src="sweep.png" width="460"/><br><sub>Refinement (matheuristic sweep, pass 6) — transit 4,643.65 nm · total 6,886.50 nm</sub></td>
</tr></table>

## Segment Summary

### Segment (capacity feasible)

5|   Segment |   Length | Stations |      Catch | Transit (nm) | Distance (nm) |
|----------:|---------:|---------:|-----------:|-------------:|--------------:|
|         1 |      175 |       87 |      43418 |       802.03 |       1132.15 |
|         2 |      117 |       58 |      43833 |       423.08 |        646.73 |
|         3 |      223 |      111 |      43366 |       824.98 |       1263.98 |
|         4 |       87 |       43 |      42365 |       413.03 |        579.40 |
|         5 |       37 |       18 |      42413 |       193.50 |        266.43 |
|         6 |       67 |       33 |      43160 |       252.92 |        385.89 |
|         7 |       17 |        8 |      43286 |       176.18 |        208.05 |
|         8 |       75 |       37 |      37457 |       321.91 |        465.25 |
|         9 |       41 |       20 |      43796 |       219.13 |        294.15 |
|        10 |       75 |       37 |      40980 |       295.42 |        431.74 |
|        11 |       83 |       41 |      43652 |       209.33 |        362.45 |
|        12 |      119 |       59 |      43785 |       289.99 |        518.68 |
|        13 |       56 |       28 |      16554 |       388.39 |        497.83 |
| **Total** | **1172** |  **580** | **528065** |  **4809.89** |   **7052.74** |

### Refinement (matheuristic sweep, capacity feasible)

|   Segment |  Length | Stations |      Catch | Transit (nm) | Total (nm) |
|----------:|--------:|---------:|-----------:|-------------:|-----------:|
|         1 |      87 |       86 |      44492 |       752.35 |    1080.57 |
|         2 |      63 |       62 |      44923 |       443.13 |     680.66 |
|         3 |     113 |      112 |      44661 |       826.27 |    1269.50 |
|         4 |      45 |       44 |      44101 |       389.27 |     559.48 |
|         5 |      13 |       12 |      35747 |       153.68 |     202.50 |
|         6 |      35 |       34 |      44460 |       241.23 |     378.30 |
|         7 |      14 |       13 |      44841 |       189.95 |     242.05 |
|         8 |      37 |       36 |      38524 |       312.06 |     449.00 |
|         9 |      20 |       19 |      43509 |       220.35 |     293.50 |
|        10 |      38 |       37 |      40980 |       295.42 |     431.74 |
|        11 |      44 |       43 |      44128 |       206.91 |     366.98 |
|        12 |      48 |       47 |      35245 |       235.84 |     417.74 |
|        13 |      35 |       35 |      22454 |       377.17 |     514.47 |
| **Total** | **592** |  **580** | **528065** |  **4643.65** | **6886.50** |
