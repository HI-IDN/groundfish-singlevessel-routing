# Matheuristic Framework

The matheuristic models a route as an ordered list of capacity-feasible segments separated by ports.
Each segment begins at a port or vessel start, visits a subset of stations whose expected cumulative
catch stays below capacity, and ends at the next port where the vessel unloads.

The method has two phases:

1. Initialization: produce an initial station ordering and a feasible segmentation.
2. Improvement: repeatedly refine adjacent segment pairs with restricted MIP subproblems.

This produces a scalable method that exploits local structure without trying to solve the full C-MIP
end to end.

## Phase 0: initialization and baseline

1. Build an ordered station list using an initialization strategy.
2. Convert that ordering into a capacity-feasible segmentation.
3. Solve each segment as a directed segment-TSP.
4. Mark all segment boundaries as active.

## Initial segmentation

From the total expected catch, the method computes the minimum number of required segments:

$$
N_\min = \ceil(\sum_{s=1}^{580} a_s / C)
$$

The route is scanned in order, and the nearest feasible port is inserted:

1. Before a station if adding that station would exceed capacity.
2. After a station if the running load reaches capacity and more work remains.

Each port insertion resets the load.

## Phase 1: boundary sweep

The paper defines a sweep as a full traversal of all boundaries in circular order. For each active
boundary between adjacent segments in tour order, the method calls the boundary-capacity
subroutine. When a boundary changes, that boundary and the other modified segments are marked
active again. The process stops only when a full circular sweep yields no accepted change.

## Boundary-capacity subroutine

For a boundary $b$ between adjacent segments $S_L$ and $S_R$, with boundary port $p$, the paper's
subroutine is:

1. Set $S_\text{2seg} = S_L \cup S_R \cup {s_D}$ where $s_D$ is empty unless the fallback move is
   used.
2. If fallback is used, $s_D$ is drawn from some other donor segment $S_D$, and
   $S_D' = S_D \ {s_D}$.
3. Fix the start and end nodes around $S_\text{2seg}$ and enforce exactly one visit to $p$.
4. Solve the two-segment $C-MIP$ on $S_\text{2seg}$ with time limit $L_\text{2seg}$.
5. Extract tentative segments $S_L'$ and $S_R'$ from the resulting tour.
6. Re-optimize each tentative segment with a directed segment-TSP using limit $L_\text{1seg}$.
7. If either segment-TSP fails, reject the move.
8. Accept the move if $d(S_L') + d(S_R') < d(S_L) + d(S_R)$.
9. If fallback was used, accept only if
   $d(S_L') + d(S_R') + d(S_D') < d(S_L) + d(S_R) + d(S_D)$.

## Fallback move

If the two-segment-only attempt does not improve the boundary, the paper's fallback is:

1. Select the closest station $s_D$ from any donor segment outside ${S_L, S_R}$.
2. Require that inserting $s_D$ into the nearer of ${S_L, S_R}$ remains capacity-feasible.
3. Rebuild the two-segment working set with $s_D$ included.
4. Re-run the boundary-capacity subroutine.
5. Accept only if the affected left, right, and donor segments improve in total.

## Algorithm form from the paper

The paper's matheuristic framework is:

1. Obtain an ordered station list and an initial partition $V$ using the chosen initialization mode.
2. Evaluate each segment in $V$ with the directed segment-TSP using limit $L_\text{1seg}$.
3. Mark all boundaries as active.
4. Repeat full circular sweeps over the boundaries until no boundary changes in a full sweep.
5. For each active boundary, call the boundary-capacity subroutine with $L_\text{1seg}$ and $L_\text{2seg}$.
6. If the boundary changes, update $V$ and mark that boundary and any modified segments as active.

## Key interpretation

This boundary pass is the actual second optimization phase described in the paper. It is a sweep
over adjacent segment boundaries, not a generic postprocessing stage.
