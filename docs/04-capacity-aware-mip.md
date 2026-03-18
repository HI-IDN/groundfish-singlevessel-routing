# Capacity-Aware MIP

The survey-routing problem resembles a multi-trip vehicle routing problem because capacity limitations force repeated returns to port. At the same time, it differs from a classical VRP because all stations must be visited exactly once and the solution is a single directed tour whose segment breaks are induced by capacity. It is therefore a hybrid between a multi-trip VRP and a directed TSP with mandatory unloads.

## Node representation

The survey is represented as a directed tour over a graph containing:

1. The vessel start and end.
2. Station endpoints.
3. Port endpoints.

The vessel is represented by two nodes with zero internal cost so the tour is fixed to start and end correctly. Each station and port is also represented by a paired start and end node so direction can be chosen explicitly.

Let:

- `d[i,j]` be the waypoint-aware directed distance between nodes `i` and `j`.
- `x[i,j]` be a binary variable indicating whether the tour uses arc `(i,j)`.
- `C` be vessel capacity.

## Constraints

The model enforces:

1. Exactly one incoming and one outgoing arc for every node.
2. Exactly one direction choice for each paired station or port node.
3. Subtour elimination through lazy constraints.
4. Flow conservation for vessel load.
5. Catch injection at stations.
6. Load reset at ports.
7. Capacity limits on carried load.

## Objective

The objective is to minimize total directed travel distance:

```text
minimize sum over all selected arcs of distance times arc-selection
```

## Important modeling limitation

Because any port included in the model must also be visited exactly once, the full capacity-aware MIP cannot choose ports dynamically. The visited-port set must therefore be fixed in advance, and duplicated port nodes are used to model repeated calls.

## C-MIP procedure

The paper states the general capacity-aware MIP procedure as:

1. Build the directed graph with paired nodes for the vessel, stations, and fixed ports.
2. Duplicate port nodes to encode the fixed maximum number of port calls.
3. Create the MIP with degree, pair, flow, and capacity constraints.
4. Enable subtour elimination through a callback.
5. Set the solver time limit `L`.
6. Optimize and record the best feasible incumbent.
7. Return the incumbent tour and objective value, if any.

In the paper's terminology, this is the `C-MIP` procedure used both as a full end-to-end model and
as the exact subroutine inside the matheuristic boundary improvement step.

## Why the full model is hard

The C-MIP extends flow-based TSP models with directed towing, load propagation, mandatory port resets, and capacity. These additions make the model computationally difficult, especially when the instance is operationally large. The no-port variant, NP-MIP, removes the capacity structure and reduces to a directed TSP.
