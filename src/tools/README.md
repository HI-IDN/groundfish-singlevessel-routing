# `tools`

Standalone utilities outside the main init/sweep solve loop.

These programs are part of the overall workflow, but they are not themselves
the main optimization entrypoints.

Current Utilities
-----------------

```text
tools/
├── country_bootstrap_main.c      coastline and waypoint bootstrap into the routing database
├── station_import.c              station import utility
├── distance_builder.c            distance rebuild utility
├── prepare_routing_data_main.c   one-pass preprocessing wrapper
├── export_survey_json.c          export observed survey routes to JSON
└── export_fixedport_candidates.c derive fixed-port candidate sequences from survey JSON
```

Rule Of Thumb
-------------

- if it is a standalone utility program, it belongs in `tools/`
- if it is part of the main solve loop, it should usually live in `init/`, `sweep/`, `mip/`, or `common/`

Typical Outputs
---------------

- `dat/gsp.db`
- `sol/survey/boat*.json`
- `dat/candidate_ports.json`

These utilities feed the main optimization pipeline, but they should not define
the solver architecture.
