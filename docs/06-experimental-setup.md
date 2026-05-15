# Experimental Setup

## Data and instance summary

The experiments use data from the 2023 spring groundfish survey conducted by the Icelandic Marine
and Freshwater Research Institute.

The instance contains:

- 580 trawling stations
- 23 ports
- 7 repeated port visits
- 27 waypoint nodes
- 4 survey vessels in the full dataset

For simplicity, the experiments focus on a single vessel, *Árni Friðriksson*, based in
Hafnarfjörður, with capacity $C = 45$ tonnes.

The full model contains 611 paired endpoint nodes, covering stations, ports, and vessel start/end.
Station catch values are expected catches derived from historical time series. Total expected catch
is 528 tonnes, implying a minimum of 12 capacity-feasible segments. Distances are waypoint-aware
shortest-path distances that reroute land-crossing arcs via waypoints.

## Port configuration

The full C-MIP uses a fixed port list $P_\text{fix}$, including duplicated port entries to
represent repeated visits. In the experiments this matches the 2023 survey structure and yields 13
segments.
By contrast, the matheuristic selects ports dynamically from the unique candidate set.

## Model classes

Three model classes are evaluated:

1. **NP-MIP**: no-port directed TSP over all stations.
2. **C-MIP**: the full capacitated MIP with fixed ports.
3. **MH variants**: the proposed matheuristic using small two-segment C-MIP subproblems.

## Initialization strategies

Four initialization strategies are tested:

1. **NN**: nearest neighbor.
2. **CI**: cheapest insertion.
3. **GE**: greedy edge.
4. **OPT**: solve the no-port directed TSP first, then insert ports.

## Solver settings

All models are solved with Gurobi 13.

The full C-MIP remains intractable on the full dataset: even with a 12-hour time limit it does not
produce a feasible solution. By contrast:

- NP-MIP solves in a little over one hour.
- Single-segment TSP subproblems usually solve in seconds and are not the bottleneck.
- The two-segment C-MIP is the dominant computational bottleneck.

The study therefore evaluates:

```text
L_2seg in {10, 30, 60, 120, 180, 240, 300, 600} seconds
```

The experiment target runs only these capped two-segment limits; the uncapped default refinement is
not part of the experiment sweep.

## Matheuristic settings

The boundary-sweep procedure is capped at 100 sweeps, which is more than enough for all runs.
Boundary updates consider only immediate neighbors. After each accepted change, the affected
segments are re-solved using the directed segment-TSP.
