# GSP Solver Configuration

All solver parameters live in **`gsp_solver.yaml`**. Instance data (boat capacity,
home port, station counts) is loaded from `dat/gsp.db` at runtime, so the YAML stays
small and never drifts out of sync with the data.

See `README.md` for pipeline commands (`make -C src init`, `sweep`, etc.).

---

## Parameters

### `global_time_limit_seconds`

Wall-clock cap for multi-phase workflows. Sweep returns early once the limit is
reached, regardless of `sweep.max_iterations`.

### `boat.id`

Selects the vessel. Capacity, home port, and name are read from the database;
changing this integer is sufficient to re-target the solver to a different boat.

### `init.strategies`

Controls which initialization methods are run by `make -C src init`. Each entry
corresponds to a `sol/<strategy>/init.json` output. Per-strategy sub-keys (`nn: {}`,
etc.) are reserved for future method-specific options.


### `sweep.max_iterations`

Maximum boundary-swap iterations per sweep run. Each iteration attempts to improve
one adjacent segment pair; sweep terminates early if a full pass produces no improvement.

### `gurobi.time_limit_seconds`

Per-phase Gurobi time limits (seconds; `0` = no limit):

| Key | Phase |
|-----|-------|
| `0seg` | No-port MIP |
| `1seg` | Per-segment init TSP postopt |
| `2seg` | Two-segment sweep boundary MIP |
| `Xseg` | Fixed-port full MIP (applied after first incumbent) |

### `gurobi.haul_distance_scale`

Each station is modelled as an entry/exit node pair; the arc between them is the
haul (tow) distance. This multiplier scales those intra-station arcs in the MIP
objective, giving three regimes:

| `include_haul_distance` | scale | Effective weight | Effect |
|:-:|--:|--:|---|
| `true` | _(ignored)_ | 1.0 | Full haul in objective |
| `false` | `0.0` | 0.0 | Haul excluded; station orientation arbitrary |
| `false` | `ε > 0` | ε | Transit-primary; haul breaks orientation ties |

A scale of `0.00001` (1e-5) makes haul ~1/100 000th of a typical transit leg —
small enough to never distort the transit-optimal solution, large enough to steer
station entry/exit orientation toward the shorter tow direction.

Recommended settings:

| Phase | Scale | Rationale |
|-------|:-----:|-----------|
| `0seg` | `0.00001` | Transit-minimising global order; haul ties broken sensibly |
| `1seg` | `0.00001` | Single-segment postopt; per-segment haul is nearly constant |
| `2seg` | `1.0` | Sweep changes segment membership, so haul varies and must be fully counted |
| `Xseg` | `0.00001` | Port schedule fixed; transit-primary with orientation tie-breaking |

Avoid `0.0` unless deliberately ignoring tow orientation — it admits free haul
lengthening that `0.00001` prevents at no cost to transit quality.

### `gurobi.threads / mip_focus / seed`

Standard Gurobi controls. `seed: -1` uses a random seed (results vary across runs);
set a non-negative integer for reproducibility.

---

## References

- Gurobi Parameters: https://www.gurobi.com/documentation/
