#!/usr/bin/env Rscript
# plot_refinement_sweep.R
#
# DB-based replacement for plot_sweep_summary_panels.R.
# Produces two kinds of output:
#   1. Combined plot  — one series per method     → --output (default sol/refinement_sweep.png)
#   2. Per-method plots — one series per L2seg    → sol/<method>/refinement_sweep.png
#
# Usage (from project root):
#   Rscript R/plot_refinement_sweep.R
#   Rscript R/plot_refinement_sweep.R --method nn,ci,ge --l2seg all

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
    method        = "all",
    l2seg         = "all",    # for combined plot: "all" | "uncapped" | "10,30,60"
    output        = "sol/refinement_sweep.png",
    per_method    = "true",
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

parse_str_list <- function(x) {
  if (is.null(x) || !nzchar(x) || x %in% c("all", "ALL", "*")) return(NULL)
  trimws(strsplit(x, ",", fixed = TRUE)[[1]])
}

parse_l2seg_filter <- function(x) {
  if (is.null(x) || x %in% c("all", "ALL", "*")) return(NULL)
  if (x %in% c("uncapped", "inf", "Inf")) return("uncapped")
  suppressWarnings(as.integer(trimws(strsplit(x, ",", fixed = TRUE)[[1]])))
}

# ---------------------------------------------------------------------------
# Palettes / labels
# ---------------------------------------------------------------------------

method_order  <- c("noport", "nn", "ge", "ci", "fixedport")
method_labels <- c(noport = "MH-OPT", nn = "MH-NN", ge = "MH-GE",
                   ci = "MH-CI", fixedport = "Fixed-port")
method_colors <- c(noport = "#355070", nn = "#6D597A",
                   ge     = "#B56576", ci = "#E56B6F",
                   fixedport = "#EAAC8B")

l2seg_label <- function(x) {
  if (is.na(x)) "\u221e s (uncapped)" else sprintf("%d s", as.integer(x))
}

# ---------------------------------------------------------------------------
# DB read
# ---------------------------------------------------------------------------

