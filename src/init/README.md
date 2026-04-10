# `init`

Phase 0 initialization logic.

This folder should contain:

- the common init mode entrypoint
- initialization strategies such as `nn`, `ge`, `ci`
- the `noport` initialization path that consumes a no-port ordering and turns it into a capacity-feasible
  segmented solution

Intended boundary:

- `init/` owns construction and init-side orchestration
- `mip/` owns the Gurobi models that `init/` may call

Current contents:

- `gsp_init_mode.c`
  common entrypoint for `gsp --mode init`
- `nearest_neighbor.c`
- `greedy_insertion.c`
- `cheapest_insertion.c`
- `init_from_order.c`
  builds an init solution from an ordered input file, currently `sol/noport/noport.json`
- `noport_order_main.c`
  standalone executable that computes the no-port ordering by calling the model in `mip/noport.c`

The no-port ordering executable lives here because it is part of the initialization pipeline, even
though the underlying solver is implemented in `mip/`.
