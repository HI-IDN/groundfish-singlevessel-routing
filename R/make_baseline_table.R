#!/usr/bin/env Rscript
# make_baseline_table.R
#
# Builds the LaTeX table for construction and segmentation baseline transit
# distances from dat/solution.db, plus a short summary of segment/refinement
# MIP runtimes.
#
# Run from the project root:
#   Rscript R/make_baseline_table.R
#   Rscript R/make_baseline_table.R --output Paper/tables/baseline.tex

source("R/gsp_db.R")

load_required_packages <- function(pkgs) {
  for (pkg in pkgs) {
    if (!requireNamespace(pkg, quietly = TRUE)) {
      stop(sprintf(
        "Package '%s' is required but not installed.\nInstall with: install.packages('%s')",
        pkg, pkg
      ), call. = FALSE)
    }
    suppressPackageStartupMessages(library(pkg, character.only = TRUE))
  }
}

load_required_packages(c("dplyr", "knitr", "tibble"))

parse_args <- function(args) {
  out <- list(
    gsp_db = "dat/gsp.db",
    solution_db = "dat/solution.db",
    config = "config/gsp_solver.yaml",
    output = NULL
  )

  i <- 1L
  while (i <= length(args)) {
    key <- args[[i]]
    val <- if (i < length(args)) args[[i + 1L]] else NULL
    if (!startsWith(key, "--") || is.null(val)) {
      stop(sprintf("Invalid argument near: %s", key), call. = FALSE)
    }
    name <- sub("^--", "", key)
    if (!name %in% names(out)) stop(sprintf("Unknown option: %s", key), call. = FALSE)
    out[[name]] <- val
    i <- i + 2L
  }
  out
}

format_runtime <- function(seconds) {
  ifelse(
    is.na(seconds),
    "---",
    ifelse(
      seconds >= 7200,
      sprintf("%.1f h", seconds / 3600),
      sprintf("%.1f min", seconds / 60)
    )
  )
}

format_timeout_runtime <- function(seconds) {
  if (is.na(seconds)) return("---")
  if (seconds %% 3600 == 0) return(sprintf("%.0f h", seconds / 3600))
  sprintf("%.1f min", seconds / 60)
}

format_distance <- function(value) {
  if (is.na(value)) return("---")
  sprintf("%.2f", value)
}

read_config_timeout_seconds <- function(path, key) {
  if (!file.exists(path)) {
    warning(sprintf("Missing solver config: %s", path))
    return(NA_real_)
  }

  lines <- readLines(path, warn = FALSE)
  match <- grep(sprintf("^\\s*%s:\\s*[0-9]+", key), lines, value = TRUE)
  if (length(match) == 0L) {
    warning(sprintf("Missing timeout key '%s' in %s", key, path))
    return(NA_real_)
  }

  as.numeric(sub(sprintf("^\\s*%s:\\s*([0-9]+).*$", key), "\\1", match[[1]]))
}

