## Algorithm 3 — Optimize Boundary Capacity (C-MIP Subroutine in MH)

### Inputs
- Boundary `b` between segments `S_L` and `S_R`
- Boundary port `p`
- Capacity `C`
- Single-segment TSP limit `L_1seg`
- Two-segment C-MIP limit `L_2seg`

### Output
- Updated segments `S_L'`, `S_R'`
- Optionally updated donor segment `S_D'`
- Otherwise: no change

---

### Procedure

1. Set donor station:
   ```text
   s_D = ∅
   ```

---

### Boundary Setup

2. Define:
   ```text
   S_2seg = S_L ∪ S_R ∪ {s_D}
   ```

   where:
    - `s_D`, when used, is drawn from another donor segment `S_D`,
    - and:
      ```text
      S_D' = S_D \ {s_D}
      ```

3. Fix the start and end boundary nodes around `S_2seg`.

4. Enforce exactly one visit to boundary port `p`.

5. Solve the two-segment C-MIP (Algorithm 1) on `S_2seg` with time limit `L_2seg`.

6. Extract tentative segments:
   ```text
   S_L', S_R'
   ```

7. Re-optimize each tentative segment using directed segment-TSP solves with limit `L_1seg`.

---

### Acceptance Rules

8. If either segment-TSP solve fails:
    - return no change.

9. If:
   ```text
   d(S_L') + d(S_R') < d(S_L) + d(S_R)
   ```
   then:
    - return updated segments `S_L'`, `S_R'`.

10. Else if fallback was used and:
    ```text
    d(S_L') + d(S_R') + d(S_D')
    <
    d(S_L) + d(S_R) + d(S_D)
    ```
    then:
    - return updated segments `S_L'`, `S_R'`, `S_D'`.

---

### Fallback Strategy

11. If:
    ```text
    s_D = ∅
    ```

    then:

    1. Select the closest station `s_D` from any donor segment:
       ```text
       S_D ∈ V \ {S_L,S_R}
       ```
       In the cyclic sweep implementation, `S_D` must also be non-adjacent
       to both `S_L` and `S_R`; the immediate previous segment and immediate
       next segment are excluded from donor selection.

    2. First filter donor stations by capacity feasibility:
       ```text
       catch(S_L) + catch(s_D) <= C
       or
       catch(S_R) + catch(s_D) <= C
       ```

    3. Assign `s_D` to the nearer feasible side. If only one side is feasible,
       assign it to that side.

    4. Nearness is evaluated with the waypoint-aware distance matrix using the
       minimum distance from `s_D` to the boundary port and the location points
       already present in `S_L` and `S_R`.

    5. Require that inserting `s_D` into the selected side from:
       ```text
       {S_L,S_R}
       ```
       remains capacity-feasible.

    6. Return to **Boundary Setup**.

12. Return no change.

### Refinement JSON

- `solution.<pass>.fallback_changes` records accepted fallback moves in that pass.
- `summary.sweep.total_fallback_changes` records accepted fallback moves over the run.
- `mip.1seg.values[*].candidate_split_index = 1` and
  `mip.2seg.values[*].candidate_split_index = 1` identify solves from fallback attempts.
