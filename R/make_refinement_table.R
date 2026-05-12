#!/usr/bin/env Rscript
# make_refinement_table.R
#
# Builds a LaTeX refinement summary table and transit sweep plot from
# dat/solution.db. Run from the project root:
#   Rscript R/make_refinement_table.R
#   Rscript R/make_refinement_table.R --output Paper/tables/refinement.tex

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

load_required_packages(c("dplyr", "ggplot2", "knitr", "scales", "tibble"))

parse_args <- function(args) {
  out <- list(
    gsp_db = "dat/gsp.db",
    solution_db = "dat/solution.db",
    method = "all",
    l2seg = "all",
    output = NULL,
    plot_output = "sol/refinement_transit_sweeps.png"
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

parse_list <- function(x) {
  if (is.null(x) || !nzchar(x) || x %in% c("all", "ALL", "*")) return(NULL)
  trimws(strsplit(x, ",", fixed = TRUE)[[1]])
}

parse_l2seg <- function(x) {
  if (is.null(x) || !nzchar(x) || x %in% c("all", "ALL", "*")) return(NULL)
  if (x %in% c("inf", "Inf", "uncapped")) return(NA_integer_)
  vals <- suppressWarnings(as.integer(trimws(strsplit(x, ",", fixed = TRUE)[[1]])))
  if (any(is.na(vals))) stop("--l2seg must be all, uncapped, or comma-separated integer seconds.", call. = FALSE)
  vals
}

format_runtime <- function(seconds) {
  if (is.null(seconds) || length(seconds) == 0L || is.na(seconds)) return("---")
  if (seconds >= 7200) return(sprintf("%.1f h", seconds / 3600))
  sprintf("%.1f min", seconds / 60)
}

format_distance <- function(value) {
  if (is.null(value) || length(value) == 0L || is.na(value)) return("---")
  sprintf("%.2f", as.numeric(value))
}

format_num <- function(value, digits = 1) {
  if (is.null(value) || length(value) == 0L || is.na(value)) return("---")
  sprintf(paste0("%.", digits, "f"), as.numeric(value))
}

format_l2seg <- function(l2seg) {
  ifelse(is.na(l2seg), "$\\infty$", as.character(as.integer(l2seg)))
}

method_label <- function(method) {
  switch(method,
    nn = "MH-NN",
    ci = "MH-CI",
    ge = "MH-GE",
    noport = "MH-OPT",
    method
  )
}

method_variant <- function(method) {
  switch(method,
    nn = "MH with nearest-neighbor",
    ci = "MH with cheapest-insertion",
    ge = "MH with greedy-edge",
    noport = "MH with NP-based initialization",
    method
  )
}

method_rank <- function(method) {
  order <- c("nn", "ci", "ge", "noport")
  match(method, order, nomatch = length(order) + 1L)
}

read_refinement_pass_data <- function(con, methods = NULL, l2seg_values = NULL) {
  filters <- "WHERE r.phase = 'refinement'"
  params <- list()

  if (!is.null(methods) && length(methods) > 0L) {
    ph <- paste(rep("?", length(methods)), collapse = ",")
    filters <- paste(filters, sprintf("AND r.method IN (%s)", ph))
    params <- c(params, as.list(methods))
  }

  if (!is.null(l2seg_values)) {
    if (length(l2seg_values) == 1L && is.na(l2seg_values)) {
      filters <- paste(filters, "AND r.l2seg_timeout_seconds IS NULL")
    } else {
      ph <- paste(rep("?", length(l2seg_values)), collapse = ",")
      filters <- paste(filters, sprintf("AND r.l2seg_timeout_seconds IN (%s)", ph))
      params <- c(params, as.list(l2seg_values))
    }
  }

  sql <- sprintf("
    SELECT
      r.method,
      r.l2seg_timeout_seconds AS l2seg,
      rp.run_id AS refinement_run_id,
      rp.solution_run_id,
      rp.pass_number,
      rp.changed,
      COALESCE(rp.stations_moved, 0) AS stations_moved,
      COALESCE(rp.boundary_attempts, 0) AS boundary_attempts,
      COALESCE(rp.boundary_changes, 0) AS boundary_changes,
      COALESCE(rp.mip_solves, 0) AS mip_solves,
      rp.runtime_seconds AS pass_runtime_seconds,
      r.runtime_seconds AS run_runtime_seconds,
      d.transit_nm
    FROM solution.refinement_passes rp
    JOIN solution.runs r ON r.run_id = rp.solution_run_id
    LEFT JOIN solution.distance d
      ON d.run_id = rp.solution_run_id
     AND d.segment IS NULL
    %s
    ORDER BY r.method, r.l2seg_timeout_seconds, rp.pass_number
  ", filters)

  db_read(con, sql, if (length(params) > 0L) params else NULL)
}

add_derived <- function(df) {
  if (nrow(df) == 0L) return(df)
  df$series <- paste(df$method, ifelse(is.na(df$l2seg), "inf", df$l2seg), sep = ":")
  df <- df[order(df$series, df$pass_number), , drop = FALSE]

  init_transit <- tapply(df$transit_nm, df$series, function(x) x[which(!is.na(x))[1]])
  df$initial_transit <- as.numeric(init_transit[df$series])
  df$relative_improvement_percent <- 100 * (df$initial_transit - df$transit_nm) / df$initial_transit
  df$Notation <- vapply(df$method, method_label, character(1L))
  df$Variant <- vapply(df$method, method_variant, character(1L))
  df$l2seg_label <- format_l2seg(df$l2seg)
  df
}

build_refinement_table <- function(df) {
  pass_rows <- df %>% filter(pass_number > 0)
  final_rows <- df %>%
    group_by(series) %>%
    filter(pass_number == max(pass_number, na.rm = TRUE)) %>%
    ungroup()

  pass_summary <- pass_rows %>%
    group_by(series) %>%
    summarise(
      moved_mean = mean(stations_moved, na.rm = TRUE),
      moved_median = median(stations_moved, na.rm = TRUE),
      moved_max = max(stations_moved, na.rm = TRUE),
      total_boundary_attempts = sum(boundary_attempts, na.rm = TRUE),
      total_boundary_changes = sum(boundary_changes, na.rm = TRUE),
      total_mip_solves = sum(mip_solves, na.rm = TRUE),
      .groups = "drop"
    )

  final_rows %>%
    left_join(pass_summary, by = "series") %>%
    mutate(
      improvement_nm = initial_transit - transit_nm,
      Runtime = vapply(run_runtime_seconds, format_runtime, character(1L)),
      `Stations moved` = sprintf(
        "%s/%s/%s",
        vapply(moved_mean, format_num, character(1L), digits = 1),
        vapply(moved_median, format_num, character(1L), digits = 1),
        ifelse(is.na(moved_max), "---", as.character(as.integer(moved_max)))
      ),
      `Boundary changes/attempts` = sprintf("%d/%d", total_boundary_changes, total_boundary_attempts)
    ) %>%
    transmute(
      method,
      l2seg,
      Notation,
      `$L_{2\\text{seg}}$ (s)` = l2seg_label,
      Sweeps = pass_number,
      `Final transit (nm)` = vapply(transit_nm, format_distance, character(1L)),
      `Transit improvement (nm)` = vapply(improvement_nm, format_distance, character(1L)),
      `Relative improvement (%)` = vapply(relative_improvement_percent, format_num, character(1L), digits = 1),
      `Stations moved`,
      `Boundary changes/attempts`,
      `MIP solves` = total_mip_solves,
      Runtime
    ) %>%
    arrange(method_rank(method), is.na(l2seg), l2seg) %>%
    select(-method, -l2seg)
}

make_transit_plot <- function(df, output) {
  if (is.null(output) || !nzchar(output) || nrow(df) == 0L) return(invisible(FALSE))

  plot_df <- df %>%
    mutate(
      Label = ifelse(is.na(l2seg), Notation, sprintf("%s (%ss)", Notation, as.integer(l2seg))),
      Label = factor(Label, levels = unique(Label))
    )

  p <- ggplot(plot_df, aes(x = pass_number, y = transit_nm, color = Label, group = Label)) +
    geom_line(linewidth = 0.7, alpha = 0.85) +
    geom_point(aes(size = ifelse(pass_number == 0, NA_real_, stations_moved)), alpha = 0.75, na.rm = TRUE) +
    scale_size_continuous(name = "Stations moved", range = c(1.5, 5), breaks = scales::breaks_pretty(n = 4)) +
    labs(
      x = "Sweep",
      y = "Transit distance (nm)",
      color = "Variant",
      title = "Refinement Transit Sweeps"
    ) +
    theme_bw(base_size = 11) +
    theme(
      legend.position = "bottom",
      panel.grid.minor = element_blank(),
      plot.title = element_text(face = "bold")
    )

  dir.create(dirname(output), recursive = TRUE, showWarnings = FALSE)
  ggplot2::ggsave(output, p, width = 11, height = 5.5, dpi = 150, bg = "white")
  message("Plot saved to: ", output)
  invisible(TRUE)
}

main <- function() {
  opt <- parse_args(commandArgs(trailingOnly = TRUE))
  methods <- parse_list(opt$method)
  l2seg_values <- parse_l2seg(opt$l2seg)

  con <- connect_gsp_db(opt$gsp_db, opt$solution_db)
  on.exit(DBI::dbDisconnect(con), add = TRUE)

  df <- read_refinement_pass_data(con, methods, l2seg_values) %>% add_derived()
  if (nrow(df) == 0L) {
    message("No refinement rows found in solution.db; skipping refinement table and plot.")
    quit(save = "no", status = 0)
  }

  refinement_tbl <- build_refinement_table(df)
  make_transit_plot(df, opt$plot_output)

  latex_table <- knitr::kable(
    refinement_tbl,
    format = "latex",
    booktabs = TRUE,
    escape = FALSE,
    linesep = "",
    label = "refinement-summary",
    caption = "Refinement outcomes by initialization strategy. Distances report transit distance only; the common station towing distance is excluded. Improvement is relative to the initial segmented baseline for each strategy. Stations moved are reported as mean/median/max per sweep pass, and MIP solves are local two-segment C-MIP boundary solves."
  )

  text <- paste0(
    latex_table,
    "\n\n\\noindent In Figure~\\ref{fig:refinement-transit-sweeps}, point size reports the number of stations reassigned across segment boundaries in each sweep. Boundary changes/attempts counts accepted boundary changes over all boundary attempts.\n"
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
