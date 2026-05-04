#!/usr/bin/env Rscript
# make_baseline_table.R
#
# Builds the LaTeX table for construction and segmentation baseline transit
# distances, plus a short text summary of segment/refinement MIP runtimes.
#
# Run from the project root:
#   Rscript R/make_baseline_table.R

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

load_required_packages(c("jsonlite", "dplyr", "knitr"))

script_dir <- tryCatch(
  dirname(normalizePath(sys.frame(1)$ofile)),
  error = function(e) getwd()
)
sol_dir <- normalizePath(file.path(script_dir, "..", "sol"), mustWork = FALSE)

if (!dir.exists(sol_dir)) {
  sol_dir <- "sol"
}

methods <- c("nn", "ci", "ge", "noport", "fixedport")

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

format_distance <- function(value) {
  if (is.na(value)) {
    return("---")
  }
  sprintf("%.2f", value)
}

read_result_json <- function(method, source) {
  json_path <- file.path(sol_dir, method, sprintf("%s.json", source))
  if (!file.exists(json_path)) {
    warning(sprintf("Missing result JSON: %s", json_path))
    return(NULL)
  }

  tryCatch(
    jsonlite::fromJSON(json_path, simplifyVector = FALSE),
    error = function(e) {
      warning(sprintf("Failed to parse %s: %s", json_path, e$message))
      NULL
    }
  )
}

extract_solution_distance <- function(method, source, component = "transit") {
  json <- read_result_json(method, source)
  if (is.null(json)) {
    return(NA_real_)
  }

  final_variant <- json$summary$status$final
  if (is.null(final_variant) || length(final_variant) == 0) {
    return(NA_real_)
  }

  solution <- json$solution[[final_variant]]
  value <- solution$distance_nm$grand_total[[component]]

  if (is.null(value) || length(value) == 0 || is.na(value)) {
    return(NA_real_)
  }

  as.numeric(value)
}

construction_distance <- function(method) {
  format_distance(extract_solution_distance(method, "construction", "transit"))
}

segment_distance <- function(method) {
  format_distance(extract_solution_distance(method, "segment", "transit"))
}

runtime_for <- function(method, source) {
  json <- read_result_json(method, source)
  value <- json$summary$runtime_seconds$grandtotal

  if (is.null(value) || length(value) == 0 || is.na(value)) {
    return("---")
  }

  format_runtime(as.numeric(value))
}

runtime_for_sources <- function(method, sources) {
  values <- vapply(sources, function(source) {
    json <- read_result_json(method, source)
    value <- json$summary$runtime_seconds$grandtotal

    if (is.null(value) || length(value) == 0 || is.na(value)) {
      return(NA_real_)
    }

    as.numeric(value)
  }, numeric(1))

  if (all(is.na(values))) {
    return("---")
  }

  format_runtime(sum(values, na.rm = TRUE))
}

read_config_timeout_seconds <- function(key) {
  config_path <- normalizePath(
    file.path(sol_dir, "..", "config", "gsp_solver.yaml"),
    mustWork = FALSE
  )
  if (!file.exists(config_path)) {
    warning(sprintf("Missing solver config: %s", config_path))
    return(NA_real_)
  }

  lines <- readLines(config_path, warn = FALSE)
  match <- grep(sprintf("^\\s*%s:\\s*[0-9]+", key), lines, value = TRUE)
  if (length(match) == 0) {
    warning(sprintf("Missing timeout key '%s' in %s", key, config_path))
    return(NA_real_)
  }

  as.numeric(sub(sprintf("^\\s*%s:\\s*([0-9]+).*$", key), "\\1", match[[1]]))
}

format_timeout_runtime <- function(seconds) {
  if (is.na(seconds)) {
    return("---")
  }

  if (seconds %% 3600 == 0) {
    return(sprintf("%.0f h", seconds / 3600))
  }

  sprintf("%.1f min", seconds / 60)
}

extract_mip_solves <- function(json_path, method, source_label) {
  if (!file.exists(json_path)) {
    return(NULL)
  }

  json <- tryCatch(
    jsonlite::fromJSON(json_path, simplifyVector = FALSE),
    error = function(e) NULL
  )
  if (is.null(json) || is.null(json$mip)) {
    return(NULL)
  }

  tuple_names <- unlist(json$mip$solve_detail_tuple)
  solves <- json$mip$solves
  rt_col <- which(tuple_names == "runtime_seconds")

  if (length(solves) == 0 || length(rt_col) == 0) {
    return(NULL)
  }

  data.frame(
    method = method,
    source = source_label,
    runtime_seconds = vapply(solves, function(s) as.numeric(s[[rt_col]]), numeric(1)),
    stringsAsFactors = FALSE
  )
}

all_data <- list()
for (method in methods) {
  method_dir <- file.path(sol_dir, method)
  for (src in c("construction", "segment", "refinement")) {
    d <- extract_mip_solves(
      file.path(method_dir, sprintf("%s.json", src)),
      method,
      src
    )
    if (!is.null(d)) {
      all_data <- c(all_data, list(d))
    }
  }
}

if (length(all_data) == 0) {
  stop("No MIP data found in any of the method directories.")
}

df <- do.call(rbind, all_data)
df$source <- factor(df$source, levels = c("construction", "segment", "refinement"))

station_distance <- format_distance(extract_solution_distance("noport", "construction", "haul"))
cmip_timeout_runtime <- format_timeout_runtime(read_config_timeout_seconds("Xseg"))

runtime_rows <- tibble::tribble(
  ~method,     ~Runtime,
  "noport",    runtime_for("noport", "construction"),
  "fixedport", cmip_timeout_runtime,
  "nn",        runtime_for_sources("nn", c("construction", "segment")),
  "ci",        runtime_for_sources("ci", c("construction", "segment")),
  "ge",        runtime_for_sources("ge", c("construction", "segment")),
  "noport_mh", runtime_for("noport", "segment")
)

method_rows <- tibble::tribble(
  ~method,     ~Notation, ~Variant,                       ~"No port",    ~"With port",
  "noport",    "NP-MIP",  "No-port directed TSP",          construction_distance("noport"), "---",
  "fixedport", "C-MIP",   "Capacity-aware MIP",            "---",       "timeout",
  "nn",        "MH-NN",   "MH with nearest-neighbor",      "---",       segment_distance("nn"),
  "ci",        "MH-CI",   "MH with cheapest-insertion",    construction_distance("ci"), segment_distance("ci"),
  "ge",        "MH-GE",   "MH with greedy-edge",           construction_distance("ge"), segment_distance("ge"),
  "noport_mh", "MH-OPT",  "MH with NP-based initialization","(NP-MIP)",  segment_distance("noport")
)

table_tbl <- method_rows %>%
  left_join(runtime_rows, by = "method") %>%
  mutate(Runtime = ifelse(is.na(Runtime), "---", Runtime)) %>%
  select(Notation, Variant, "No port", "With port", Runtime)

phase_runtime_summary <- df %>%
  filter(source %in% c("segment", "refinement")) %>%
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
  )

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

cat(latex_table)
cat("\n\n")
cat("\\noindent ")
cat(sprintf(
  "The common station towing distance is %s nm; table distances report transit distance only. ",
  station_distance
))
cat(paste(phase_runtime_summary$summary, collapse = "; "))
cat(".\n")
