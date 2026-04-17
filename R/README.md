# `R/`

R scripts for plotting and visualising GSP solver outputs.  
All plots are driven by `make -C src plot` targets; see `src/Makefile` for the full list.

## Scripts

| Script | Make target | Output |
|--------|-------------|--------|
| `plot_country.R` | `plot-country` | `dat/coastline_waypoints_ports.png` |
| `plot_survey_overview.R` | `plot-overview` | `dat/survey_overview.png` |
| `plot_single_vessel_route.R` | `plot-routes` | `sol/<strategy>/{init,sweep}.png` |
| `plot_multi_vessel_route.R` | `plot-multivessel` | `sol/survey/multivessel.png` |
| `plot_sweep_summary_panels.R` | `plot-sweep-panels` | `sol/sweep_summary_panels.png` |
| `plot_utils.R` | _(shared)_ | Sourced automatically — not run directly |

`plot_utils.R` provides shared helpers used by all plotting scripts:
`base_coastline_plot()`, `coord_fixed_for_lat()`, `apply_degree_axes()`,
`gsp_common_theme()`, `build_station_line_segments()`, `ensure_segment_list()`.

## Dependencies

```r
install.packages(c("ggplot2", "dplyr", "tibble", "DBI", "RSQLite", "jsonlite"))
```
