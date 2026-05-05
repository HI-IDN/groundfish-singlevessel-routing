# Fixed-Port MIP

The fixed-port model solves a capacity-constrained MIP with a predefined sequence of port visits.

Port candidates are derived from the ports visited during the [2023 spring survey](../survey) and 
stored in `candidate_ports.json` (generated via `make -C src fixedport_candidates`).

These ports define the required port visits in the route, but not the detailed routing between them.  
The model partitions the tour into segments between consecutive port visits and optimizes the 
assignment and ordering of stations within each segment, subject to vessel capacity constraints.

This is the most expensive presolve path and is run with a generous time limit (`Xseg`).

-----

> ⚠️ Despite an extended (7 days) runtime, the fixed-port MIP did not find a single feasible 
> solution for the 2023 spring survey instance. 