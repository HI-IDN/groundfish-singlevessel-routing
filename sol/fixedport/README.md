# Fixed-Port MIP

The fixed-port model solves a capacity-constrained MIP with a predefined sequence of port visits.

Port candidates are derived from the ports visited during the [2023 spring survey](../survey) and 
stored in `candidate_ports.json` (generated via `make -C src fixedport_candidates`).

These ports define the required port visits in the route, but not the detailed routing between them.  
The model partitions the tour into segments between consecutive port visits and optimizes the 
assignment and ordering of stations within each segment, subject to vessel capacity constraints.

This is the most expensive presolve path and is run with a generous time limit (`Xseg`).

-----

## Route Plotting

<table><tr>
<td align="center"><img src="construction.png" width="460"/><br><sub>Fixed-port MIP — transit 1,640.46 nm · total 1,835.78 nm</sub></td>
</tr></table>

## Segment Summary

### Fixed-port solution (capacity feasible)

> ⚠️ Only 50 of 580 stations are covered — this is a partial / candidate-port result,
> not a full-survey solution.

|   Segment | Length | Stations |     Catch | Transit (nm) |   Total (nm) |
|----------:|-------:|---------:|----------:|-------------:|-------------:|
|         1 |      2 |        0 |         0 |         0.00 |         0.00 |
|         2 |      8 |        6 |      5670 |       221.19 |       245.34 |
|         3 |      2 |        0 |         0 |         0.00 |         0.00 |
|         4 |     14 |       12 |     12812 |       378.04 |       423.30 |
|         5 |      2 |        0 |         0 |         0.00 |         0.00 |
|         6 |     15 |       13 |      6005 |       396.60 |       446.99 |
|         7 |      2 |        0 |         0 |        43.55 |        43.55 |
|         8 |      9 |        7 |      3763 |       238.48 |       266.31 |
|         9 |      2 |        0 |         0 |         0.00 |         0.00 |
|        10 |      2 |        0 |         0 |         0.00 |         0.00 |
|        11 |     13 |       11 |      8799 |       275.36 |       319.01 |
|        12 |      3 |        1 |      1146 |        87.24 |        91.28 |
|        13 |      2 |        0 |         0 |         0.00 |         0.00 |
| **Total** | **76** |   **50** | **38195** | **1,640.46** | **1,835.78** |

