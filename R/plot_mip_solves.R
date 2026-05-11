#!/usr/bin/env Rscript
# plot_mip_solves.R
#
# DB-based replacement for plot_mip_runtime.R.
# Reads the mip_solves table from solution.db and produces three panels:
#   A — ECDF of solve times by segment model (optimal vs timed-out)
#   B — Runtime boxplot + jitter by segment model
#   C — Station count vs solve time scatter with OLS trend lines
#
# Usage (from project root):
#   Rscript R/plot_mip_solves.R
#   Rscript R/plot_mip_solves.R --output sol/mip_solves.png

source("R/gsp_db.R")
source("R/gsp_plot_utils.R")

if (!requireNamespace("cowplot", quietly = TRUE)) {
  stop("Missing required R package: cowplot", call. = FALSE)
}

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------

parse_args <- function(args) {
  out <- list(
    gsp_db        = "dat/gsp.db",
    solution_db   = "dat/solution.db",
    output        = "sol/mip_solves.png",
    skip_existing = "false"
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

parse_bool <- function(x) {
  v <- tolower(trimws(x))
  if (v %in% c("1", "true",  "t", "yes", "y")) return(TRUE)
  if (v %in% c("0", "false", "f", "no",  "n")) return(FALSE)
  stop(sprintf("Invalid boolean value: %s", x), call. = FALSE)
}

# ---------------------------------------------------------------------------
# Labels and palettes
# ---------------------------------------------------------------------------

phase_code_label <- function(code) {
  switch(code,
    C = "Construction",
    S = "Segmentation",
    R = "Refinement",
    code
  )
}

segment_model_label <- function(m) {
  switch(m,
    `0seg` = "No-port MIP",
    `1seg` = "1-seg boundary",
    `2seg` = "2-seg boundary",
    Xseg  = "Fixed-port MIP",
    m
  )
}

# Ordered levels: construction-like first, then boundary models
model_levels  <- c("0seg", "Xseg", "1seg", "2seg")
model_labels  <- vapply(model_levels, segment_model_label, character(1L))
model_palette <- c(
  `0seg` = "#7570b3",
  Xseg   = "#1b9e77",
  `1seg` = "#d95f02",
  `2seg` = "#e7298a"
)

gap_palette <- c("TRUE"  = "#2166ac", "FALSE" = "#d73027")
gap_shapes  <- c("TRUE"  = 16L,       "FALSE" = 4L)
gap_labels  <- c("TRUE"  = "Gap = 0 (optimal)", "FALSE" = "Gap > 0 (timed-out)")

ref_times <- data.frame(
  t     = c(60, 120, 180),
  label = c("60 s", "120 s", "180 s")
)

# ---------------------------------------------------------------------------
# Build plots
# ---------------------------------------------------------------------------

build_plots <- function(df) {

  df$gap_zero      <- !is.na(df$gap_percent) & df$gap_percent == 0
  df$gap_f         <- factor(df$gap_zero)
  df$segment_model <- factor(df$segment_model, levels = model_levels,
                             labels = model_labels)

  # ---- Panel A: ECDF -------------------------------------------------------
  p_a <- ggplot2::ggplot(df,
    ggplot2::aes(x = runtime_seconds,
                 color    = segment_model,
                 linetype = gap_f,
                 group    = interaction(segment_model, gap_f))
  ) +
    ggplot2::geom_vline(
      data = ref_times,
      ggplot2::aes(xintercept = t),
      color = "grey60", linetype = "dotted", linewidth = 0.5,
      inherit.aes = FALSE
    ) +
    ggplot2::geom_text(
      data = ref_times,
      ggplot2::aes(x = t, label = label),
      y = 0.04, hjust = -0.1, vjust = 0, angle = 90,
      color = "grey45", size = 2.8, inherit.aes = FALSE
    ) +
    ggplot2::stat_ecdf(geom = "step", linewidth = 0.85, pad = FALSE) +
    ggplot2::scale_color_manual(
      name   = "Model",
      values = stats::setNames(model_palette, model_labels)
    ) +
    ggplot2::scale_linetype_manual(
      name   = "Optimality",
      values = c("TRUE" = "solid", "FALSE" = "dashed"),
      labels = gap_labels
    ) +
    ggplot2::scale_x_log10(
      breaks = scales::trans_breaks("log10", function(x) 10^x),
      labels = scales::trans_format("log10", scales::math_format(10^.x))
    ) +
    ggplot2::scale_y_continuous(
      labels = scales::percent_format(accuracy = 1),
      breaks = seq(0, 1, 0.1)
    ) +
    ggplot2::labs(
      title    = "A \u2014 Solve-time ECDF",
      subtitle = "Solid = gap 0 (optimal)  \u00b7  dashed = gap > 0 (timed-out)",
      x = "Solve time (s, log scale)",
      y = "Cumulative fraction"
    ) +
    ggplot2::theme(
      legend.position  = "right",
      panel.grid.minor = ggplot2::element_blank()
    )

  # ---- Panel B: Boxplot + jitter by segment model --------------------------
  p_b <- ggplot2::ggplot(df,
    ggplot2::aes(x = segment_model, y = runtime_seconds,
                 fill = segment_model)
  ) +
    ggplot2::geom_hline(
      data = ref_times,
      ggplot2::aes(yintercept = t),
      color = "grey60", linetype = "dotted", linewidth = 0.5,
      inherit.aes = FALSE
    ) +
    ggplot2::geom_text(
      data = ref_times,
      ggplot2::aes(y = t, label = label),
      x = 0.4, hjust = 0, vjust = -0.3,
      color = "grey45", size = 2.8, inherit.aes = FALSE
    ) +
    ggplot2::geom_boxplot(
      outlier.shape = NA, width = 0.55,
      alpha = 0.45, linewidth = 0.4
    ) +
    ggplot2::geom_jitter(
      ggplot2::aes(color = gap_f, shape = gap_f),
      width = 0.18, size = 1.1, alpha = 0.55
    ) +
    ggplot2::scale_fill_manual(
      name = "Model", values = stats::setNames(model_palette, model_labels)
    ) +
    ggplot2::scale_color_manual(
      name = "Optimality", values = gap_palette, labels = gap_labels
    ) +
    ggplot2::scale_shape_manual(
      name = "Optimality", values = gap_shapes, labels = gap_labels
    ) +
    ggplot2::scale_y_log10(
      breaks = scales::trans_breaks("log10", function(x) 10^x),
      labels = scales::trans_format("log10", scales::math_format(10^.x))
    ) +
    ggplot2::labs(
      title    = "B \u2014 Solve Time by Model",
      subtitle = "Boxes = IQR  \u00b7  blue = optimal  \u00b7  red = timed-out",
      x = NULL,
      y = "Solve time (s, log scale)"
    ) +
    ggplot2::theme(
      legend.position  = "right",
      panel.grid.minor = ggplot2::element_blank(),
      axis.text.x = ggplot2::element_text(angle = 15, hjust = 1)
    )

  # ---- Panel C: Station count vs solve time --------------------------------
  p_c <- ggplot2::ggplot(df,
    ggplot2::aes(x = station_count, y = runtime_seconds,
                 color = segment_model, shape = gap_f)
  ) +
    ggplot2::geom_hline(
      data = ref_times,
      ggplot2::aes(yintercept = t),
      color = "grey60", linetype = "dotted", linewidth = 0.5,
      inherit.aes = FALSE
    ) +
    ggplot2::geom_text(
      data = ref_times,
      ggplot2::aes(y = t, label = label),
      x = -Inf, hjust = -0.1, vjust = -0.35,
      color = "grey45", size = 2.8, inherit.aes = FALSE
    ) +
    ggplot2::geom_point(alpha = 0.50, size = 1.4) +
    ggplot2::geom_smooth(
      data = df[ave(df$station_count, df$segment_model, FUN = length) >= 2L, ],
      ggplot2::aes(x = station_count, y = runtime_seconds,
                   group = segment_model, color = segment_model),
      method = "lm", formula = y ~ x,
      se = FALSE, linewidth = 0.7, alpha = 0.85,
      inherit.aes = FALSE
    ) +
    ggplot2::scale_color_manual(
      name = "Model", values = stats::setNames(model_palette, model_labels)
    ) +
    ggplot2::scale_shape_manual(
      name = "Optimality", values = gap_shapes, labels = gap_labels
    ) +
    ggplot2::scale_x_continuous(labels = scales::comma) +
    ggplot2::scale_y_log10(
      breaks = scales::trans_breaks("log10", function(x) 10^x),
      labels = scales::trans_format("log10", scales::math_format(10^.x))
    ) +
    ggplot2::labs(
      title    = "C \u2014 Problem Size vs Solve Time",
      subtitle = "Each point is one MIP solve  \u00b7  lines are OLS fits per model (log-y scale)",
      x = "Station count",
      y = "Solve time (s, log scale)"
    ) +
    ggplot2::theme(
      legend.position  = "right",
      panel.grid.minor = ggplot2::element_blank()
    )

  list(p_a = p_a, p_b = p_b, p_c = p_c)
}

# ---------------------------------------------------------------------------
# Summary to console
# ---------------------------------------------------------------------------

print_summary <- function(df) {
  df$gap_zero <- !is.na(df$gap_percent) & df$gap_percent == 0
  tbl <- do.call(rbind, by(df, df$segment_model, function(g) {
    data.frame(
      model    = g$segment_model[1],
      n        = nrow(g),
      pct_opt  = round(100 * mean(g$gap_zero,        na.rm = TRUE), 1),
      mean_rt  = round(mean(g$runtime_seconds,        na.rm = TRUE), 2),
      max_rt   = round(max(g$runtime_seconds,         na.rm = TRUE), 2),
      mean_gap = round(mean(g$gap_percent,            na.rm = TRUE), 2),
      stringsAsFactors = FALSE
    )
  }, simplify = FALSE))
  tbl <- tbl[order(tbl$model), ]
  cat(sprintf("\n%-8s  %5s  %7s  %8s  %8s  %8s\n",
              "model", "n", "pct_opt", "mean_rt", "max_rt", "mean_gap%"))
  cat(strrep("-", 52), "\n")
  for (i in seq_len(nrow(tbl))) {
    r <- tbl[i, ]
    cat(sprintf("%-8s  %5d  %6.1f%%  %8.2f  %8.2f  %8.2f\n",
                r$model, r$n, r$pct_opt, r$mean_rt, r$max_rt, r$mean_gap))
  }
  cat("\n")
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

main <- function() {
  load_gsp_plot_packages()
  opt <- parse_args(commandArgs(trailingOnly = TRUE))

  if (parse_bool(opt$skip_existing) && file.exists(opt$output)) {
    message("Skipping (file exists): ", opt$output)
    return(invisible(FALSE))
  }

  con <- connect_gsp_db(opt$gsp_db, opt$solution_db)
  on.exit(DBI::dbDisconnect(con), add = TRUE)

  df <- db_read(con, "
    SELECT
      phase_code,
      segment_model,
      station_count,
      node_count,
      model_variable_count,
      model_constraint_count,
      runtime_seconds,
      gap_percent
    FROM solution.mip_solves
    WHERE runtime_seconds IS NOT NULL
    ORDER BY phase_code, segment_model
  ")

  if (nrow(df) == 0L) {
    message("No rows in mip_solves — nothing to plot.")
    return(invisible(FALSE))
  }

  # Drop rows that would produce -Inf on log scales
  df <- df[!is.na(df$runtime_seconds) & df$runtime_seconds > 0, , drop = FALSE]
  df <- df[!is.na(df$station_count)   & df$station_count   > 0, , drop = FALSE]

  message(sprintf("Read %d MIP solve records", nrow(df)))
  print_summary(df)

  # Drop models with no data at all
  present_models <- intersect(model_levels, unique(df$segment_model))
  df <- df[df$segment_model %in% present_models, , drop = FALSE]

  plots <- build_plots(df)

  combined <- cowplot::plot_grid(plots$p_a, plots$p_b, plots$p_c, ncol = 3L,
                                 align = "h", axis = "tb")

  dir.create(dirname(opt$output), showWarnings = FALSE, recursive = TRUE)
  ggplot2::ggsave(opt$output, combined, width = 22, height = 7, dpi = 150,
                  bg = "white")
  message("Wrote ", opt$output)
  invisible(TRUE)
}

main()

