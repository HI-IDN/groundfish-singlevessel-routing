# Results

## Baseline distances

| Notation | Variant | Solver limits | No port | With port |
| --- | --- | --- | ---: | ---: |
| NP-MIP | No-port directed TSP | `L_NP = 2 hrs` | 3562.57 | N/A |
| C-MIP | Capacity-aware MIP | `L_cap = 12 hrs` | N/A | timeout |
| MH-NN | MH with nearest-neighbor | `L_2seg = 1-8 min` | N/A | 6911.67 |
| MH-CI | MH with cheapest-insertion | `L_2seg = 1-8 min` | 4377.20 | 5354.93 |
| MH-GE | MH with greedy-edge | `L_2seg = 1-8 min` | 5027.03 | 5104.34 |
| MH-noport | MH with NP-based initialization | `L_2seg = 1-8 min` | NP-MIP | 4762.17 |

The four initialization strategies yield very different starting points. NN gives the weakest capacity-feasible baseline, while noport gives the strongest baseline at the cost of a separate no-port MIP solve.

## Overall findings

Two broad patterns emerge:

1. Moderate two-segment limits, around 2 to 3 minutes, tend to perform best for a given initialization.
2. Larger limits, 4 to 8 minutes, substantially increase runtime without giving systematic improvements in the final route.

The practical implication is that many short local MIP solves are more effective than fewer long ones.

## MH-NN

| `L_2seg` (s) | Sweeps | Final distance | Stations moved mean/med/max | Accepted/solves | MIP gap min/mean/max/std (%) | Runtime (min) |
| ---: | ---: | ---: | --- | --- | --- | ---: |
| 60 | 7 | 5842.279 | 6 / 6 / 12 | 16 / 62 | 0 / 13 / 99 / 16 | 145 |
| 120 | 7 | 5994.583 | 5.4 / 4 / 12 | 12 / 62 | 0 / 7 / 32 / 8 | 273 |
| 180 | 7 | 6171.916 | 5.6 / 4 / 11 | 15 / 61 | 0 / 6 / 18 / 6 | 351 |
| 240 | 8 | 6263.959 | 4.4 / 2.5 / 11 | 14 / 55 | 0 / 7 / 99 / 13 | 431 |
| 300 | 6 | 6112.102 | 6.2 / 7 / 12 | 11 / 54 | 0 / 5 / 19 / 5 | 517 |
| 360 | 5 | 6067.262 | 6.6 / 8 / 12 | 10 / 51 | 0 / 4 / 33 / 7 | 560 |
| 420 | 7 | 6116.561 | 5.1 / 6 / 12 | 13 / 56 | 0 / 3 / 14 / 4 | 680 |
| 480 | 6 | 5894.774 | 5.5 / 5 / 12 | 14 / 49 | 0 / 4 / 28 / 5 | 727 |

## MH-CI

| `L_2seg` (s) | Sweeps | Final distance | Stations moved mean/med/max | Accepted/solves | MIP gap min/mean/max/std (%) | Runtime (min) |
| ---: | ---: | ---: | --- | --- | --- | ---: |
| 60 | 2 | 5000.347 | 5.5 / 5.5 / 11 | 4 / 24 | 0 / 14 / 99 / 20 | 66 |
| 120 | 2 | 5108.418 | 4.5 / 4.5 / 9 | 3 / 22 | 0 / 6 / 18 / 6 | 86 |
| 180 | 3 | 5062.339 | 4.0 / 2 / 10 | 4 / 24 | 0 / 7 / 18 / 6 | 155 |
| 240 | 5 | 5012.521 | 3.8 / 3 / 10 | 7 / 33 | 0 / 6 / 17 / 5 | 267 |
| 300 | 3 | 5067.459 | 5.3 / 6 / 10 | 7 / 28 | 0 / 5 / 14 / 5 | 247 |
| 360 | 5 | 4886.220 | 3.4 / 2 / 9 | 8 / 32 | 0 / 3 / 14 / 4 | 365 |
| 420 | 3 | 5108.968 | 3.7 / 3 / 8 | 4 / 24 | 0 / 5 / 13 / 4 | 367 |
| 480 | 4 | 5025.292 | 3.5 / 3 / 8 | 7 / 27 | 0 / 5 / 13 / 4 | 485 |

## MH-GE

| `L_2seg` (s) | Sweeps | Final distance | Stations moved mean/med/max | Accepted/solves | MIP gap min/mean/max/std (%) | Runtime (min) |
| ---: | ---: | ---: | --- | --- | --- | ---: |
| 60 | 6 | 4799.879 | 3.5 / 2.5 / 9 | 12 / 39 | 0 / 12 / 37 / 9 | 109 |
| 120 | 5 | 4751.567 | 4.6 / 5 / 8 | 12 / 40 | 0 / 11 / 52 / 10 | 229 |
| 180 | 6 | 4771.586 | 4.7 / 3.5 / 10 | 16 / 44 | 0 / 8 / 25 / 7 | 313 |
| 240 | 3 | 4850.068 | 4 / 4 / 8 | 7 / 27 | 0 / 8 / 29 / 7 | 267 |
| 300 | 5 | 4773.525 | 3.8 / 3 / 9 | 11 / 37 | 0 / 7 / 36 / 7 | 455 |
| 360 | 6 | 4853.620 | 3.8 / 3.5 / 9 | 16 / 39 | 0 / 6 / 26 / 6 | 549 |
| 420 | 3 | 4813.738 | 5.7 / 6 / 8 | 8 / 31 | 0 / 5 / 26 / 5 | 504 |
| 480 | 6 | 4746.185 | 3.5 / 2 / 9 | 11 / 39 | 0 / 6 / 20 / 5 | 825 |

## MH-noport

| `L_2seg` (s) | Sweeps | Final distance | Stations moved mean/med/max | Accepted/solves | MIP gap min/mean/max/std (%) | Runtime (min) |
| ---: | ---: | ---: | --- | --- | --- | ---: |
| 60 | 3 | 4533.433 | 3.7 / 4 / 7 | 7 / 23 | 0 / 14 / 99 / 21 | 89 |
| 120 | 3 | 4592.349 | 3.0 / 2 / 7 | 5 / 21 | 0 / 11 / 26 / 7 | 117 |
| 180 | 6 | 4484.448 | 4.7 / 5.5 / 8 | 10 / 48 | 0 / 9 / 34 / 7 | 388 |
| 240 | 2 | 4600.363 | 2.5 / 2.5 / 5 | 3 / 16 | 0 / 7 / 17 / 6 | 171 |
| 300 | 4 | 4437.978 | 4.5 / 5 / 8 | 8 / 31 | 0 / 8 / 62 / 12 | 312 |
| 360 | 5 | 4500.298 | 3.8 / 3 / 8 | 8 / 33 | 0 / 7 / 19 / 7 | 396 |
| 420 | 2 | 4604.483 | 2.5 / 2.5 / 5 | 3 / 16 | 0 / 6 / 14 / 5 | 285 |
| 480 | 10 | 4390.774 | 3.5 / 3 / 7 | 10 / 64 | 0 / 8 / 30 / 6 | 1120 |

## Best observed plan

Across all runs, the best final plan improves the baseline distance from `4762.172 nm` to `4390.774 nm`, a reduction of about `270 nm` or `5.8%`.

At realistic steaming speeds of 8 to 12 knots, that corresponds to about 23 to 34 hours saved, roughly one day at sea.
