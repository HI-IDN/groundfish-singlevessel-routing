# GSP Solver Configuration

All solver parameters live in **`gsp_solver.yaml`**. Instance data (boat capacity,
home port, station counts) is loaded from `dat/gsp.db` at runtime, so the YAML stays
small and never drifts out of sync with the data.

See `README.md` for pipeline commands (`make -C src construction`, `segment`, `refinement`, etc.).

---

## Parameters

### `global_time_limit_seconds`

Wall-clock cap for multi-phase workflows. Sweep returns early once the limit is
reached, regardless of `sweep.max_iterations`.

### `boat.id`

Selects the vessel. Capacity, home port, and name are read from the database;
changing this integer is sufficient to re-target the solver to a different boat.

### `init.strategies`

Controls which construction methods are available to the workflow. Per-strategy sub-keys (`nn: {}`,
etc.) are reserved for future method-specific options.

### `sweep.max_iterations`

Maximum boundary-swap iterations per refinement (matheuristic sweep) run. Each iteration attempts to
improve
one adjacent segment pair; refinement terminates early if a full pass produces no improvement.

### `gurobi.time_limit_seconds`

Per-phase Gurobi time limits (seconds; `0` = no limit):

| Key    | Phase                                                    |
|--------|----------------------------------------------------------|
| `0seg` | No-port MIP                                              |
| `1seg` | Per-segment segment-stage TSP postopt                    |
| `2seg` | Two-segment refinement (matheuristic sweep) boundary MIP |
| `Xseg` | Fixed-port full MIP (applied after first incumbent)      |


### `gurobi.threads / mip_focus / seed`

Standard Gurobi controls. `seed: -1` uses a random seed (results vary across runs);
set a non-negative integer for reproducibility.

---

## References

- Gurobi Parameters: https://www.gurobi.com/documentation/
