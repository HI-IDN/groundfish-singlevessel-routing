## Algorithm 1 — General Capacity-Aware MIP (C-MIP)

### Inputs
- Stations `S`
- Fixed port-call set `P_fix`
- Vessel capacity `C`
- Waypoint-aware distances `d[i,j]`
- Solver time limit `L_Xseg`

### Output
- Best feasible survey tour found within `L_Xseg`

### Procedure
1. Build the directed graph with paired nodes for:
    - the vessel,
    - stations `S`,
    - instantiated port calls `P_fix`.

2. Use:
    - waypoint-aware transit distances `d[i,j]` for inter-entity arcs,
    - zero cost for intra-entity arcs.

3. Create the MIP with:
    - degree constraints,
    - pair constraints,
    - flow constraints,
    - capacity constraints.

4. Enable subtour elimination via callback.

5. Set solver time limit to `L_Xseg`.

6. Optimize and record the best feasible incumbent.

7. Return the incumbent tour and objective value, if any.


