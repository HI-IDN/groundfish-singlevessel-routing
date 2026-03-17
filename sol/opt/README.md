# OPT Initialization

The OPT initialization mirrors the old `src` no-port model. It first solves a directed no-port TSP
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

Because this is still a full directed TSP with lazy subtour elimination, OPT is much more expensive
than NN, GE, or CI. In return, it provides the strongest station-order baseline for later capacity
repair and matheuristic refinement.

![OPT Initialization](init.png)
