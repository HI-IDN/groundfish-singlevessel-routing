#!/usr/bin/env Rscript
# make_baseline_table.R
#
# Builds the LaTeX table for variant notation, construction/segmentation
# baseline transit distances, and runtimes.
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

load_required_packages(c("tibble"))

parse_args <- function(args) {
  out <- list(
    gsp_db = "dat/gsp.db",
    solution_db = "dat/solution.db",
    config = "config/gsp_solver.yaml",
    output = "baseline.tex"
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
  if (is.na(seconds)) return("---")
  if (seconds >= 7200) return(sprintf("%.1f h", seconds / 3600))
  sprintf("%.1f min", seconds / 60)
}

format_duration <- function(seconds) {
  if (is.na(seconds)) return("---")
  if (seconds %% (7 * 24 * 3600) == 0 && seconds >= 7 * 24 * 3600) {
    value <- seconds / (7 * 24 * 3600)
    return(sprintf("%d %s", value, if (value == 1) "week" else "weeks"))
  }
  if (seconds %% (24 * 3600) == 0 && seconds >= 24 * 3600) {
    value <- seconds / (24 * 3600)
    return(sprintf("%d %s", value, if (value == 1) "day" else "days"))
  }
  if (seconds %% 3600 == 0 && seconds >= 3600) return(sprintf("%.0f h", seconds / 3600))
  sprintf("%.1f min", seconds / 60)
}

format_distance <- function(value) {
  if (is.na(value)) return("---")
  sprintf("%.2f", value)
}

read_config_number <- function(path, key) {
  if (!file.exists(path)) {
    warning(sprintf("Missing solver config: %s", path))
    return(NA_real_)
  }

  lines <- readLines(path, warn = FALSE)
  match <- grep(sprintf("^\\s*%s:\\s*[0-9]+", key), lines, value = TRUE)
  if (length(match) == 0L) {
    warning(sprintf("Missing numeric key '%s' in %s", key, path))
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

latex_distance <- function(value, suffix = "") {
  if (is.na(value)) return("\\multicolumn{1}{c}{\\textemdash}")
  paste0(format_distance(value), suffix)
}

build_table <- function(con, config_path) {
  cmip_timeout <- format_duration(read_config_number(config_path, "global_time_limit_seconds"))

  np_mip_distance <- run_transit_distance(con, "noport", "construction")

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
    ~method,     ~Notation, ~Variant,                        ~no_port,                                ~with_port,
    "noport",    "NP-MIP",  "No-port directed TSP",           latex_distance(np_mip_distance),         "\\multicolumn{1}{c}{\\textemdash}",
    "fixedport", "C-MIP",   "Capacity-aware MIP",             "\\multicolumn{1}{c}{\\textemdash}",      "timeout$^\\dagger$",
    "nn",        "MH-NN",   "MH with nearest-neighbor",       "\\multicolumn{1}{c}{\\textemdash}",      latex_distance(run_transit_distance(con, "nn", "segmentation")),
    "ci",        "MH-CI",   "MH with cheapest-insertion",     latex_distance(run_transit_distance(con, "ci", "construction")), latex_distance(run_transit_distance(con, "ci", "segmentation")),
    "ge",        "MH-GE",   "MH with greedy-edge",            latex_distance(run_transit_distance(con, "ge", "construction")), latex_distance(run_transit_distance(con, "ge", "segmentation")),
    "noport_mh", "MH-OPT",  "MH with NP-based initialization", latex_distance(np_mip_distance, "$^\\ast$"), latex_distance(run_transit_distance(con, "noport", "segmentation"))
  )

  rows <- merge(method_rows, runtime_rows, by = "method", all.x = TRUE, sort = FALSE)
  rows$Runtime[is.na(rows$Runtime)] <- "---"
  rows$Runtime[rows$method == "fixedport"] <- cmip_timeout
  rows[match(method_rows$method, rows$method), ]
}

render_baseline_latex <- function(rows) {
  row_lines <- apply(rows, 1L, function(row) {
    sprintf(
      "%s & %s & %s & %s & %s \\\\",
      row[["Notation"]],
      row[["Variant"]],
      row[["no_port"]],
      row[["with_port"]],
      row[["Runtime"]]
    )
  })

  c(
    "\\begin{table}[b]",
    "\\centering",
    "\\caption{",
    "Variant notation, baseline initialization distances, and runtime.",
    "}",
    "\\label{tab:variant-notation}",
    "",
    "{\\fontsize{6.5}{6.2}\\selectfont",
    "\\setlength{\\tabcolsep}{4pt}",
    "",
    "\\begin{tabular}[t]{llllr}",
    "\\toprule",
    "Notation & Variant & \\multicolumn{2}{c}{Distance (nm)} & Runtime\\\\",
    "\\cmidrule(lr){3-4}",
    " &  & no port & with port & \\\\",
    "\\midrule",
    row_lines,
    "\\bottomrule",
    "\\end{tabular}",
    "",
    "\\vspace{0.15em}",
    "",
    "\\tiny",
    "\\begin{tabular}{@{}l@{}}",
    "\\textemdash{} variant does not entail a corresponding no-port or with-port solution. \\\\",
    "$^\\ast$ Uses the NP-MIP ordering as initialization. \\\\",
    "$^\\dagger$ No feasible solution found before timeout.",
    "\\end{tabular}",
    "",
    "}",
    "\\end{table}"
  )
}

main <- function() {
  opt <- parse_args(commandArgs(trailingOnly = TRUE))
  con <- connect_gsp_db(opt$gsp_db, opt$solution_db)
  on.exit(DBI::dbDisconnect(con), add = TRUE)

  text <- paste(render_baseline_latex(build_table(con, opt$config)), collapse = "\n")

  if (!is.null(opt$output) && nzchar(opt$output)) {
    output_dir <- dirname(opt$output)
    if (!identical(output_dir, ".")) {
      dir.create(output_dir, recursive = TRUE, showWarnings = FALSE)
    }
    writeLines(text, opt$output, useBytes = TRUE)
    message("Wrote ", opt$output)
  } else {
    cat(text)
  }
}

if (sys.nframe() == 0L) {
  main()
}
