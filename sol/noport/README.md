# Noport Initialization

The noport initialization mirrors the old `src` no-port model. It first solves a directed no-port TSP
over the boat and all stations only. Ports are not part of the optimization model at this stage.
The boat contributes one start endpoint and one end endpoint, and each station contributes its two
directional endpoints. The model then chooses a single Hamiltonian cycle on these doubled nodes with
lazy subtour elimination, and that cycle is oriented back into a signed station order.

High level:

- The no-port MIP optimizes only the port-free station ordering.
- The boat is the anchor of the no-port model, not a nearest or fixed port.
- The boat closure arcs are zero-cost in both directions.
- Ports are inserted only later, when the no-port ordering is converted into a capacity-feasible
  route.

This separation is intentional. The no-port MIP is meant to answer only: *"what is the best station
ordering if capacity resets are ignored?"* It is not the full capacity-feasible routing model.

![Noport Model](noport.png)

Because this is still a full directed TSP with lazy subtour elimination, noport is much more expensive
than NN, GE, or CI. In return, it provides the strongest station-order baseline for later capacity
repair and matheuristic refinement.

![Noport Initialization](init.png)
![Noport Sweep Adjustment](sweep.png)

## Segment Summary

### No-Port solution (capacity infeasible)

|   Segment |  Length | Stations |      Catch | Distance (nm) |
|----------:|--------:|---------:|-----------:|--------------:|
|         1 |     580 |      580 |     528065 |       5794.88 |
| **Total** | **580** |  **580** | **528065** |   **5794.88** |

### Initilization (capacity feasible)

|   Segment |   Length | Stations |      Catch | Distance (nm) |
|----------:|---------:|---------:|-----------:|--------------:|
|         1 |       81 |       40 |      44394 |        483.48 |
|         2 |       13 |        6 |      28261 |        240.82 |
|         3 |       21 |       10 |      44991 |        194.52 |
|         4 |       69 |       34 |      44676 |        365.09 |
|         5 |       29 |       14 |      36579 |        224.17 |
|         6 |       91 |       45 |      44719 |        569.65 |
|         7 |      243 |      121 |      44241 |       1314.78 |
|         8 |      107 |       53 |      44993 |        585.75 |
|         9 |      177 |       88 |      44749 |        968.31 |
|        10 |      127 |       63 |      44747 |        529.49 |
|        11 |      107 |       53 |      42140 |        479.84 |
|        12 |       67 |       33 |      44937 |        400.77 |
|        13 |       40 |       20 |      18638 |        216.53 |
| **Total** | **1172** |  **580** | **528065** |   **6573.19** |

### Sweep adjustment (capacity feasible)

|   Segment |  Length | Stations |      Catch | Distance (nm) |
|----------:|--------:|---------:|-----------:|--------------:|
|         1 |      33 |       32 |      34830 |        358.57 |
|         2 |      15 |       14 |      37825 |        279.26 |
|         3 |      11 |       10 |      44991 |        194.52 |
|         4 |      32 |       31 |      39740 |        334.08 |
|         5 |      18 |       17 |      41515 |        240.15 |
|         6 |      46 |       45 |      44719 |        569.65 |
|         7 |     122 |      121 |      44241 |       1314.78 |
|         8 |      54 |       53 |      44993 |        585.75 |
|         9 |      89 |       88 |      44749 |        968.31 |
|        10 |      64 |       63 |      44747 |        529.49 |
|        11 |      54 |       53 |      42140 |        479.84 |
|        12 |      34 |       33 |      44937 |        400.77 |
|        13 |      20 |       20 |      18638 |        216.53 |
| **Total** | **592** |  **580** | **528065** |   **6471.70** |
