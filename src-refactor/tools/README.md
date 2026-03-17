# `tools`

Standalone utilities outside the main `gsp` init/sweep solve flow.

Examples:

- survey export utilities
- country / coastline / waypoint bootstrap tools
- station import and distance rebuild entrypoints
- one-off converters or inspection helpers

Rule of thumb:

- if it is part of the main solve pipeline, it should usually live in `init/`, `sweep/`, `mip/`, or
  `common/`
- if it is a separate utility program, it belongs in `tools/`

This separation matters because `tools/` may share code with the solver, but it should not define
the solver architecture.

Current examples in this folder include:

- `station_import.c`
- `distance_builder.c`
- `historical_survey_import.c`
- `survey_import.c`
- `export_survey_json.c`
- `infer_waypoints_from_coastline.c`
