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
<td align="center"><img src="construction.png" width="300"/><br><sub>Construction — transit 5,138.75 nm · total 7,381.60 nm (capacity infeasible)</sub></td>
<td align="center"><img src="segment.png" width="300"/><br><sub>Segment — transit 4,858.97 nm · total 7,101.82 nm</sub></td>
<td align="center"><img src="refinement.png" width="300"/><br><sub>Refinement (matheuristic sweep, pass 5) — transit 4,689.84 nm · total 6,932.69 nm</sub></td>
</tr></table>

## Segment Summary

### Construction (capacity infeasible)

|   Segment |   Length | Stations |      Catch | Transit (nm) |   Total (nm) |
|----------:|---------:|---------:|-----------:|-------------:|-------------:|
|         1 |     1160 |      580 |     528065 |     5,138.75 |     7,381.60 |
| **Total** | **1160** |  **580** | **528065** | **5,138.75** | **7,381.60** |

### Segment (capacity feasible)

|   Segment |   Length | Stations |      Catch | Transit (nm) |   Total (nm) |
|----------:|---------:|---------:|-----------:|-------------:|-------------:|
|         1 |      181 |       90 |      44386 |       809.82 |     1,151.93 |
|         2 |      113 |       56 |      44909 |       422.97 |       638.64 |
|         3 |      235 |      117 |      44170 |       855.12 |     1,314.34 |
|         4 |       73 |       36 |      39517 |       375.19 |       517.35 |
|         5 |       39 |       19 |      44427 |       194.11 |       271.13 |
|         6 |       67 |       33 |      42659 |       254.67 |       387.45 |
|         7 |       23 |       11 |      44916 |       176.29 |       220.28 |
|         8 |       67 |       33 |      34314 |       296.92 |       424.25 |
|         9 |       45 |       22 |      44841 |       241.41 |       322.88 |
|        10 |       71 |       35 |      39935 |       291.03 |       420.89 |
|        11 |       89 |       44 |      44726 |       239.03 |       404.10 |
|        12 |      123 |       61 |      44944 |       312.76 |       549.40 |
|        13 |       46 |       23 |      14321 |       389.63 |       479.18 |
| **Total** | **1172** |  **580** | **528065** | **4,858.97** | **7,101.82** |

### Refinement (matheuristic sweep, pass 5, capacity feasible)

|   Segment |  Length | Stations |      Catch | Transit (nm) |   Total (nm) |
|----------:|--------:|---------:|-----------:|-------------:|-------------:|
|         1 |      92 |       91 |      44776 |       813.59 |     1,159.68 |
|         2 |      57 |       56 |      44909 |       422.97 |       638.64 |
|         3 |     118 |      117 |      44170 |       855.12 |     1,314.34 |
|         4 |      39 |       38 |      44239 |       368.76 |       518.82 |
|         5 |      19 |       18 |      40183 |       202.24 |       275.32 |
|         6 |      33 |       32 |      42181 |       240.76 |       369.57 |
|         7 |      12 |       11 |      44916 |       176.29 |       220.28 |
|         8 |      35 |       34 |      34601 |       293.77 |       422.97 |
|         9 |      19 |       18 |      42167 |       214.59 |       283.79 |
|        10 |      39 |       38 |      42322 |       293.67 |       433.94 |
|        11 |      45 |       44 |      44726 |       239.03 |       404.10 |
|        12 |      20 |       19 |      23078 |       124.43 |       195.59 |
|        13 |      64 |       64 |      35797 |       444.61 |       695.66 |
| **Total** |  **592** |  **580** | **528065** | **4,689.84** | **6,932.69** |
