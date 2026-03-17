# `include`

Project-wide shared headers.

Use this folder for interfaces that are consumed by more than one module, for example:

- shared data types
- feasibility interfaces
- database helpers
- routing and preprocessing interfaces

Guideline:

- `include/` is for general shared headers
- `mip/include/` is for MIP-model-specific interfaces only

If a header is only used by one module, it should usually live next to that module instead of here.

