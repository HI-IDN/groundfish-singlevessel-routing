# `R/`

R scripts for plotting and visualising GSP solver outputs.
Static plots are driven by `make -C src plot`. That target rebuilds
`dat/solution.db` from the current `sol/**/*.json` files, then runs the R
plotting scripts listed below.

## Scripts

| Script                        | Make target         | Output                                  |
|-------------------------------|---------------------|-----------------------------------------|
| `plot_survey_overview.R`      | `plot-survey-overview`      | `dat/survey_overview.png`               |
| `plot_waypoints.R`            | `plot-waypoints`            | `dat/waypoints.png`                     |
| `plot_survey_multivessel.R`   | `plot-survey-multivessel`   | `sol/survey/multivessel.png`            |
| `plot_construction_segment.R` | `plot-construction-segment` | `sol/<method>/mh_phase0.png`            |
| `plot_refinement.R`           | `plot-refinement`           | `sol/<method>/mh_phase1_<l2seg>.png`    |
| `plot_refinement_sweep.R`     | `plot-refinement-sweep`     | `sol/refinement_sweep.png`, `sol/<method>/refinement_sweep.png` |
| `plot_mip_solves.R`           | `plot-mip-solves`           | `sol/mip_solves.png`                    |
| `make_baseline_table.R`       | `baseline_table`            | LaTeX baseline construction/segmentation table |
| `make_refinement_table.R`     | `refinement_table`          | LaTeX refinement table and `sol/refinement_transit_sweeps.png` |
| `gsp_db.R`                    | _(shared)_                  | Sourced automatically; not run directly |
| `gsp_plot_utils.R`            | _(shared)_                  | Sourced automatically; not run directly |

`refinement_sweep_leaflet.R` is not part of `make -C src plot`.

## Dependencies

```r
install.packages(c("ggplot2", "dplyr", "tibble", "DBI", "RSQLite", "jsonlite", "knitr", "cowplot", "tikzDevice"))
```
