# `include`

Project-wide shared headers.

This directory contains interfaces used across more than one module in the
workflow.

Typical contents:

```text
include/
├── shared data types
├── database helpers
├── feasibility interfaces
├── routing and preprocessing interfaces
└── shared JSON/reporting interfaces
```

Guideline
---------

- `include/`
  general shared headers used across the project
- `mip/include/`
  MIP-model-specific headers only

If a header is only used by a single implementation file or a single module, it
should usually live next to that module rather than here.
