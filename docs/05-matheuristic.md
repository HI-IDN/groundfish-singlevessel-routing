# Matheuristic Framework

The matheuristic models a route as an ordered list of capacity-feasible segments separated by ports. Each segment begins at a port or vessel start, visits a subset of stations whose expected cumulative catch stays below capacity, and ends at the next port where the vessel unloads.

The method has two phases:

1. Initialization: produce an initial station ordering and a feasible segmentation.
2. Improvement: repeatedly refine adjacent segment pairs with restricted MIP subproblems.

This produces a scalable method that exploits local structure without trying to solve the full C-MIP end to end.

## Phase 0: initialization and baseline

1. Build an ordered station list using an initialization strategy.
2. Convert that ordering into a capacity-feasible segmentation.
3. Solve each segment as a directed segment-TSP.
4. Mark all segment boundaries as active.

## Initial segmentation

From the total expected catch, the method computes the minimum number of required segments:

```text
N_min = ceil(total expected catch / capacity)
```

The route is scanned in order, and the nearest feasible port is inserted:

1. Before a station if adding that station would exceed capacity.
2. After a station if the running load reaches capacity and more work remains.

Each port insertion resets the load.

## Phase 1: boundary sweep

The improvement phase repeatedly scans boundaries between adjacent segments in circular order. For each active boundary, the method solves a restricted two-segment subproblem.

When a boundary changes, neighboring boundaries are marked active and reconsidered in later passes. The process stops when a full sweep yields no accepted change.

## Two-segment subproblem

For adjacent segments `S_L` and `S_R`, separated by boundary port `p`, the method solves a local C-MIP on `S_L union S_R` with:

1. Fixed start node.
2. Fixed end node.
3. Exactly one forced visit to `p`.

The MIP may reassign stations between the two segments subject to capacity. The best incumbent found within the two-segment time limit is accepted only if it improves total distance.

## Fallback move

If the local C-MIP does not produce an accepted change, the algorithm tries a fallback move:

1. Select the closest station from outside the two current segments.
2. Insert it into the nearer side if capacity allows.
3. Re-optimize the left, right, and donor segments.
4. Accept only if the three affected segments improve in total.

## Boundary optimization subroutine

1. Build the local two-segment working set.
2. Optionally add one donor station for the fallback case.
3. Fix start and end nodes and force exactly one visit to the boundary port.
4. Solve the two-segment C-MIP.
5. Extract tentative segments from the returned tour.
6. Re-optimize each tentative segment using directed segment-TSP solves.
7. Accept the move only if the affected segment total strictly improves.

## Key interpretation

This boundary pass is the actual second optimization phase described in the paper. It is a sweep over adjacent segment boundaries, not a generic postprocessing stage.
