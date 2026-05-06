## Algorithm 2 — Matheuristic Boundary-Sweep Framework (MH)

### Inputs
- Stations `S`
- Ports `P`
- Capacity `C`
- Waypoint-aware distances `d[i,j]`
- Start/end vessel nodes
- Initialization mode `InitMode`
- Single-segment TSP limit `L_1seg`
- Two-segment C-MIP limit `L_2seg`

### Output
- Best segmented tour found
- Refinement JSON with separate MIP accounting for:
  - `mip.1seg`: limit `L_1seg`, number of directed segment-TSP solves, and solve values
  - `mip.2seg`: limit `L_2seg`, number of two-segment C-MIP solves, and solve values

---

### Phase 0 — Initialization and Baseline

1. Obtain ordered stations `S_ord` and initial partition `V` using `InitMode`.

2. Evaluate each segment in `V` using the directed segment-TSP with time limit `L_1seg`.

3. Mark all segment boundaries as active.

---

### Phase 1 — Boundary-Sweep Improvement

A *sweep* corresponds to one full traversal of all boundaries in circular order.

Repeat until no boundary changes during a full sweep:

1. For each boundary `b` between adjacent segments in tour order:

    - If `b` is not active:
        - continue.

    - Mark `b` as inactive.

    - Call Algorithm 3 (`Optimize Boundary Capacity`) on boundary `b` using:
        - `L_1seg`,
        - `L_2seg`.

    - If the boundary changes:
        1. Update `V`.
        2. Mark the modified boundary and neighboring boundaries as active.