read_sweep_data <- function(con, methods = NULL, l2seg_filter = NULL) {
  filters <- "WHERE 1=1"
  params  <- list()

  if (!is.null(methods) && length(methods) > 0L) {
    ph      <- paste(rep("?", length(methods)), collapse = ",")
    filters <- paste(filters, sprintf("AND r.method IN (%s)", ph))
    params  <- c(params, as.list(methods))
  }
  if (identical(l2seg_filter, "uncapped")) {
    filters <- paste(filters, "AND r.l2seg_timeout_seconds IS NULL")
  } else if (!is.null(l2seg_filter) && length(l2seg_filter) > 0L) {
    ph      <- paste(rep("?", length(l2seg_filter)), collapse = ",")
    filters <- paste(filters, sprintf("AND r.l2seg_timeout_seconds IN (%s)", ph))
    params  <- c(params, as.list(l2seg_filter))
  }

  sql <- sprintf("
    SELECT
      r.method,
      r.l2seg_timeout_seconds        AS l2seg,
      rp.pass_number,
      rp.changed,
      COALESCE(rp.stations_moved, 0) AS stations_moved,
      COALESCE(rp.mip_solves, 0)     AS mip_solve_count,
      rp.runtime_seconds             AS pass_runtime,
      COALESCE(rp.boundary_attempts, 0) AS boundary_attempts,
      COALESCE(rp.boundary_changes,  0) AS boundary_changes,
      d.transit_nm
    FROM solution.refinement_passes rp
    JOIN solution.runs r ON r.run_id = rp.solution_run_id
    LEFT JOIN solution.distance d
      ON d.run_id = rp.solution_run_id AND d.segment IS NULL
    %s
    ORDER BY r.method, r.l2seg_timeout_seconds, rp.pass_number
  ", filters)

  db_read(con, sql, if (length(params) > 0L) params else NULL)
}

# ---------------------------------------------------------------------------
# Derived columns
# ---------------------------------------------------------------------------

add_derived <- function(df) {
  df$series <- paste0(df$method, "_",
                      ifelse(is.na(df$l2seg), "inf", as.character(df$l2seg)))
  # Sort so within-series diffs are in pass order
  df <- df[order(df$series, df$pass_number), , drop = FALSE]

  init_transit <- tapply(df$transit_nm, df$series, function(x) x[1])
  df$init_transit <- init_transit[df$series]

  df$rel_improvement_pct <- ifelse(
    is.na(df$init_transit) | df$init_transit == 0,
    NA_real_,
    100 * (df$init_transit - df$transit_nm) / df$init_transit
  )
  # Per-pass improvement in nm (positive = transit dropped this pass)
  df$pass_improvement_nm <- ave(
    df$transit_nm, df$series,
    FUN = function(x) c(NA_real_, -diff(x))
  )
  df$boundary_change_rate <- ifelse(
    df$boundary_attempts > 0,
    df$boundary_changes / df$boundary_attempts,
    NA_real_
  )
  df$l2seg_label <- vapply(df$l2seg, l2seg_label, character(1L))
  df
}

# ---------------------------------------------------------------------------
# Six-panel list builder — returns named list(p_a … p_f, title)
# Generic: caller supplies `color_var`, `palette`, `plot_title`
# ---------------------------------------------------------------------------

build_six_panels <- function(df, color_var, palette, plot_title) {
  changed  <- df[as.logical(df$changed) & df$pass_number > 0, , drop = FALSE]

  df[[color_var]]      <- factor(df[[color_var]],      levels = names(palette))
  if (nrow(changed) > 0)
    changed[[color_var]] <- factor(changed[[color_var]], levels = names(palette))

  no_legend <- ggplot2::theme(panel.grid.minor = ggplot2::element_blank(),
                               legend.position  = "none")

  # ---- A: Stations moved ---------------------------------------------------
  p_a <- if (nrow(changed) > 0) {
    ggplot2::ggplot(changed,
      ggplot2::aes(x = .data[[color_var]], y = stations_moved,
                   fill = .data[[color_var]])
    ) +
      ggplot2::geom_boxplot(width = 0.6, alpha = 0.82, outlier.shape = NA,
                            linewidth = 0.4) +
      ggplot2::scale_fill_manual(values = palette) +
      ggplot2::labs(title = "A \u2014 Stations moved per pass",
                    x = NULL, y = "Stations moved") + no_legend
  } else {
    cowplot::ggdraw() + cowplot::draw_label("No changed passes", size = 10)
  }

  # ---- B: Transit distance by pass ----------------------------------------
  p_b <- ggplot2::ggplot(df,
    ggplot2::aes(x = pass_number, y = transit_nm,
                 color = .data[[color_var]], group = series)
  ) +
    ggplot2::geom_line(linewidth = 0.9) + ggplot2::geom_point(size = 1.8) +
    ggplot2::scale_color_manual(values = palette, name = NULL) +
    ggplot2::scale_x_continuous(breaks = scales::pretty_breaks()) +
    ggplot2::labs(title = "B \u2014 Transit distance by sweep",
                  x = "Pass", y = "Transit (nm)") +
    ggplot2::theme(panel.grid.minor = ggplot2::element_blank(),
                   legend.position = "none")

  # ---- C: Relative improvement by pass ------------------------------------
  p_c <- ggplot2::ggplot(df[!is.na(df$rel_improvement_pct), , drop = FALSE],
    ggplot2::aes(x = pass_number, y = rel_improvement_pct,
                 color = .data[[color_var]], group = series)
  ) +
    ggplot2::geom_line(linewidth = 0.9) + ggplot2::geom_point(size = 1.8) +
    ggplot2::scale_color_manual(values = palette, name = NULL) +
    ggplot2::scale_x_continuous(breaks = scales::pretty_breaks()) +
    ggplot2::labs(title = "C \u2014 Relative improvement by sweep",
                  x = "Pass", y = "Improvement (%)") + no_legend

  # ---- D: Pass runtime (log) ----------------------------------------------
  p_d <- if (nrow(changed) > 0 &&
             any(!is.na(changed$pass_runtime) & changed$pass_runtime > 0)) {
    ggplot2::ggplot(
      changed[!is.na(changed$pass_runtime) & changed$pass_runtime > 0, ],
      ggplot2::aes(x = .data[[color_var]], y = pass_runtime,
                   fill = .data[[color_var]])
    ) +
      ggplot2::geom_boxplot(width = 0.6, alpha = 0.82, outlier.shape = NA,
                            linewidth = 0.4) +
      ggplot2::scale_fill_manual(values = palette) +
      ggplot2::scale_y_log10(
        breaks = scales::trans_breaks("log10", function(x) 10^x),
        labels = scales::trans_format("log10", scales::math_format(10^.x))
      ) +
      ggplot2::labs(title = "D \u2014 Pass runtime (s)",
                    x = NULL, y = "Runtime (s, log)") + no_legend
  } else {
    cowplot::ggdraw() + cowplot::draw_label("No runtime data", size = 10)
  }

  # ---- E: MIP solves per pass ---------------------------------------------
  p_e <- if (nrow(changed) > 0 && any(changed$mip_solve_count > 0)) {
    ggplot2::ggplot(changed[changed$mip_solve_count > 0, ],
      ggplot2::aes(x = .data[[color_var]], y = mip_solve_count,
                   fill = .data[[color_var]])
    ) +
      ggplot2::geom_boxplot(width = 0.6, alpha = 0.82, outlier.shape = NA,
                            linewidth = 0.4) +
      ggplot2::scale_fill_manual(values = palette) +
      ggplot2::labs(title = "E \u2014 MIP solves per pass",
                    x = NULL, y = "MIP solves") + no_legend
  } else {
    cowplot::ggdraw() + cowplot::draw_label("No MIP solve data", size = 10)
  }

  # ---- F: Boundary change rate --------------------------------------------
  p_f <- if (any(!is.na(df$boundary_change_rate))) {
    ggplot2::ggplot(df[!is.na(df$boundary_change_rate), ],
      ggplot2::aes(x = .data[[color_var]], y = boundary_change_rate,
                   fill = .data[[color_var]])
    ) +
      ggplot2::geom_boxplot(width = 0.6, alpha = 0.82, outlier.shape = NA,
                            linewidth = 0.4) +
      ggplot2::scale_fill_manual(values = palette) +
      ggplot2::scale_y_continuous(
        labels = scales::percent_format(accuracy = 1)) +
      ggplot2::labs(title = "F \u2014 Boundary change rate",
                    x = NULL, y = "Changes / attempts") + no_legend
  } else {
    cowplot::ggdraw() + cowplot::draw_label("No boundary data", size = 10)
  }

  list(p_a = p_a, p_b = p_b, p_c = p_c,
       p_d = p_d, p_e = p_e, p_f = p_f,
       title = plot_title)
}

# Assemble a six-panel list into a combined cowplot figure
assemble_six_panels <- function(panels) {
  top    <- cowplot::plot_grid(panels$p_a, panels$p_b, panels$p_c,
                               ncol = 3L, align = "h", axis = "tb")
  bottom <- cowplot::plot_grid(panels$p_d, panels$p_e, panels$p_f,
                               ncol = 3L, align = "h", axis = "tb")
  hdr <- cowplot::ggdraw() +
    cowplot::draw_label(panels$title, fontface = "bold", size = 14,
                        x = 0.5, hjust = 0.5)
  cowplot::plot_grid(hdr, top, bottom, ncol = 1L, rel_heights = c(0.06, 1, 1))
}

# ---------------------------------------------------------------------------
# Per-method pass statistics table panel (goes in position A for method plots)
# ---------------------------------------------------------------------------

make_pass_table_panel <- function(df, level_order, palette) {
  passes <- df[df$pass_number > 0, , drop = FALSE]
  passes <- passes[order(match(passes$l2seg_label, level_order),
                         passes$pass_number), , drop = FALSE]

  if (nrow(passes) == 0L) {
    return(cowplot::ggdraw() + cowplot::draw_label("No pass data", size = 10))
  }

  # Column names as plain strings (avoid \uxxxx-in-backticks parse error)
  col_l2seg <- "L\u2082seg"
  col_sd    <- "|S\u0394|"
  col_dnm   <- "\u0394nm"
  col_names <- c(col_l2seg, "#", "hr", "MIP", "Acc", col_sd, col_dnm)

  out_rows      <- list()
  row_fills     <- character()
  row_fontfaces <- character()   # "plain" or "bold" per row
  sep_after     <- integer()     # data-row indices after which to draw hline
  row_idx       <- 0L

  for (sv in level_order) {
    sub <- passes[passes$l2seg_label == sv, , drop = FALSE]
    if (nrow(sub) == 0L) next
    base_col <- palette[[sv]]

    # --- data rows for this l2seg ---
    for (i in seq_len(nrow(sub))) {
      r <- sub[i, , drop = FALSE]
      row_idx <- row_idx + 1L
      impr    <- r$pass_improvement_nm
      rt      <- r$pass_runtime
      row_df  <- data.frame(
        l2seg = sv,
        pass  = as.character(r$pass_number),
        rt    = if (is.na(rt)) "\u2014" else sprintf("%.2f", rt / 3600),
        mip   = as.character(r$mip_solve_count),
        acc   = as.character(r$boundary_changes),
        smov  = as.character(r$stations_moved),
        dnm   = if (is.na(impr)) "\u2014" else sprintf("%.0f", impr),
        stringsAsFactors = FALSE
      )
      names(row_df)         <- col_names
      out_rows[[row_idx]]   <- row_df
      row_fills[[row_idx]]  <- scales::alpha(base_col, 0.22)
      row_fontfaces[[row_idx]] <- "plain"
    }

    # --- subtotal row for this l2seg (bold) ---
    row_idx <- row_idx + 1L
    total_rt   <- sum(sub$pass_runtime,       na.rm = TRUE)
    total_impr <- sum(sub$pass_improvement_nm, na.rm = TRUE)
    sub_df <- data.frame(
      l2seg = sv,
      pass  = "\u03a3",
      rt     = sprintf("%.2f", sum(sub$pass_runtime,       na.rm = TRUE) / 3600),
      mip    = as.character(sum(sub$mip_solve_count,  na.rm = TRUE)),
      acc    = as.character(sum(sub$boundary_changes, na.rm = TRUE)),
      smov   = as.character(sum(sub$stations_moved,   na.rm = TRUE)),
      dnm    = sprintf("%.0f", total_impr),
      stringsAsFactors = FALSE
    )
    names(sub_df)           <- col_names
    out_rows[[row_idx]]     <- sub_df
    row_fills[[row_idx]]    <- scales::alpha(base_col, 0.45)
    row_fontfaces[[row_idx]] <- "bold"

    sep_after <- c(sep_after, row_idx)
  }

  # Remove the very last separator (no hline after final block)
  if (length(sep_after) > 0L) sep_after <- sep_after[-length(sep_after)]

  tbl <- do.call(rbind, out_rows)

  fill_mat     <- matrix(row_fills,     nrow = nrow(tbl), ncol = ncol(tbl))
  face_mat     <- matrix(row_fontfaces, nrow = nrow(tbl), ncol = ncol(tbl))
  th           <- table_theme(fill_mat, base_size = 7.5, fontface_matrix = face_mat)
  tg           <- gridExtra::tableGrob(tbl, rows = NULL, theme = th)

  # Horizontal separator lines between l2seg groups
  hline <- function() {
    grid::segmentsGrob(
      x0 = grid::unit(0, "npc"), x1 = grid::unit(1, "npc"),
      y0 = grid::unit(0, "npc"), y1 = grid::unit(0, "npc"),
      gp = grid::gpar(col = "grey50", lwd = 0.9)
    )
  }
  for (r in sep_after) {
    tg <- gtable::gtable_add_grob(tg, hline(),
                                  t = r + 1L, b = r + 1L,
                                  l = 1L, r = ncol(tg), z = Inf)
  }

  ggplot2::ggplot(data.frame(x = 0, y = 0), ggplot2::aes(x, y)) +
    ggplot2::annotation_custom(tg, xmin = 0.01, xmax = 0.99,
                                   ymin = 0.02, ymax = 0.97) +
    ggplot2::scale_x_continuous(limits = c(0, 1), expand = c(0, 0)) +
    ggplot2::scale_y_continuous(limits = c(0, 1), expand = c(0, 0)) +
    ggplot2::coord_cartesian(clip = "off") +
    ggplot2::labs(
      title    = "A \u2014 Pass statistics",
      subtitle = "runtime (hr) \u00b7 MIP solves \u00b7 accepted boundary moves \u00b7 stations moved \u00b7 transit improvement"
    ) +
    ggplot2::theme_void() +
    ggplot2::theme(
      plot.title    = ggplot2::element_text(size = 10, hjust = 0),
      plot.subtitle = ggplot2::element_text(size = 7.5, hjust = 0,
                                            color = "grey40",
                                            margin = ggplot2::margin(b = 3)),
      plot.title.position = "plot",
      plot.margin = ggplot2::margin(5, 5, 5, 5)
    )
}

# ---------------------------------------------------------------------------
# Combined-plot table: one total row per (method, l2seg), grouped by method
# ---------------------------------------------------------------------------

make_combined_table_panel <- function(df, keep_methods, palette) {
  passes <- df[df$pass_number > 0, , drop = FALSE]
  if (nrow(passes) == 0L) {
    return(cowplot::ggdraw() + cowplot::draw_label("No pass data", size = 10))
  }

  col_method <- "Method"
  col_l2seg  <- "L\u2082seg"
  col_sd     <- "|S\u0394|"
  col_dnm    <- "\u0394nm"
  col_dpct   <- "\u0394%"
  col_names  <- c(col_method, col_l2seg, "#", "hr", "MIP", "Acc", col_sd, col_dnm, col_dpct)

  out_rows  <- list()
  row_fills <- character()
  sep_after <- integer()
  row_idx   <- 0L

  for (m in keep_methods) {
    sub_m <- passes[passes$method == m, , drop = FALSE]
    if (nrow(sub_m) == 0L) next
    lbl      <- method_labels[[m]]
    base_col <- palette[[lbl]]

    # Sort l2segs: uncapped first, then ascending
    l2segs_m <- sort(unique(sub_m$l2seg[!is.na(sub_m$l2seg)]))
    has_unc   <- any(is.na(sub_m$l2seg))
    l2seg_ord <- c(if (has_unc) NA, l2segs_m)

    for (lv in l2seg_ord) {
      sub <- if (is.na(lv)) sub_m[is.na(sub_m$l2seg), , drop = FALSE]
             else             sub_m[!is.na(sub_m$l2seg) & sub_m$l2seg == lv, , drop = FALSE]
      if (nrow(sub) == 0L) next
      row_idx <- row_idx + 1L

      final_cum_pct <- sub$rel_improvement_pct[nrow(sub)]
      row_df <- data.frame(
        method = lbl,
        l2seg  = l2seg_label(lv),
        pass   = as.character(nrow(sub)),
        rt     = sprintf("%.2f", sum(sub$pass_runtime,        na.rm = TRUE) / 3600),
        mip    = as.character(sum(sub$mip_solve_count,        na.rm = TRUE)),
        acc    = as.character(sum(sub$boundary_changes,       na.rm = TRUE)),
        smov   = as.character(sum(sub$stations_moved,         na.rm = TRUE)),
        dnm    = sprintf("%.0f", sum(sub$pass_improvement_nm, na.rm = TRUE)),
        dpct   = if (is.na(final_cum_pct)) "\u2014"
                 else sprintf("%.1f%%", final_cum_pct),
        stringsAsFactors = FALSE
      )
      names(row_df)              <- col_names
      out_rows[[row_idx]]        <- row_df
      row_fills[[row_idx]]       <- scales::alpha(base_col, 0.25)
    }
    sep_after <- c(sep_after, row_idx)
  }
  if (length(sep_after) > 0L) sep_after <- sep_after[-length(sep_after)]

  tbl      <- do.call(rbind, out_rows)
  fill_mat <- matrix(row_fills, nrow = nrow(tbl), ncol = ncol(tbl))
  th       <- table_theme(fill_mat, base_size = 7.5)
  tg       <- gridExtra::tableGrob(tbl, rows = NULL, theme = th)

  hline <- function() {
    grid::segmentsGrob(
      x0 = grid::unit(0, "npc"), x1 = grid::unit(1, "npc"),
      y0 = grid::unit(0, "npc"), y1 = grid::unit(0, "npc"),
      gp = grid::gpar(col = "grey50", lwd = 0.9)
    )
  }
  for (r in sep_after) {
    tg <- gtable::gtable_add_grob(tg, hline(),
                                  t = r + 1L, b = r + 1L,
                                  l = 1L, r = ncol(tg), z = Inf)
  }

  ggplot2::ggplot(data.frame(x = 0, y = 0), ggplot2::aes(x, y)) +
    ggplot2::annotation_custom(tg, xmin = 0.01, xmax = 0.99,
                                   ymin = 0.02, ymax = 0.97) +
    ggplot2::scale_x_continuous(limits = c(0, 1), expand = c(0, 0)) +
    ggplot2::scale_y_continuous(limits = c(0, 1), expand = c(0, 0)) +
    ggplot2::coord_cartesian(clip = "off") +
    ggplot2::labs(
      title    = "A \u2014 Method / L\u2082seg summary",
      subtitle = "passes \u00b7 runtime (hr) \u00b7 MIP solves \u00b7 accepted boundary moves \u00b7 stations moved \u00b7 transit improvement \u00b7 relative improvement"
    ) +
    ggplot2::theme_void() +
    ggplot2::theme(
      plot.title    = ggplot2::element_text(size = 10, hjust = 0),
      plot.subtitle = ggplot2::element_text(size = 7.5, hjust = 0,
                                            color = "grey40",
                                            margin = ggplot2::margin(b = 3)),
      plot.title.position = "plot",
      plot.margin = ggplot2::margin(5, 5, 5, 5)
    )
}

# ---------------------------------------------------------------------------
# Combined (all-methods) plot — all l2segs, color=method, linetype=l2seg
# ---------------------------------------------------------------------------
make_combined_plot <- function(df, methods_present) {
  keep_methods <- intersect(method_order, methods_present)
  palette      <- method_colors[keep_methods]
  names(palette) <- vapply(keep_methods, function(m) method_labels[m], character(1L))

  # All data for all methods (no l2seg filtering)
  df_c <- df[df$method %in% keep_methods, , drop = FALSE]
  df_c$method_label <- factor(method_labels[df_c$method], levels = names(palette))
  df_c$series       <- paste0(as.character(df_c$method_label), "__", df_c$l2seg_label)

  changed <- df_c[as.logical(df_c$changed) & df_c$pass_number > 0, , drop = FALSE]

  no_legend <- ggplot2::theme(panel.grid.minor = ggplot2::element_blank(),
                               legend.position  = "none")

  # ---- A: combined method/l2seg table -------------------------------------
  p_a <- make_combined_table_panel(df, keep_methods, palette)

  # ---- B: transit — color by method, linetype by l2seg --------------------
  p_b <- ggplot2::ggplot(df_c,
    ggplot2::aes(x = pass_number, y = transit_nm,
                 color = method_label, linetype = l2seg_label, group = series)
  ) +
    ggplot2::geom_line(linewidth = 0.85) +
    ggplot2::geom_point(size = 1.5) +
    ggplot2::scale_color_manual(values = palette) +
    ggplot2::scale_linetype_discrete() +
    ggplot2::scale_x_continuous(breaks = scales::pretty_breaks()) +
    ggplot2::labs(title = "B \u2014 Transit distance by sweep",
                  x = "Pass", y = "Transit (nm)") + no_legend

  # ---- C: relative improvement — same aesthetics --------------------------
  p_c <- ggplot2::ggplot(df_c[!is.na(df_c$rel_improvement_pct), , drop = FALSE],
    ggplot2::aes(x = pass_number, y = rel_improvement_pct,
                 color = method_label, linetype = l2seg_label, group = series)
  ) +
    ggplot2::geom_line(linewidth = 0.85) +
    ggplot2::geom_point(size = 1.5) +
    ggplot2::scale_color_manual(values = palette) +
    ggplot2::scale_linetype_discrete() +
    ggplot2::scale_x_continuous(breaks = scales::pretty_breaks()) +
    ggplot2::labs(title = "C \u2014 Relative improvement by sweep",
                  x = "Pass", y = "Improvement (%)") + no_legend

  # ---- D: stations moved — boxplot by method (collapsed across l2segs) ---
  p_d <- if (nrow(changed) > 0) {
    ggplot2::ggplot(changed,
      ggplot2::aes(x = method_label, y = stations_moved, fill = method_label)
    ) +
      ggplot2::geom_boxplot(width = 0.6, alpha = 0.82, outlier.shape = NA,
                            linewidth = 0.4) +
      ggplot2::scale_fill_manual(values = palette) +
      ggplot2::labs(title = "D \u2014 Stations moved per pass",
                    x = NULL, y = "Stations moved") + no_legend
  } else {
    cowplot::ggdraw() + cowplot::draw_label("No changed passes", size = 10)
  }

  # ---- E: MIP solves ------------------------------------------------------
  p_e <- if (nrow(changed) > 0 && any(changed$mip_solve_count > 0)) {
    ggplot2::ggplot(changed[changed$mip_solve_count > 0, ],
      ggplot2::aes(x = method_label, y = mip_solve_count, fill = method_label)
    ) +
      ggplot2::geom_boxplot(width = 0.6, alpha = 0.82, outlier.shape = NA,
                            linewidth = 0.4) +
      ggplot2::scale_fill_manual(values = palette) +
      ggplot2::labs(title = "E \u2014 MIP solves per pass",
                    x = NULL, y = "MIP solves") + no_legend
  } else {
    cowplot::ggdraw() + cowplot::draw_label("No MIP solve data", size = 10)
  }

  # ---- F: boundary change rate --------------------------------------------
  p_f <- if (any(!is.na(df_c$boundary_change_rate))) {
    ggplot2::ggplot(df_c[!is.na(df_c$boundary_change_rate), ],
      ggplot2::aes(x = method_label, y = boundary_change_rate, fill = method_label)
    ) +
      ggplot2::geom_boxplot(width = 0.6, alpha = 0.82, outlier.shape = NA,
                            linewidth = 0.4) +
      ggplot2::scale_fill_manual(values = palette) +
      ggplot2::scale_y_continuous(labels = scales::percent_format(accuracy = 1)) +
      ggplot2::labs(title = "F \u2014 Boundary change rate",
                    x = NULL, y = "Changes / attempts") + no_legend
  } else {
    cowplot::ggdraw() + cowplot::draw_label("No boundary data", size = 10)
  }

  panels <- list(p_a = p_a, p_b = p_b, p_c = p_c,
                 p_d = p_d, p_e = p_e, p_f = p_f,
                 title = "Refinement Sweep Summary \u2014 All Methods")
  assemble_six_panels(panels)
}

# ---------------------------------------------------------------------------
# Per-method plot (l2seg as series)
# ---------------------------------------------------------------------------

make_method_plot <- function(df_m, method) {
  l2segs       <- sort(unique(df_m$l2seg[!is.na(df_m$l2seg)]))
  has_uncapped <- any(is.na(df_m$l2seg))
  l2seg_order  <- c(if (has_uncapped) NA, l2segs)
  level_labels <- vapply(l2seg_order, l2seg_label, character(1L))

  df_m$l2seg_label <- factor(df_m$l2seg_label, levels = level_labels)
  df_m$series      <- df_m$l2seg_label

  n_levels <- length(level_labels)
  pal_cols <- grDevices::colorRampPalette(unname(method_colors))(max(n_levels, 2L))
  pal_cols <- pal_cols[seq_len(n_levels)]
  palette  <- stats::setNames(pal_cols, level_labels)

  lbl <- if (!is.null(method_labels[[method]]) && !is.na(method_labels[[method]]))
    method_labels[[method]] else method

  panels <- build_six_panels(df_m, color_var = "l2seg_label", palette = palette,
                              plot_title = sprintf(
                                "Refinement Sweep Summary \u2014 %s", lbl))

  # Per-method layout:
  #   A = pass statistics table   (replaces stations-moved)
  #   B = transit by sweep
  #   C = relative improvement
  #   D = stations moved boxplot  (was A in combined)
  #   E = MIP solves
  #   F = boundary change rate
  p_table    <- make_pass_table_panel(df_m, level_labels, palette)
  panels$p_d <- panels$p_a + ggplot2::labs(title = "D \u2014 Stations moved per pass")
  panels$p_a <- p_table

  assemble_six_panels(panels)
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

main <- function() {
  load_gsp_plot_packages()
  opt <- parse_args(commandArgs(trailingOnly = TRUE))

  methods_arg    <- parse_str_list(opt$method)
  l2seg_arg      <- parse_l2seg_filter(opt$l2seg)

  con <- connect_gsp_db(opt$gsp_db, opt$solution_db)
  on.exit(DBI::dbDisconnect(con), add = TRUE)

  df_raw <- read_sweep_data(con, methods_arg, l2seg_arg)
  if (nrow(df_raw) == 0L) {
    message("No refinement pass data found.")
    return(invisible(FALSE))
  }

  df <- add_derived(df_raw)
  methods_present <- intersect(method_order,
                               c(unique(df$method),
                                 setdiff(unique(df$method), method_order)))

  message(sprintf("Loaded %d pass records across %d method(s)",
                  nrow(df), length(methods_present)))

  # ---- Combined plot -------------------------------------------------------
  if (!parse_bool(opt$skip_existing) || !file.exists(opt$output)) {
    combined <- make_combined_plot(df, methods_present)
    dir.create(dirname(opt$output), showWarnings = FALSE, recursive = TRUE)
    ggplot2::ggsave(opt$output, combined,
                    width = 15, height = 10, dpi = 200, bg = "white")
    message("Wrote ", opt$output)
  }

  # ---- Per-method plots ----------------------------------------------------
  if (parse_bool(opt$per_method)) {
    for (m in methods_present) {
      out_path <- file.path("sol", m, "refinement_sweep.png")
      if (parse_bool(opt$skip_existing) && file.exists(out_path)) {
        message("Skipping (exists): ", out_path)
        next
      }
      df_m <- df[df$method == m, , drop = FALSE]
      if (nrow(df_m) == 0L) next
      p <- make_method_plot(df_m, m)
      dir.create(dirname(out_path), showWarnings = FALSE, recursive = TRUE)
      ggplot2::ggsave(out_path, p,
                      width = 15, height = 10, dpi = 200, bg = "white")
      message("Wrote ", out_path)
    }
  }

  invisible(TRUE)
}

main()






