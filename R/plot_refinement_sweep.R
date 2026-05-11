#!/usr/bin/env Rscript
# plot_refinement_sweep.R
#
# DB-based refinement sweep plots.
#
# Produces:
#   1. Combined plot    — colour = method, linetype = L2seg
#   2. Per-method plots — colour/group = L2seg, with table panel A spanning both rows
#
# Usage from project root:
#   Rscript R/plot_refinement_sweep.R
#   Rscript R/plot_refinement_sweep.R --method nn,ci,ge --l2seg all

source("R/gsp_db.R")
source("R/gsp_plot_utils.R")

required_pkgs <- c("cowplot", "ggplot2", "scales", "grid", "gridExtra", "gtable")
for (pkg in required_pkgs) {
  if (!requireNamespace(pkg, quietly = TRUE)) {
    stop(sprintf("Missing required R package: %s", pkg), call. = FALSE)
  }
}

# ---------------------------------------------------------------------------
# Arguments
# ---------------------------------------------------------------------------

parse_args <- function(args) {
  out <- list(
    gsp_db        = "dat/gsp.db",
    solution_db   = "dat/solution.db",
    method        = "all",
    l2seg         = "all",
    output        = "sol/refinement_sweep.png",
    per_method    = "true",
    skip_existing = "false",
    # Practical tie rule for table highlighting.
    # A formal alpha=0.05 significance rule is not valid here unless each
    # method/L2seg setting has replicated independent runs or comparable samples.
    # For percentages this is percentage points; for transit nm this is nm.
    highlight_tol_pct = "0.2",
    highlight_tol_nm  = "1.0"
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

parse_num <- function(x, name) {
  v <- suppressWarnings(as.numeric(x))
  if (length(v) != 1L || is.na(v)) stop(sprintf("Invalid numeric value for %s: %s", name, x), call. = FALSE)
  v
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
# Labels / palettes
# ---------------------------------------------------------------------------

method_order  <- c("noport", "nn", "ge", "ci", "fixedport")
method_labels <- c(noport = "MH-OPT", nn = "MH-NN", ge = "MH-GE",
                   ci = "MH-CI", fixedport = "Fixed-port")

# Used only when methods are the visual groups.
method_colors <- c(noport = "#355070", nn = "#6D597A",
                   ge = "#B56576", ci = "#E56B6F",
                   fixedport = "#EAAC8B")

# Used only when L2seg values are the visual groups.
# Deliberately different from method_colors to avoid implying the same meaning.
l2seg_colors <- c("#264653", "#2A9D8F", "#8AB17D", "#E9C46A",
                  "#F4A261", "#E76F51", "#A44A3F", "#6D597A")

l2seg_label <- function(x) {
  if (is.na(x)) "∞" else sprintf("%d", as.integer(x))
}

label_method <- function(method) {
  out <- method_labels[[method]]
  if (is.null(out) || is.na(out)) method else out
}

make_method_palette <- function(methods) {
  keep <- intersect(method_order, methods)
  pal <- method_colors[keep]
  names(pal) <- vapply(keep, label_method, character(1L))
  pal
}

make_l2seg_palette <- function(level_labels) {
  n <- length(level_labels)
  if (n <= length(l2seg_colors)) {
    cols <- l2seg_colors[seq_len(n)]
  } else {
    cols <- grDevices::colorRampPalette(l2seg_colors)(n)
  }
  stats::setNames(cols, level_labels)
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
      r.l2seg_timeout_seconds          AS l2seg,
      rp.pass_number,
      rp.changed,
      COALESCE(rp.stations_moved, 0)   AS stations_moved,
      COALESCE(rp.mip_solves, 0)       AS mip_solve_count,
      rp.runtime_seconds               AS pass_runtime,
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
  df$series <- paste0(df$method, "_", ifelse(is.na(df$l2seg), "inf", as.character(df$l2seg)))
  df <- df[order(df$series, df$pass_number), , drop = FALSE]

  init_transit <- tapply(df$transit_nm, df$series, function(x) x[1])
  df$init_transit <- init_transit[df$series]

  df$rel_improvement_pct <- ifelse(
    is.na(df$init_transit) | df$init_transit == 0,
    NA_real_,
    100 * (df$init_transit - df$transit_nm) / df$init_transit
  )

  df$pass_improvement_nm <- ave(
    df$transit_nm, df$series,
    FUN = function(x) c(NA_real_, -diff(x))
  )

  df$boundary_change_rate <- ifelse(
    df$boundary_attempts > 0,
    df$boundary_changes / df$boundary_attempts,
    NA_real_
  )

  df$l2seg_label   <- vapply(df$l2seg, l2seg_label, character(1L))
  df$method_label  <- vapply(df$method, label_method, character(1L))
  df
}

methods_present_in <- function(df) {
  unique_methods <- unique(df$method)
  c(intersect(method_order, unique_methods), setdiff(unique_methods, method_order))
}

l2seg_levels_in <- function(df) {
  l2segs <- sort(unique(df$l2seg[!is.na(df$l2seg)]))
  c(if (any(is.na(df$l2seg))) l2seg_label(NA), vapply(l2segs, l2seg_label, character(1L)))
}

# ---------------------------------------------------------------------------
# Plot helpers
# ---------------------------------------------------------------------------

no_legend_theme <- function() {
  ggplot2::theme(panel.grid.minor = ggplot2::element_blank(), legend.position = "none")
}

empty_panel <- function(label, size = 10) {
  cowplot::ggdraw() + cowplot::draw_label(label, size = size)
}

plot_line_panel <- function(df, y, title, ylab, group_var, color_var,
                            palette, linetype_var = NULL) {
  aes_args <- list(
    x = quote(pass_number),
    y = as.name(y),
    color = as.name(color_var),
    group = as.name(group_var)
  )
  if (!is.null(linetype_var)) aes_args$linetype <- as.name(linetype_var)

  ggplot2::ggplot(df, do.call(ggplot2::aes, aes_args)) +
    ggplot2::geom_line(linewidth = 0.85) +
    ggplot2::geom_point(size = 1.5) +
    ggplot2::scale_color_manual(values = palette) +
    ggplot2::scale_x_continuous(breaks = scales::pretty_breaks()) +
    ggplot2::labs(title = title, x = "Pass", y = ylab) +
    no_legend_theme()
}

plot_box_panel <- function(df, x_var, y_var, fill_var, palette, title, ylab,
                           y_percent = FALSE, y_log10 = FALSE) {
  sub <- df[!is.na(df[[y_var]]), , drop = FALSE]
  if (nrow(sub) == 0L) return(empty_panel(sprintf("No %s data", ylab)))

  p <- ggplot2::ggplot(
    sub,
    ggplot2::aes(x = .data[[x_var]], y = .data[[y_var]], fill = .data[[fill_var]])
  ) +
    ggplot2::geom_boxplot(width = 0.6, alpha = 0.82, outlier.shape = NA, linewidth = 0.4) +
    ggplot2::scale_fill_manual(values = palette) +
    ggplot2::labs(title = title, x = NULL, y = ylab) +
    no_legend_theme()

  if (y_percent) {
    p <- p + ggplot2::scale_y_continuous(labels = scales::percent_format(accuracy = 1))
  }
  if (y_log10) {
    p <- p + ggplot2::scale_y_log10(
      breaks = scales::trans_breaks("log10", function(x) 10^x),
      labels = scales::trans_format("log10", scales::math_format(10^.x))
    )
  }
  p
}

# ---------------------------------------------------------------------------
# Table helpers
# ---------------------------------------------------------------------------

add_table_body_fontfaces <- function(tg, bold_cells) {
  if (is.null(bold_cells) || nrow(bold_cells) == 0L) return(tg)

  body <- tg$layout[tg$layout$name == "core-fg", , drop = FALSE]

  for (i in seq_len(nrow(bold_cells))) {
    rr <- bold_cells$row[[i]]
    cc <- bold_cells$col[[i]]

    hit <- body[body$t == rr + 1L & body$l == cc, , drop = FALSE]
    if (nrow(hit) < 1L) next

    # The row name of tg$layout is the index into tg$grobs.
    grob_idx <- as.integer(rownames(hit)[1L])
    if (is.na(grob_idx) || grob_idx > length(tg$grobs)) next

    # grid::gpar cannot contain both `font` and `fontface`.
    # tableGrob may already set numeric `font`, so remove it before using fontface.
    if (!is.null(tg$grobs[[grob_idx]]$gp$font)) {
      tg$grobs[[grob_idx]]$gp$font <- NULL
    }
    tg$grobs[[grob_idx]]$gp$fontface <- "bold"

    if ("colour" %in% names(bold_cells) && !is.na(bold_cells$colour[[i]])) {
      tg$grobs[[grob_idx]]$gp$col <- bold_cells$colour[[i]]
    }
  }

  tg
}

add_group_separators <- function(tg, sep_after) {
  if (length(sep_after) == 0L) return(tg)
  hline <- function() {
    grid::segmentsGrob(
      x0 = grid::unit(0, "npc"), x1 = grid::unit(1, "npc"),
      y0 = grid::unit(0, "npc"), y1 = grid::unit(0, "npc"),
      gp = grid::gpar(col = "grey50", lwd = 0.9)
    )
  }
  for (r in sep_after) {
    tg <- gtable::gtable_add_grob(
      tg, hline(), t = r + 1L, b = r + 1L, l = 1L, r = ncol(tg), z = Inf
    )
  }
  tg
}

make_table_plot <- function(tbl, fill_vec, title, subtitle,
                            bold_cells = NULL, sep_after = integer(),
                            base_size = 7.2) {
  fill_mat <- matrix(fill_vec, nrow = nrow(tbl), ncol = ncol(tbl))
  th <- table_theme(fill_mat, base_size = base_size)
  tg <- gridExtra::tableGrob(tbl, rows = NULL, theme = th)
  tg <- add_table_body_fontfaces(tg, bold_cells)
  tg <- add_group_separators(tg, sep_after)

  ggplot2::ggplot(data.frame(x = 0, y = 0), ggplot2::aes(x, y)) +
    ggplot2::annotation_custom(tg, xmin = 0.01, xmax = 0.99, ymin = 0.02, ymax = 0.97) +
    ggplot2::scale_x_continuous(limits = c(0, 1), expand = c(0, 0)) +
    ggplot2::scale_y_continuous(limits = c(0, 1), expand = c(0, 0)) +
    ggplot2::coord_cartesian(clip = "off") +
    ggplot2::labs(title = title, subtitle = subtitle) +
    ggplot2::theme_void() +
    ggplot2::theme(
      plot.title = ggplot2::element_text(size = 10, hjust = 0),
      plot.subtitle = ggplot2::element_text(size = 7.2, hjust = 0, color = "grey40",
                                            margin = ggplot2::margin(b = 3)),
      plot.title.position = "plot",
      plot.margin = ggplot2::margin(5, 5, 5, 5)
    )
}

# Practical tie rule:
#   1. Bold the lowest final transit_nm, including near-ties within tol_nm.
#   2. Also bold the highest final relative improvement, including near-ties within tol_pct.
# This is not a formal alpha=0.05 significance test. With one deterministic
# final value per setting, there is no within-setting sampling variance to test.
# The actual value cells are bolded, not the whole row.
find_best_cells <- function(summary_df, col_names, tol_nm, tol_pct) {
  bold <- data.frame(row = integer(), col = integer(), colour = character())

  transit_col <- match("Transit", col_names)
  dpct_col    <- match("\u0394%", col_names)

  if (!is.na(transit_col) && "final_transit" %in% names(summary_df)) {
    best_nm <- min(summary_df$final_transit, na.rm = TRUE)
    rows <- which(!is.na(summary_df$final_transit) & summary_df$final_transit <= best_nm + tol_nm)
    if (length(rows) > 0L) bold <- rbind(bold, data.frame(row = rows, col = transit_col, colour = NA_character_))
  }

  if (!is.na(dpct_col) && "final_pct" %in% names(summary_df)) {
    best_pct <- max(summary_df$final_pct, na.rm = TRUE)
    rows <- which(!is.na(summary_df$final_pct) & summary_df$final_pct >= best_pct - tol_pct)
    if (length(rows) > 0L) bold <- rbind(bold, data.frame(row = rows, col = dpct_col, colour = NA_character_))
  }

  bold
}

find_table_best_cells <- function(tbl, col_names, colour = "red") {
  out <- data.frame(row = integer(), col = integer(), colour = character())

  transit_col <- match("Transit", col_names)
  if (!is.na(transit_col)) {
    transit <- suppressWarnings(as.numeric(tbl[[transit_col]]))
    if (any(!is.na(transit))) {
      rows <- which(!is.na(transit) & transit == min(transit, na.rm = TRUE))
      out <- rbind(out, data.frame(row = rows, col = transit_col, colour = colour))
    }
  }

  dpct_col <- match("Δ%", col_names)
  if (!is.na(dpct_col)) {
    dpct <- suppressWarnings(as.numeric(sub("%", "", tbl[[dpct_col]], fixed = TRUE)))
    if (any(!is.na(dpct))) {
      rows <- which(!is.na(dpct) & dpct == max(dpct, na.rm = TRUE))
      out <- rbind(out, data.frame(row = rows, col = dpct_col, colour = colour))
    }
  }

  out
}

make_summary_rows <- function(df, group_vars) {
  groups <- unique(df[group_vars])

  # Important: grouping columns may be named "method", which conflicts with
  # the formal `method` argument of base::order(). Strip names before do.call().
  ord_args <- unname(as.list(groups))
  groups <- groups[do.call(order, ord_args), , drop = FALSE]

  rows <- vector("list", nrow(groups))
  for (i in seq_len(nrow(groups))) {
    mask <- rep(TRUE, nrow(df))
    for (v in group_vars) mask <- mask & df[[v]] == groups[[v]][[i]]
    sub_all <- df[mask, , drop = FALSE]
    sub_pass <- sub_all[sub_all$pass_number > 0, , drop = FALSE]
    final <- sub_all[order(sub_all$pass_number), , drop = FALSE]
    final <- final[nrow(final), , drop = FALSE]

    rows[[i]] <- cbind(
      groups[i, , drop = FALSE],
      data.frame(
        passes        = nrow(sub_pass),
        runtime_hr    = sum(sub_pass$pass_runtime, na.rm = TRUE) / 3600,
        mip           = sum(sub_pass$mip_solve_count, na.rm = TRUE),
        acc           = sum(sub_pass$boundary_changes, na.rm = TRUE),
        stations      = sum(sub_pass$stations_moved, na.rm = TRUE),
        delta_nm      = sum(sub_pass$pass_improvement_nm, na.rm = TRUE),
        final_transit = final$transit_nm,
        final_pct     = final$rel_improvement_pct,
        stringsAsFactors = FALSE
      )
    )
  }
  do.call(rbind, rows)
}

# ---------------------------------------------------------------------------
# Panel A tables
# ---------------------------------------------------------------------------

make_method_table_panel <- function(df_m, level_order, palette, tol_nm, tol_pct) {
  passes <- df_m[df_m$pass_number > 0, , drop = FALSE]
  if (nrow(passes) == 0L) return(empty_panel("No pass data"))

  passes$l2seg_label <- factor(passes$l2seg_label, levels = level_order)
  passes <- passes[order(passes$l2seg_label, passes$pass_number), , drop = FALSE]

  summary <- make_summary_rows(df_m, "l2seg_label")
  summary$l2seg_label <- factor(summary$l2seg_label, levels = level_order)
  summary <- summary[order(summary$l2seg_label), , drop = FALSE]

  col_names <- c("L2seg", "#", "hr", "MIP", "Acc", "|SΔ|", "Δnm", "Transit", "Δ%")

  out_rows <- list()
  row_fills <- character()
  summary_rows <- integer()
  sep_after <- integer()
  row_idx <- 0L

  for (lv in level_order) {
    sub <- passes[passes$l2seg_label == lv, , drop = FALSE]
    if (nrow(sub) == 0L) next
    base_col <- palette[[lv]]

    for (i in seq_len(nrow(sub))) {
      r <- sub[i, , drop = FALSE]
      row_idx <- row_idx + 1L
      row_df <- data.frame(
        l2seg   = lv,
        pass    = as.character(r$pass_number),
        rt      = if (is.na(r$pass_runtime)) "—" else sprintf("%.2f", r$pass_runtime / 3600),
        mip     = as.character(r$mip_solve_count),
        acc     = as.character(r$boundary_changes),
        smov    = as.character(r$stations_moved),
        dnm     = if (is.na(r$pass_improvement_nm)) "—" else sprintf("%.0f", r$pass_improvement_nm),
        transit = if (is.na(r$transit_nm)) "—" else sprintf("%.1f", r$transit_nm),
        dpct    = if (is.na(r$rel_improvement_pct)) "—" else sprintf("%.1f%%", r$rel_improvement_pct),
        stringsAsFactors = FALSE
      )
      names(row_df) <- col_names
      out_rows[[row_idx]] <- row_df
      row_fills[[row_idx]] <- scales::alpha(base_col, 0.18)
    }

    s <- summary[summary$l2seg_label == lv, , drop = FALSE]
    if (nrow(s) == 1L) {
      row_idx <- row_idx + 1L
      summary_df <- data.frame(
        l2seg   = lv,
        pass    = "Σ",
        rt      = sprintf("%.2f", s$runtime_hr),
        mip     = as.character(s$mip),
        acc     = as.character(s$acc),
        smov    = as.character(s$stations),
        dnm     = sprintf("%.0f", s$delta_nm),
        transit = sprintf("%.1f", s$final_transit),
        dpct    = sprintf("%.1f%%", s$final_pct),
        stringsAsFactors = FALSE
      )
      names(summary_df) <- col_names
      out_rows[[row_idx]] <- summary_df
      row_fills[[row_idx]] <- scales::alpha(base_col, 0.42)
      summary_rows <- c(summary_rows, row_idx)
      sep_after <- c(sep_after, row_idx)
    }
  }

  if (length(sep_after) > 0L) sep_after <- sep_after[-length(sep_after)]

  tbl <- do.call(rbind, out_rows)

  # Bold all cells in summary rows, then additionally bold best-value cells
  # among the summary rows according to the practical tie rule.
  bold_cells <- do.call(rbind, lapply(summary_rows, function(r) {
    data.frame(row = r, col = seq_along(col_names), colour = NA_character_)
  }))

  best_cells <- find_best_cells(summary, col_names, tol_nm, tol_pct)
  if (nrow(best_cells) > 0L) {
    best_cells$row <- summary_rows[best_cells$row]
    bold_cells <- rbind(bold_cells, best_cells)
  }

  # Red bold = table-wide best values among all displayed rows.
  bold_cells <- rbind(bold_cells, find_table_best_cells(tbl, col_names, colour = "red"))

  make_table_plot(
    tbl, row_fills,
    title = "A — Pass statistics by L2seg",
    subtitle = "per-pass rows plus bold summary rows; runtime (hr) · MIP solves · accepted moves · stations moved · transit",
    bold_cells = bold_cells,
    sep_after = sep_after,
    base_size = 7.4
  )
}

make_combined_table_panel <- function(df, keep_methods, palette, tol_nm, tol_pct) {
  summary <- make_summary_rows(df[df$method %in% keep_methods, , drop = FALSE], c("method", "l2seg_label"))
  summary$method_label <- vapply(summary$method, label_method, character(1L))
  summary$method_label <- factor(summary$method_label, levels = names(palette))
  summary$l2seg_label <- factor(summary$l2seg_label, levels = l2seg_levels_in(df))
  summary <- summary[order(summary$method_label, summary$l2seg_label), , drop = FALSE]

  col_names <- c("Method", "L2seg", "#", "hr", "MIP", "Acc", "|SΔ|", "Δnm", "Transit", "Δ%")
  tbl <- data.frame(
    method  = as.character(summary$method_label),
    l2seg   = as.character(summary$l2seg_label),
    pass    = as.character(summary$passes),
    rt      = sprintf("%.2f", summary$runtime_hr),
    mip     = as.character(summary$mip),
    acc     = as.character(summary$acc),
    smov    = as.character(summary$stations),
    dnm     = sprintf("%.0f", summary$delta_nm),
    transit = sprintf("%.1f", summary$final_transit),
    dpct    = sprintf("%.1f%%", summary$final_pct),
    stringsAsFactors = FALSE
  )
  names(tbl) <- col_names

  fill_vec <- scales::alpha(palette[as.character(summary$method_label)], 0.25)

  # Highlight within each method, because the comparison is method-specific across L2seg settings.
  bold_cells <- data.frame(row = integer(), col = integer(), colour = character())
  for (m in levels(summary$method_label)) {
    idx <- which(summary$method_label == m)
    local <- find_best_cells(summary[idx, , drop = FALSE], col_names, tol_nm, tol_pct)
    if (nrow(local) > 0L) {
      local$row <- idx[local$row]
      bold_cells <- rbind(bold_cells, local)
    }
  }

  # Red bold = table-wide best values among all displayed rows.
  bold_cells <- rbind(bold_cells, find_table_best_cells(tbl, col_names, colour = "red"))

  sep_after <- integer()
  for (m in levels(summary$method_label)) {
    idx <- which(summary$method_label == m)
    if (length(idx) > 0L) sep_after <- c(sep_after, max(idx))
  }
  if (length(sep_after) > 0L) sep_after <- sep_after[-length(sep_after)]

  make_table_plot(
    tbl, fill_vec,
    title = "A — Method / L2seg summary",
    subtitle = "passes \u00b7 runtime (hr) \u00b7 MIP solves \u00b7 accepted moves \u00b7 stations moved \u00b7 transit improvement \u00b7 final transit",
    bold_cells = bold_cells,
    sep_after = sep_after,
    base_size = 6.9
  )
}

# ---------------------------------------------------------------------------
# Panel builders
# ---------------------------------------------------------------------------

make_common_panels <- function(df, mode, palette, tol_nm, tol_pct) {
  stopifnot(mode %in% c("method", "combined"))

  if (mode == "method") {
    levels_l2 <- l2seg_levels_in(df)
    df$l2seg_label <- factor(df$l2seg_label, levels = levels_l2)
    df$plot_group   <- df$l2seg_label
    changed <- df[as.logical(df$changed) & df$pass_number > 0, , drop = FALSE]

    list(
      a = make_method_table_panel(df, levels_l2, palette, tol_nm, tol_pct),
      b = plot_line_panel(df, "transit_nm", "B \u2014 Transit distance by sweep", "Transit (nm)",
                          "series", "l2seg_label", palette),
      c = plot_line_panel(df[!is.na(df$rel_improvement_pct), , drop = FALSE],
                          "rel_improvement_pct", "C \u2014 Relative improvement by sweep", "Improvement (%)",
                          "series", "l2seg_label", palette),
      d = plot_box_panel(changed, "l2seg_label", "stations_moved", "l2seg_label", palette,
                         "D \u2014 Stations moved per pass", "Stations moved"),
      e = plot_box_panel(changed[changed$mip_solve_count > 0, , drop = FALSE],
                         "l2seg_label", "mip_solve_count", "l2seg_label", palette,
                         "E \u2014 MIP solves per pass", "MIP solves"),
      f = plot_box_panel(df[!is.na(df$boundary_change_rate), , drop = FALSE],
                         "l2seg_label", "boundary_change_rate", "l2seg_label", palette,
                         "F \u2014 Boundary change rate", "Changes / attempts", y_percent = TRUE)
    )
  } else {
    keep_methods <- methods_present_in(df)
    df <- df[df$method %in% keep_methods, , drop = FALSE]
    df$method_label <- factor(df$method_label, levels = names(palette))
    df$plot_series  <- paste0(as.character(df$method_label), "__", df$l2seg_label)
    changed <- df[as.logical(df$changed) & df$pass_number > 0, , drop = FALSE]

    list(
      a = make_combined_table_panel(df, keep_methods, palette, tol_nm, tol_pct),
      b = plot_line_panel(df, "transit_nm", "B \u2014 Transit distance by sweep", "Transit (nm)",
                          "plot_series", "method_label", palette, linetype_var = "l2seg_label"),
      c = plot_line_panel(df[!is.na(df$rel_improvement_pct), , drop = FALSE],
                          "rel_improvement_pct", "C \u2014 Relative improvement by sweep", "Improvement (%)",
                          "plot_series", "method_label", palette, linetype_var = "l2seg_label"),
      d = plot_box_panel(changed, "method_label", "stations_moved", "method_label", palette,
                         "D \u2014 Stations moved per pass", "Stations moved"),
      e = plot_box_panel(changed[changed$mip_solve_count > 0, , drop = FALSE],
                         "method_label", "mip_solve_count", "method_label", palette,
                         "E \u2014 MIP solves per pass", "MIP solves"),
      f = plot_box_panel(df[!is.na(df$boundary_change_rate), , drop = FALSE],
                         "method_label", "boundary_change_rate", "method_label", palette,
                         "F \u2014 Boundary change rate", "Changes / attempts", y_percent = TRUE)
    )
  }
}

# ---------------------------------------------------------------------------
# Layouts
# ---------------------------------------------------------------------------

add_header <- function(plot, title) {
  hdr <- cowplot::ggdraw() +
    cowplot::draw_label(title, fontface = "bold", size = 14, x = 0.5, hjust = 0.5)
  cowplot::plot_grid(hdr, plot, ncol = 1L, rel_heights = c(0.06, 1))
}

assemble_combined_layout <- function(panels, title) {
  top <- cowplot::plot_grid(panels$a, panels$b, panels$c,
                            ncol = 3L, align = "h", axis = "tb")
  bottom <- cowplot::plot_grid(panels$d, panels$e, panels$f,
                               ncol = 3L, align = "h", axis = "tb")
  add_header(cowplot::plot_grid(top, bottom, ncol = 1L), title)
}

assemble_method_layout <- function(panels, title) {
  # Requested layout:
  #   A spans both rows on the left.
  #   Right side: row 1 = B C; row 2 = D E F.
  right_top <- cowplot::plot_grid(panels$b, panels$c,
                                  ncol = 2L, align = "h", axis = "tb")
  right_bottom <- cowplot::plot_grid(panels$d, panels$e, panels$f,
                                     ncol = 3L, align = "h", axis = "tb")
  right <- cowplot::plot_grid(right_top, right_bottom, ncol = 1L, rel_heights = c(1, 1))
  body <- cowplot::plot_grid(panels$a, right, ncol = 2L, rel_widths = c(0.45, 1))
  add_header(body, title)
}

# ---------------------------------------------------------------------------
# Public plot functions
# ---------------------------------------------------------------------------

make_combined_plot <- function(df, tol_nm, tol_pct) {
  keep_methods <- methods_present_in(df)
  palette <- make_method_palette(keep_methods)
  panels <- make_common_panels(df, mode = "combined", palette = palette,
                               tol_nm = tol_nm, tol_pct = tol_pct)
  assemble_combined_layout(panels, "Refinement Sweep Summary \u2014 All Methods")
}

make_method_plot <- function(df_m, method, tol_nm, tol_pct) {
  levels_l2 <- l2seg_levels_in(df_m)
  palette <- make_l2seg_palette(levels_l2)
  panels <- make_common_panels(df_m, mode = "method", palette = palette,
                               tol_nm = tol_nm, tol_pct = tol_pct)
  assemble_method_layout(panels, sprintf("Refinement Sweep Summary \u2014 %s", label_method(method)))
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

main <- function() {
  load_gsp_plot_packages()
  opt <- parse_args(commandArgs(trailingOnly = TRUE))

  methods_arg <- parse_str_list(opt$method)
  l2seg_arg   <- parse_l2seg_filter(opt$l2seg)
  tol_pct     <- parse_num(opt$highlight_tol_pct, "--highlight_tol_pct")
  tol_nm      <- parse_num(opt$highlight_tol_nm,  "--highlight_tol_nm")

  con <- connect_gsp_db(opt$gsp_db, opt$solution_db)
  on.exit(DBI::dbDisconnect(con), add = TRUE)

  df_raw <- read_sweep_data(con, methods_arg, l2seg_arg)
  if (nrow(df_raw) == 0L) {
    message("No refinement pass data found.")
    return(invisible(FALSE))
  }

  df <- add_derived(df_raw)
  methods_present <- methods_present_in(df)

  message(sprintf("Loaded %d pass records across %d method(s)", nrow(df), length(methods_present)))

  if (!parse_bool(opt$skip_existing) || !file.exists(opt$output)) {
    combined <- make_combined_plot(df, tol_nm = tol_nm, tol_pct = tol_pct)
    dir.create(dirname(opt$output), showWarnings = FALSE, recursive = TRUE)
    ggplot2::ggsave(opt$output, combined, width = 15, height = 10, dpi = 200, bg = "white")
    message("Wrote ", opt$output)
  }

  if (parse_bool(opt$per_method)) {
    for (m in methods_present) {
      out_path <- file.path("sol", m, "refinement_sweep.png")
      if (parse_bool(opt$skip_existing) && file.exists(out_path)) {
        message("Skipping (exists): ", out_path)
        next
      }
      df_m <- df[df$method == m, , drop = FALSE]
      if (nrow(df_m) == 0L) next

      p <- make_method_plot(df_m, m, tol_nm = tol_nm, tol_pct = tol_pct)
      dir.create(dirname(out_path), showWarnings = FALSE, recursive = TRUE)
      ggplot2::ggsave(out_path, p, width = 15, height = 10, dpi = 200, bg = "white")
      message("Wrote ", out_path)
    }
  }

  invisible(TRUE)
}

main()