final_run_id <- function(con, method, phase) {
  rows <- db_read(con, "
    SELECT run_id
    FROM solution.runs
    WHERE method = ?
      AND phase = ?
      AND is_final = 1
    ORDER BY run_id
  ", list(method, phase))

  if (nrow(rows) == 0L) return(NA_character_)
  if (nrow(rows) > 1L) {
    warning(sprintf(
      "Multiple final runs for method='%s', phase='%s'; using %s",
      method, phase, rows$run_id[[1]]
    ))
  }
  rows$run_id[[1]]
}

run_transit_distance <- function(con, method, phase) {
  run_id <- final_run_id(con, method, phase)
  if (is.na(run_id)) return(NA_real_)

  rows <- db_read(con, "
    SELECT transit_nm
    FROM solution.distance
    WHERE run_id = ?
      AND segment IS NULL
  ", list(run_id))

  if (nrow(rows) == 0L || is.na(rows$transit_nm[[1]])) return(NA_real_)
  as.numeric(rows$transit_nm[[1]])
}

run_runtime_seconds <- function(con, method, phase) {
  rows <- db_read(con, "
    SELECT runtime_seconds
    FROM solution.runs
    WHERE method = ?
      AND phase = ?
      AND is_final = 1
    ORDER BY run_id
  ", list(method, phase))

  if (nrow(rows) == 0L || is.na(rows$runtime_seconds[[1]])) return(NA_real_)
  as.numeric(rows$runtime_seconds[[1]])
}

runtime_for <- function(con, method, phase) {
  format_runtime(run_runtime_seconds(con, method, phase))
}

runtime_for_phases <- function(con, method, phases) {
  values <- vapply(phases, function(phase) run_runtime_seconds(con, method, phase), numeric(1))
  if (all(is.na(values))) return("---")
  format_runtime(sum(values, na.rm = TRUE))
}

station_towing_distance <- function(con) {
  run_id <- final_run_id(con, "noport", "construction")
  if (is.na(run_id)) return(NA_real_)

  rows <- db_read(con, "
    SELECT total_nm - transit_nm AS haul_nm
    FROM solution.distance
    WHERE run_id = ?
      AND segment IS NULL
  ", list(run_id))

  if (nrow(rows) == 0L || is.na(rows$haul_nm[[1]])) return(NA_real_)
  as.numeric(rows$haul_nm[[1]])
}

phase_runtime_summary <- function(con) {
  df <- db_read(con, "
    SELECT
      CASE
        WHEN phase_code = 'S' THEN 'segment'
        WHEN phase_code = 'R' THEN 'refinement'
      END AS source,
      runtime_seconds
    FROM solution.mip_solves
    WHERE (phase_code = 'S' AND segment_model = '1seg')
       OR (phase_code = 'R' AND segment_model = '2seg')
  ")

  if (nrow(df) == 0L) return(character(0))

  df %>%
    filter(source %in% c("segment", "refinement")) %>%
    mutate(source = factor(source, levels = c("segment", "refinement"))) %>%
    group_by(source) %>%
    summarise(
      n = n(),
      mean_s = mean(runtime_seconds, na.rm = TRUE),
      max_s = max(runtime_seconds, na.rm = TRUE),
      .groups = "drop"
    ) %>%
    mutate(
      label = dplyr::case_when(
        source == "segment" ~ "Single-segment post-optimization",
        source == "refinement" ~ "Two-segment refinement",
        TRUE ~ as.character(source)
      ),
      summary = sprintf(
        "%s subproblems: $n=%d$, mean %.1f s, max %.1f s",
        label, n, mean_s, max_s
      )
    ) %>%
    pull(summary)
}

build_table <- function(con, config_path) {
  cmip_timeout <- format_timeout_runtime(read_config_timeout_seconds(config_path, "Xseg"))

  runtime_rows <- tibble::tribble(
    ~method,     ~Runtime,
    "noport",    runtime_for(con, "noport", "construction"),
    "fixedport", cmip_timeout,
    "nn",        runtime_for_phases(con, "nn", c("construction", "segmentation")),
    "ci",        runtime_for_phases(con, "ci", c("construction", "segmentation")),
    "ge",        runtime_for_phases(con, "ge", c("construction", "segmentation")),
    "noport_mh", runtime_for(con, "noport", "segmentation")
  )

  method_rows <- tibble::tribble(
    ~method,     ~Notation, ~Variant,                        ~"No port",                      ~"With port",
    "noport",    "NP-MIP",  "No-port directed TSP",           format_distance(run_transit_distance(con, "noport", "construction")), "---",
    "fixedport", "C-MIP",   "Capacity-aware MIP",             "---",                          "timeout",
    "nn",        "MH-NN",   "MH with nearest-neighbor",       "---",                          format_distance(run_transit_distance(con, "nn", "segmentation")),
    "ci",        "MH-CI",   "MH with cheapest-insertion",     format_distance(run_transit_distance(con, "ci", "construction")),     format_distance(run_transit_distance(con, "ci", "segmentation")),
    "ge",        "MH-GE",   "MH with greedy-edge",            format_distance(run_transit_distance(con, "ge", "construction")),     format_distance(run_transit_distance(con, "ge", "segmentation")),
    "noport_mh", "MH-OPT",  "MH with NP-based initialization", "(NP-MIP)",                     format_distance(run_transit_distance(con, "noport", "segmentation"))
  )

  method_rows %>%
    left_join(runtime_rows, by = "method") %>%
    mutate(Runtime = ifelse(is.na(Runtime), "---", Runtime)) %>%
    select(Notation, Variant, "No port", "With port", Runtime)
}

main <- function() {
  opt <- parse_args(commandArgs(trailingOnly = TRUE))
  con <- connect_gsp_db(opt$gsp_db, opt$solution_db)
  on.exit(DBI::dbDisconnect(con), add = TRUE)

  table_tbl <- build_table(con, opt$config)
  runtime_summary <- phase_runtime_summary(con)
  station_distance <- format_distance(station_towing_distance(con))

  latex_table <- knitr::kable(
    table_tbl,
    format = "latex",
    booktabs = TRUE,
    escape = FALSE,
    linesep = "",
    label = "construction-segmentation-baselines",
    caption = "Construction and segmentation baseline transit distances and total runtime.
  The no-port column gives the underlying construction route, and the with-port column gives the
  corresponding capacity-feasible port segmentation.
  All distance entries exclude the common station towing distance."
  )

  text <- paste0(
    latex_table,
    "\n\n\\noindent ",
    sprintf(
      "The common station towing distance is %s nm; table distances report transit distance only. ",
      station_distance
    ),
    paste(runtime_summary, collapse = "; "),
    ".\n"
  )

  if (!is.null(opt$output) && nzchar(opt$output)) {
    dir.create(dirname(opt$output), recursive = TRUE, showWarnings = FALSE)
    writeLines(text, opt$output, useBytes = TRUE)
    message("Wrote ", opt$output)
  } else {
    cat(text)
  }
}

if (sys.nframe() == 0L) {
  main()
}
