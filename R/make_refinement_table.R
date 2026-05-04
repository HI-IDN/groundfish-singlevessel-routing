#!/usr/bin/env Rscript
# make_refinement_table.R
#
# Builds a LaTeX summary table from sol/*/refinement.json files.
#
# Run from the project root:
#   Rscript R/make_refinement_table.R

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

load_required_packages(c("jsonlite", "dplyr", "ggplot2", "cowplot", "knitr"))

script_dir <- tryCatch(
  dirname(normalizePath(sys.frame(1)$ofile)),
  error = function(e) getwd()
)
sol_dir <- normalizePath(file.path(script_dir, "..", "sol"), mustWork = FALSE)

if (!dir.exists(sol_dir)) {
  sol_dir <- "sol"
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

format_distance <- function(value) {
  if (is.na(value)) {
    return("---")
  }
  sprintf("%.2f", value)
}

format_num <- function(value, digits = 1) {
  if (is.null(value) || length(value) == 0 || is.na(value)) {
    return("---")
  }
  sprintf(paste0("%.", digits, "f"), as.numeric(value))
}

read_result_json <- function(method, source = "refinement") {
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

extract_solution_distance <- function(json, component = "transit") {
  if (is.null(json)) {
    return(NA_real_)
  }

  final_variant <- json$summary$status$final
  if (is.null(final_variant) || length(final_variant) == 0) {
    return(NA_real_)
  }

  value <- json$solution[[final_variant]]$distance_nm$grand_total[[component]]
  if (is.null(value) || length(value) == 0 || is.na(value)) {
    return(NA_real_)
  }

  as.numeric(value)
}

notation_rows <- tibble::tribble(
  ~method,   ~Notation, ~Variant,
  "nn",      "MH-NN",   "MH with nearest-neighbor",
  "ci",      "MH-CI",   "MH with cheapest-insertion",
  "ge",      "MH-GE",   "MH with greedy-edge",
  "noport",  "MH-OPT",  "MH with NP-based initialization"
)

trajectory_rows <- function(method) {
  json <- read_result_json(method)
  if (is.null(json)) {
    return(NULL)
  }

  totals <- as.numeric(unlist(json$summary$distance_nm$trajectory))
  station_distance <- extract_solution_distance(json, "haul")
  transit <- totals - station_distance
  initial_transit <- transit[[1]]
  moved <- vapply(seq_along(transit), function(i) {
    if (i == 1) {
      return(NA_integer_)
    }

    pass_name <- sprintf("pass%d", i - 1)
    value <- json$solution[[pass_name]]$stations_moved
    if (is.null(value) || length(value) == 0 || is.na(value)) {
      return(NA_integer_)
    }

    as.integer(value)
  }, integer(1))
  accepted <- vapply(seq_along(transit), function(i) {
    if (i == 1) {
      return(NA_integer_)
    }

    pass_name <- sprintf("pass%d", i - 1)
    value <- json$solution[[pass_name]]$accepted_capacity_solves
    if (is.null(value) || length(value) == 0 || is.na(value)) {
      return(NA_integer_)
    }

    as.integer(value)
  }, integer(1))
  capacity_solves <- vapply(seq_along(transit), function(i) {
    if (i == 1) {
      return(NA_integer_)
    }

    pass_name <- sprintf("pass%d", i - 1)
    value <- json$solution[[pass_name]]$total_capacity_solves
    if (is.null(value) || length(value) == 0 || is.na(value)) {
      return(NA_integer_)
    }

    as.integer(value)
  }, integer(1))
  mip_solves <- vapply(seq_along(transit), function(i) {
    if (i == 1) {
      return(NA_integer_)
    }

    pass_name <- sprintf("pass%d", i - 1)
    value <- json$solution[[pass_name]]$mip_solves
    if (is.null(value) || length(value) == 0 || is.na(value)) {
      return(NA_integer_)
    }

    as.integer(value)
  }, integer(1))

  tibble::tibble(
    method = method,
    sweep = seq_along(transit) - 1,
    transit_nm = transit,
    relative_improvement_percent = 100 * (initial_transit - transit) / initial_transit,
    stations_moved = moved,
    accepted_capacity_solves = accepted,
    total_capacity_solves = capacity_solves,
    mip_solves = mip_solves,
    point_label = ifelse(
      is.na(moved),
      NA_character_,
      sprintf("moved %d\naccepted %d/%d", moved, accepted, capacity_solves)
    )
  )
}

refinement_row <- function(method) {
  json <- read_result_json(method)
  if (is.null(json)) {
    return(NULL)
  }

  moved <- json$summary$sweep$stations_moved_per_pass
  accepted <- json$summary$sweep$accepted_capacity_solves
  capacity_solves <- json$summary$sweep$total_capacity_solves
  trajectory <- unlist(json$summary$distance_nm$trajectory)
  initial_total <- as.numeric(trajectory[[1]])
  final_total <- as.numeric(json$summary$distance_nm$final)
  station_distance <- extract_solution_distance(json, "haul")
  initial_transit <- initial_total - station_distance
  final_transit <- extract_solution_distance(json, "transit")
  improvement <- initial_transit - final_transit
  relative_improvement <- 100 * improvement / initial_transit

  tibble::tibble(
    method = method,
    Sweeps = json$summary$sweep$sweep_pass_count,
    `Final transit (nm)` = format_distance(final_transit),
    `Transit improvement (nm)` = format_distance(improvement),
    `Relative improvement (%)` = format_num(relative_improvement, 1),
    `Stations moved` = sprintf(
      "%s/%s/%s",
      format_num(moved$mean, 1),
      format_num(moved$median, 1),
      ifelse(is.null(moved$max) || is.na(moved$max), "---", as.character(moved$max))
    ),
    `Accepted/capacity solves` = sprintf("%s/%s", accepted, capacity_solves),
    `Boundary changes` = json$summary$sweep$total_boundary_changes,
    `MIP solves` = json$summary$sweep$total_mip_solves,
    Runtime = format_runtime(as.numeric(json$summary$runtime_seconds$grandtotal))
  )
}

rows <- lapply(notation_rows$method, refinement_row)
refinement_tbl <- dplyr::bind_rows(rows) %>%
  left_join(notation_rows, by = "method") %>%
  select(
    Notation,
    Variant,
    Sweeps,
    `Final transit (nm)`,
    `Transit improvement (nm)`,
    `Relative improvement (%)`,
    `Stations moved`,
    `Accepted/capacity solves`,
    `Boundary changes`,
    `MIP solves`,
    Runtime
  )

trajectory_tbl <- lapply(notation_rows$method, trajectory_rows) %>%
  dplyr::bind_rows() %>%
  left_join(notation_rows, by = "method")

trajectory_tbl$Notation <- factor(
  trajectory_tbl$Notation,
  levels = notation_rows$Notation
)

plot_a <- ggplot(
  trajectory_tbl,
  aes(x = sweep, y = transit_nm, color = Notation, group = Notation)
) +
  geom_line(linewidth = 0.8) +
  geom_point(size = 2) +
  geom_text(
    aes(label = point_label),
    nudge_y = 95,
    size = 2.5,
    lineheight = 0.9,
    color = "black",
    show.legend = FALSE,
    na.rm = TRUE
  ) +
  expand_limits(y = max(trajectory_tbl$transit_nm, na.rm = TRUE) + 250) +
  scale_x_continuous(breaks = sort(unique(trajectory_tbl$sweep))) +
  labs(
    title = "A: Transit Distance",
    x = "Sweep",
    y = "Transit distance (nm)",
    color = "Variant"
  ) +
  theme_bw(base_size = 12) +
  theme(
    legend.position = "bottom",
    plot.title = element_text(face = "bold"),
    panel.grid.minor = element_blank()
  )

plot_b <- ggplot(
  trajectory_tbl,
  aes(x = sweep, y = relative_improvement_percent, color = Notation, group = Notation)
) +
  geom_hline(yintercept = 0, color = "grey65", linewidth = 0.4) +
  geom_line(linewidth = 0.8) +
  geom_point(size = 2) +
  scale_x_continuous(breaks = sort(unique(trajectory_tbl$sweep))) +
  labs(
    title = "B: Relative Improvement",
    x = "Sweep",
    y = "Improvement from initial transit (%)",
    color = "Variant"
  ) +
  theme_bw(base_size = 12) +
  theme(
    legend.position = "bottom",
    plot.title = element_text(face = "bold"),
    panel.grid.minor = element_blank()
  )

refinement_plot <- cowplot::plot_grid(
  plot_a + theme(legend.position = "none"),
  plot_b + theme(legend.position = "none"),
  ncol = 2,
  rel_widths = c(1, 1)
)

legend <- cowplot::get_legend(plot_a)
refinement_plot <- cowplot::plot_grid(
  refinement_plot,
  legend,
  ncol = 1,
  rel_heights = c(1, 0.12)
)

plot_file <- file.path(sol_dir, "refinement_transit_sweeps.png")
ggsave(plot_file, plot = refinement_plot, width = 11, height = 5.5, dpi = 150)
cat(sprintf("Plot saved to: %s\n\n", plot_file))

metadata_tbl <- lapply(notation_rows$method, function(method) {
  json <- read_result_json(method)
  if (is.null(json)) {
    return(NULL)
  }
  tibble::tibble(
    method = method,
    mip_time_limit_seconds = json$metadata$mip_time_limit_seconds,
    max_iterations = json$metadata$max_iterations,
    global_time_limit_seconds = json$metadata$global_time_limit_seconds
  )
}) %>%
  dplyr::bind_rows()

latex_table <- knitr::kable(
  refinement_tbl,
  format = "latex",
  booktabs = TRUE,
  escape = FALSE,
  linesep = "",
  label = "refinement-summary",
  caption = "Refinement outcomes by initialization strategy. Distances report transit distance only; the common station towing distance is excluded. Improvement is relative to the initial segmented baseline for each strategy. Stations moved are reported as mean/median/max per sweep pass, and MIP solves are local two-segment C-MIP boundary solves."
)

cat(latex_table)
cat("\n\n")

if (nrow(metadata_tbl) > 0) {
  unique_limits <- unique(metadata_tbl$mip_time_limit_seconds)
  unique_max_iterations <- unique(metadata_tbl$max_iterations)
  unique_global_limits <- unique(metadata_tbl$global_time_limit_seconds)

  cat("\\noindent ")
  cat(sprintf(
    "All refinement runs use $L_{2\\text{seg}}=%s$ seconds, a maximum of %s sweep iterations, and a global workflow limit of %s hours.",
    paste(unique_limits, collapse = "/"),
    paste(unique_max_iterations, collapse = "/"),
    paste(format(round(unique_global_limits / 3600, 1), trim = TRUE), collapse = "/")
  ))
  cat("\n")
}

cat("\\noindent In Figure~\\ref{fig:refinement-transit-sweeps}, point labels report the sweep-level diagnostics: moved is the number of stations reassigned across segment boundaries, and accepted is the number of accepted boundary-improvement attempts divided by all boundary attempts in that sweep.\n")
