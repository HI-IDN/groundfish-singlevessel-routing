# No-Port (Noport) Construction

The no-port MIP solves a directed TSP over all stations without capacity constraints or port calls.
Each station contributes an entry and exit node; the model finds a Hamiltonian cycle on these
doubled nodes with lazy subtour elimination and orients it back into a signed station order.

This answers only: *"what is the best station ordering if capacity resets are ignored?"*
Ports are inserted afterwards when the ordering is converted into a capacity-feasible route.
Because it is a full directed TSP, noport is considerably more expensive than NN, GE, or CI, but
provides the strongest station-order baseline for downstream refinement.

-----

## Route Plotting

<table><tr>
<td align="center"><img src="noport.png" width="300"/><br><sub>No-port MIP — transit 3,552.03 nm (capacity infeasible)</sub></td>
<td align="center"><img src="init.png" width="300"/><br><sub>Segment — transit 4,335.18 nm · total 6,578.03 nm</sub></td>
<td align="center"><img src="sweep.png" width="300"/><br><sub>Refinement (matheuristic sweep, pass 3) — transit 4,300.17 nm · total 6,543.02 nm</sub></td>
</tr></table>

## Segment Summary

### No-port solution (capacity infeasible)

|   Segment |  Length | Stations |      Catch | Transit (nm) |   Total (nm) |
|----------:|--------:|---------:|-----------:|-------------:|-------------:|
|         1 |     580 |      580 |     528065 |     3,552.03 |     5,794.88 |
| **Total** | **580** |  **580** | **528065** | **3,552.03** | **5,794.88** |

### Segment (capacity feasible)

|   Segment |   Length | Stations |      Catch | Transit (nm) |   Total (nm) |
|----------:|---------:|---------:|-----------:|-------------:|-------------:|
|         1 |       81 |       40 |      41219 |       323.37 |       472.54 |
|         2 |       71 |       35 |      44151 |       253.52 |       382.96 |
|         3 |      133 |       66 |      43980 |       339.94 |       590.50 |
|         4 |      129 |       64 |      44598 |       392.59 |       645.55 |
|         5 |      167 |       83 |      38996 |       569.90 |       884.15 |
|         6 |      137 |       68 |      44894 |       465.81 |       729.95 |
|         7 |      205 |      102 |      44850 |       697.34 |      1097.06 |
|         8 |       51 |       25 |      43468 |       275.27 |       375.16 |
|         9 |       43 |       21 |      34211 |       164.31 |       249.20 |
|        10 |       55 |       27 |      38620 |       211.87 |       320.14 |
|        11 |       13 |        6 |      39530 |       202.73 |       226.82 |
|        12 |       39 |       19 |      44514 |       228.30 |       302.89 |
|        13 |       48 |       24 |      25034 |       210.23 |       301.09 |
| **Total** | **1172** |  **580** | **528065** | **4,335.18** | **6,578.03** |

### Refinement (matheuristic sweep, capacity feasible)

|   Segment |  Length | Stations |      Catch | Transit (nm) |   Total (nm) |
|----------:|--------:|---------:|-----------:|-------------:|-------------:|
|         1 |      41 |       40 |      41219 |       323.37 |       472.54 |
|         2 |      37 |       36 |      44237 |       251.61 |       384.02 |
|         3 |      64 |       63 |      43825 |       326.02 |       570.12 |
|         4 |      68 |       67 |      44792 |       404.50 |       663.95 |
|         5 |      83 |       82 |      38871 |       566.09 |       877.36 |
|         6 |      69 |       68 |      44894 |       465.81 |       729.95 |
|         7 |     103 |      102 |      44850 |       697.34 |      1097.06 |
|         8 |      26 |       25 |      43468 |       275.27 |       375.16 |
|         9 |      22 |       21 |      34211 |       164.31 |       249.20 |
|        10 |      28 |       27 |      38620 |       211.87 |       320.14 |
|        11 |       6 |        5 |      37363 |       192.03 |       212.17 |
|        12 |      17 |       16 |      41411 |       212.25 |       275.16 |
|        13 |      28 |       28 |      30304 |       209.69 |       316.18 |
| **Total** | **592** |  **580** | **528065** | **4,300.17** | **6,543.02** |
