# `mip`

Gurobi-backed model implementations.

This folder should contain solver logic only, not workflow entrypoints.

Intended contents:

- no-port ordering model
- end-paired TSP subsolver
- capacity-aware local model
- any future exact or hybrid subproblems used by `init/` or `sweep/`

Current status:

- `noport.c`
  implemented no-port paired-end model
- `endpaired_tsp.c`
  model surface for exact segment solves
- `capacity_aware.c`
  placeholder for the capacity-aware local model
- `mip_common.c`
  shared Gurobi/MIP helpers and parameter handling
- `mip_paired_tour.c`
  shared paired-end tour extraction and lazy-subtour callback helpers
- `mip/include/`
  model-specific headers plus shared MIP utility headers

What does not belong here:

- JSON writers
- CLI entrypoints
- init- or sweep-specific orchestration

Those should live in `init/`, `sweep/`, or shared utilities in `common/`.
