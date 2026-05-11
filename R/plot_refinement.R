#!/usr/bin/env Rscript

source("R/gsp_db.R")
source("R/gsp_plot_utils.R")

if (!requireNamespace("cowplot", quietly = TRUE)) {
  stop("Missing required R package: cowplot", call. = FALSE)
}

parse_args <- function(args) {
  out <- list(
    gsp_db = "dat/gsp.db",
    solution_db = "dat/solution.db",
    method = "all",
    l2seg = "all",
    output = NULL,
    ports = "all",
    table_corner_a = "upper_right",
    table_corner_b = "upper_right",
    skip_existing = "true",
    camera_ready = "false"
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

valid_methods <- function() {
  c("noport", "nn", "ge", "ci", "fixedport")
}

parse_bool <- function(x) {
  value <- tolower(trimws(x))
  if (value %in% c("1", "true", "t", "yes", "y")) return(TRUE)
  if (value %in% c("0", "false", "f", "no", "n")) return(FALSE)
  stop(sprintf("Invalid boolean value: %s", x), call. = FALSE)
}

parse_methods <- function(x) {
  if (is.null(x) || !nzchar(x) || x %in% c("all", "ALL", "*")) {
    return(valid_methods())
  }
  methods <- trimws(strsplit(x, ",", fixed = TRUE)[[1]])
  methods <- methods[nzchar(methods)]
  unknown <- setdiff(methods, valid_methods())
  if (length(unknown) > 0L) {
    stop(
      sprintf(
        "Unknown method(s): %s. Use one or more of: %s, or all.",
        paste(unknown, collapse = ", "),
        paste(valid_methods(), collapse = ", ")
      ),
      call. = FALSE
    )
  }
  methods
}

parse_l2seg <- function(x) {
  if (is.null(x) || !nzchar(x) || x %in% c("all", "ALL", "*")) return(NULL)
  vals <- suppressWarnings(as.integer(trimws(strsplit(x, ",", fixed = TRUE)[[1]])))
  if (any(is.na(vals))) {
    stop("--l2seg must be all or a comma-separated list of integer seconds.", call. = FALSE)
  }
  vals
}

parse_id_list <- function(x) {
  if (is.null(x) || x %in% c("all", "ALL", "*")) return(NULL)
  if (x %in% c("none", "NONE", "-")) return(integer())
  as.integer(strsplit(x, ",", fixed = TRUE)[[1]])
}

method_label <- function(method) {
  switch(method,
    noport = "MH-OPT",
    nn = "MH-NN",
    ge = "MH-GE",
    ci = "MH-CI",
    method
  )
}

method_code_label <- function(method) {
  switch(method,
    noport = "MH-OPT",
    nn = "MH-NN",
    ge = "MH-GE",
    ci = "MH-CI",
    method
  )
}

method_refinement_label <- function(method) {
  switch(method,
    noport = "no-port refinement",
    nn = "nearest neighbor refinement",
    ge = "greedy edge refinement",
    ci = "cheapest insertion refinement",
    method
  )
}

read_refinement_variants <- function(con, methods, l2seg_values = NULL) {
  placeholders <- paste(rep("?", length(methods)), collapse = ",")
  sql <- sprintf("
    SELECT
      init.method,
      init.l2seg_timeout_seconds AS l2seg,
      init.run_id AS init_run_id,
      final.run_id AS final_run_id,
      final.solution_key AS final_solution_key,
      CAST(SUBSTR(final.solution_key, 5) AS INTEGER) AS n_sweeps
    FROM solution.runs init
    JOIN solution.runs final
      ON final.method = init.method
     AND final.phase = 'refinement'
     AND final.l2seg_timeout_seconds = init.l2seg_timeout_seconds
     AND final.is_final = 1
    WHERE init.phase = 'refinement'
      AND init.solution_key = 'init'
      AND init.method IN (%s)
    ORDER BY init.method, init.l2seg_timeout_seconds
  ", placeholders)

  rows <- db_read(con, sql, as.list(methods))
  rows <- rows[order(match(rows$method, methods), rows$l2seg), , drop = FALSE]

  if (!is.null(l2seg_values)) {
    rows <- rows[rows$l2seg %in% l2seg_values, , drop = FALSE]
  }
  rows
}

grand_transit <- function(route) {
  total <- route$distances[is.na(route$distances$segment), , drop = FALSE]
  if (nrow(total) > 0L) return(total$transit_nm[[1]])
  sum(route$distances$transit_nm, na.rm = TRUE)
}

format_runtime <- function(seconds) {
  if (is.null(seconds) || length(seconds) == 0L || is.na(seconds)) return("n/a")
  units <- c(
    "week" = 7 * 24 * 3600,
    "day" = 24 * 3600,
    "hr" = 3600,
    "min" = 60,
    "s" = 1
  )
  for (unit in names(units)) {
    value <- seconds / units[[unit]]
    if (value >= 1 || identical(unit, "s")) {
      label <- if (unit %in% c("week", "day") && round(value, 1) != 1) paste0(unit, "s") else unit
      return(sprintf("%.1f %s", value, label))
    }
  }
}

improvement_label <- function(init_route, final_route) {
  init_nm <- grand_transit(init_route)
  final_nm <- grand_transit(final_route)
  delta <- init_nm - final_nm
  pct <- if (final_nm == 0) 0 else delta / final_nm * 100
  sprintf("%.0f nm (%.1f%%)", delta, pct)
}

refinement_run_id <- function(solution_run_id) {
  sub(":(init|pass[0-9]+)$", "", solution_run_id)
}

unique_stations_moved <- function(con, solution_run_id) {
  rows <- db_read(con, "
    SELECT COALESCE(unique_moved_stations, 0) AS unique_moved_stations
    FROM solution.refinement_summary
    WHERE final_run_id = ?
  ", list(solution_run_id))
  if (nrow(rows) == 0L) return(0L)
  as.integer(rows$unique_moved_stations[[1]])
}

unique_moved_by_final_segment <- function(con, solution_run_id) {
  rows <- db_read(con, "
    WITH refinement AS (
      SELECT run_id
      FROM solution.refinement_summary
      WHERE final_run_id = ?
    ),
    segments AS (
      SELECT segment
      FROM solution.distance
      WHERE run_id = ?
        AND segment IS NOT NULL
    ),
    moved_out AS (
      SELECT initial_segment AS segment, COUNT(*) AS moved_out
      FROM solution.refinement_unique_station_moves
      WHERE run_id = (SELECT run_id FROM refinement)
      GROUP BY initial_segment
    ),
    moved_in AS (
      SELECT final_segment AS segment, COUNT(*) AS moved_in
      FROM solution.refinement_unique_station_moves
      WHERE run_id = (SELECT run_id FROM refinement)
      GROUP BY final_segment
    )
    SELECT
      segments.segment,
      COALESCE(moved_out.moved_out, 0) AS moved_out,
      COALESCE(moved_in.moved_in, 0) AS moved_in
    FROM segments
    LEFT JOIN moved_out ON moved_out.segment = segments.segment
    LEFT JOIN moved_in ON moved_in.segment = segments.segment
    ORDER BY segments.segment
  ", list(solution_run_id, solution_run_id))
  rows$moved_out <- as.integer(rows$moved_out)
  rows$moved_in <- as.integer(rows$moved_in)
  rows
}

station_count <- function(route) {
  length(unique(route$station_lines$station_id))
}

segment_metrics <- function(route, prefix) {
  counts <- stats::aggregate(station_id ~ segment, route$station_lines, length)
  names(counts)[2] <- paste0(prefix, "_stations")
  catches <- stats::aggregate(catch_amount ~ segment, route$station_lines, sum, na.rm = TRUE)
  names(catches)[2] <- paste0(prefix, "_catch")
  distances <- route$distances[!is.na(route$distances$segment), c("segment", "transit_nm"), drop = FALSE]
  names(distances)[2] <- paste0(prefix, "_nm")
  out <- merge(distances, counts, by = "segment", all.x = TRUE)
  out <- merge(out, catches, by = "segment", all.x = TRUE)
  out[[paste0(prefix, "_stations")]][is.na(out[[paste0(prefix, "_stations")]])] <- 0L
  out[[paste0(prefix, "_catch")]][is.na(out[[paste0(prefix, "_catch")]])] <- 0
  out
}

comparison_table <- function(init_route, final_route, segment_moves) {
  before <- segment_metrics(init_route, "before")
  after <- segment_metrics(final_route, "after")
  tbl <- merge(before, after, by = "segment", all = TRUE)
  tbl <- merge(tbl, segment_moves, by = "segment", all.x = TRUE)
  tbl$moved_out[is.na(tbl$moved_out)] <- 0L
  tbl$moved_in[is.na(tbl$moved_in)] <- 0L
  tbl <- tbl[order(tbl$segment), , drop = FALSE]

  out <- data.frame(
    `#` = as.character(tbl$segment),
    `|S|` = as.integer(tbl$before_stations),
    t = sprintf("%.0f", tbl$before_catch / 1000),
    nm = sprintf("%.0f", tbl$before_nm),
    `|S'|` = as.integer(tbl$after_stations),
    t = sprintf("%.0f", tbl$after_catch / 1000),
    nm = sprintf("%.0f", tbl$after_nm),
    `|S-|` = as.integer(tbl$moved_out),
    `|S+|` = as.integer(tbl$moved_in),
    check.names = FALSE
  )
  names(out) <- c("#", "|S|", "t", "nm", "|S'|", "t", "nm", "|S\u207b|", "|S\u207a|")

  total <- data.frame(
    `#` = "\u03a3",
    `|S|` = sum(tbl$before_stations),
    t = sprintf("%.0f", sum(tbl$before_catch) / 1000),
    nm = sprintf("%.0f", grand_transit(init_route)),
    `|S'|` = sum(tbl$after_stations),
    t = sprintf("%.0f", sum(tbl$after_catch) / 1000),
    nm = sprintf("%.0f", grand_transit(final_route)),
    `|S-|` = sum(tbl$moved_out),
    `|S+|` = sum(tbl$moved_in),
    check.names = FALSE
  )
  names(total) <- names(out)
  rbind(out, total)
}

comparison_table_grob <- function(table_data, palette) {
  panel_grey <- "grey92"

  fills <- unname(palette[table_data$`#`])
  is_segment <- !is.na(fills)
  fills[!is_segment] <- panel_grey

  # Apply alpha only to palette-coloured (segment) rows; grey rows stay fully opaque
  filled <- mapply(function(f, a) scales::alpha(f, a),
                   fills, ifelse(is_segment, 0.30, 1.0))
  fill_matrix <- matrix(filled, nrow = nrow(table_data), ncol = ncol(table_data))

  theme <- table_theme(fill_matrix, base_size = 8.2, colhead_fill = panel_grey)

  tg <- gridExtra::tableGrob(
    table_data,
    rows = NULL,
    theme = theme
  )

  tg <- gtable::gtable_add_rows(
    tg,
    heights = grid::unit(1.1, "lines"),
    pos = 0
  )

  header_fill <- "grey92"

  header_grob <- function(label) {
    grid::grobTree(
      grid::rectGrob(
        gp = grid::gpar(fill = header_fill, col = "white", lwd = 0.5)
      ),
      grid::textGrob(
        label,
        gp = grid::gpar(fontface = "bold", fontsize = 8.2)
      )
    )
  }

  # Group headers
  tg <- gtable::gtable_add_grob(tg, header_grob(""),       t = 1, l = 1, r = 1)
  tg <- gtable::gtable_add_grob(tg, header_grob("before"), t = 1, l = 2, r = 4)
  tg <- gtable::gtable_add_grob(tg, header_grob("after"),  t = 1, l = 5, r = 7)
  tg <- gtable::gtable_add_grob(tg, header_grob("change"), t = 1, l = 8, r = 9)

  # Helper vertical separator lines before before / after / change
  separator_grob <- function() {
    grid::segmentsGrob(
      x0 = grid::unit(0, "npc"),
      x1 = grid::unit(0, "npc"),
      y0 = grid::unit(0, "npc"),
      y1 = grid::unit(1, "npc"),
      gp = grid::gpar(col = "grey92", lwd = 0.8)
    )
  }

  for (col in c(2, 5, 8)) {
    tg <- gtable::gtable_add_grob(
      tg,
      separator_grob(),
      t = 1,
      b = nrow(tg),
      l = col,
      r = col,
      z = Inf
    )
  }

  tg
}

comparison_panel <- function(init_route, final_route, segment_moves,
                             total_moved, moved_pct, total_stations, palette) {
  table_data <- comparison_table(init_route, final_route, segment_moves)
  table_grob <- comparison_table_grob(table_data, palette)

  # Wrap in a viewport that pins the table to the top of the panel so it
  # doesn't float in the centre the way annotation_custom normally would.
  top_grob <- grid::grobTree(
    table_grob,
    vp = grid::viewport(
      x     = grid::unit(0.5, "npc"),
      y     = grid::unit(1,   "npc"),
      just  = c("centre", "top"),
      width = grid::unit(1, "npc"),
      height = sum(table_grob$heights)
    )
  )

  ggplot2::ggplot() +
    ggplot2::annotation_custom(top_grob, xmin = -Inf, xmax = Inf,
                                         ymin = -Inf, ymax = Inf) +
    ggplot2::labs(
      title = "C \u2014 Segment redistribution",
      subtitle = sprintf(
        "%d stations redistributed (%.0f%% of %d)",
        total_moved, moved_pct, total_stations
      )
    ) +
    gsp_map_theme(legend_position = "none") +
    hide_map_axes()
}

total_stations_moved <- function(con, solution_run_id) {
  rows <- db_read(con, "
    SELECT COALESCE(moved_stations, 0) AS moved_stations
    FROM solution.refinement_summary
    WHERE final_run_id = ?
  ", list(solution_run_id))
  if (nrow(rows) == 0L) return(0L)
  as.integer(rows$moved_stations[[1]])
}

moved_by_refinement_segment <- function(con, solution_run_id) {
  rows <- db_read(con, "
    SELECT to_segment AS segment, COUNT(*) AS moved
    FROM solution.refinement_station_moves
    WHERE run_id = (
      SELECT run_id
      FROM solution.refinement_summary
      WHERE final_run_id = ?
    )
    GROUP BY to_segment
    ORDER BY to_segment
  ", list(solution_run_id))
  rows$moved <- as.integer(rows$moved)
  rows
}

visited_ports <- function(ports, ...) {
  routes <- list(...)
  location_ids <- unique(unlist(lapply(routes, function(route) route$route_path$location_id)))
  ports[ports$location_id %in% location_ids, , drop = FALSE]
}

output_path_for_variant <- function(method, l2seg, output, variant_count) {
  if (is.null(output) || !nzchar(output)) {
    return(file.path("sol", method, sprintf("mh_phase1_%d.png", l2seg)))
  }
  if (variant_count == 1L) return(output)
  file.path(output, method, sprintf("mh_phase1_%d.png", l2seg))
}

save_camera_ready_panels <- function(output, p_a, p_b, p_c, table_data = NULL, palette = NULL) {
  base <- tools::file_path_sans_ext(output)
  dir.create(dirname(output), showWarnings = FALSE, recursive = TRUE)
  output_a <- paste0(base, "_A_before.png")
  output_b <- paste0(base, "_B_after.png")
  output_c <- paste0(base, "_C_table.tex")

  ggplot2::ggsave(output_a, p_a+ggplot2::ggtitle(NULL,NULL),
                  width = 4.2, height = 2.7, dpi = 300, bg = "white")
  ggplot2::ggsave(output_b, p_b+ggplot2::ggtitle(NULL,NULL),
                  width = 4.2, height = 2.7, dpi = 300, bg = "white")

  if (!is.null(table_data) && !is.null(palette)) {
    write_segment_comparison_latex(
      table_data,
      palette,
      output_c
    )
  }
  message("Wrote ", output_a)
  message("Wrote ", output_b)
  message("Wrote ", output_c)
}

latex_colour_name <- function(x) {
  paste0("seg", gsub("[^A-Za-z0-9]", "", x))
}

write_segment_comparison_latex <- function(table_data, palette, path) {
  rows <- table_data

  colour_defs <- character()
  for (nm in names(palette)) {
    col <- grDevices::col2rgb(palette[[nm]])[, 1]
    colour_defs <- c(
      colour_defs,
      sprintf(
        "\\definecolor{%s}{RGB}{%d,%d,%d}",
        latex_colour_name(nm), col[[1]], col[[2]], col[[3]]
      )
    )
  }

  row_to_latex <- function(i) {
    seg <- rows$`#`[[i]]
    vals <- as.character(rows[i, , drop = TRUE])

    prefix <- ""
    if (seg %in% names(palette)) {
      prefix <- sprintf("\\rowcolor{%s!30} ", latex_colour_name(seg))
    } else if (seg == "Σ") {
      prefix <- "\\rowcolor{gray!15} "
    }

    paste0(prefix, paste(vals, collapse = " & "), " \\\\")
  }

  lines <- c(
    "% Requires: \\usepackage[table]{xcolor}, \\usepackage{booktabs}",
    colour_defs,
    "\\begin{tabular}{rrrrrrrrr}",
    "\\toprule",
    "\\multicolumn{1}{c}{} & \\multicolumn{3}{c}{Before} & \\multicolumn{3}{c}{After} & \\multicolumn{2}{c}{Change} \\\\",
    "\\cmidrule(lr){2-4}\\cmidrule(lr){5-7}\\cmidrule(lr){8-9}",
    paste(names(rows), collapse = " & "),
    "\\\\",
    "\\midrule",
    vapply(seq_len(nrow(rows)), row_to_latex, character(1L)),
    "\\bottomrule",
    "\\end{tabular}"
  )

  writeLines(lines, path, useBytes = TRUE)
}

plot_variant <- function(con, opt, variant, output) {
  if (parse_bool(opt$skip_existing) && file.exists(output)) {
    message("Skipping ", variant$method, " L2SEG=", variant$l2seg, " (file exists): ", output)
    return(invisible(FALSE))
  }

  country <- read_country_layers(con)
  init_route <- read_route_run(con, variant$init_run_id)
  final_route <- read_route_run(con, variant$final_run_id)
  total_moved <- unique_stations_moved(con, variant$final_run_id)
  total_stations <- station_count(init_route)
  moved_pct <- if (total_stations == 0L) 0 else 100 * total_moved / total_stations
  segment_moves <- unique_moved_by_final_segment(con, variant$final_run_id)
  ports <- visited_ports(read_ports(con, parse_id_list(opt$ports)), init_route, final_route)
  vessels <- read_vessels(con, init_route$run$boat_id)

  all_route_points <- rbind(
    init_route$route_path[, c("lon", "lat"), drop = FALSE],
    final_route$route_path[, c("lon", "lat"), drop = FALSE]
  )
  bounds <- plot_bounds(country$coastline, all_route_points, pad = 0.001)

  n_seg <- max(
    init_route$route_path$segment,
    final_route$route_path$segment,
    na.rm = TRUE
  )
  palette <- make_segment_palette(n_seg)
  caption_text <- sprintf(
    "%s phase 1: %s",
    method_code_label(variant$method),
    method_refinement_label(variant$method)
  )

  p_a <- plot_country_or_route(
    country = country,
    route = init_route,
    ports = ports,
    vessels = vessels,
    table_corner = opt$table_corner_a,
    palette = palette,
    title = "A \u2014 Before refinement",
    subtitle = sprintf("Transit %.0f nm", grand_transit(init_route)),
    legend_position = "none",
    bounds_override = bounds,
    station_col_label = "|S|",
    show_table = FALSE
  )

  p_b <- plot_country_or_route(
    country = country,
    route = final_route,
    ports = ports,
    vessels = vessels,
    table_corner = opt$table_corner_b,
    palette = palette,
    title = bquote("B \u2014 After " * .(variant$n_sweeps) * " sweeps using " * L[2*seg] == .(variant$l2seg) * " s"),
    subtitle = sprintf(
      "Transit %.0f nm | Improvement %s | Runtime %s",
      grand_transit(final_route),
      improvement_label(init_route, final_route),
      format_runtime(final_route$run$runtime_seconds[[1]])
    ),
    legend_position = "none",
    bounds_override = bounds,
    moved_by_segment = segment_moves,
    station_col_label = "|S'|",
    show_table = FALSE
  )
  p_c <- comparison_panel(
    init_route = init_route,
    final_route = final_route,
    segment_moves = segment_moves,
    total_moved = total_moved,
    moved_pct = moved_pct,
    total_stations = total_stations,
    palette = palette
  )

  if (parse_bool(opt$camera_ready)) {
    table_data <- comparison_table(init_route, final_route, segment_moves)

    save_camera_ready_panels(
      output = output,
      p_a = p_a,
      p_b = p_b,
      p_c = p_c,
      table_data = table_data,
      palette = palette
    )
  }

  panel_row <- cowplot::plot_grid(p_a, p_b, p_c, ncol = 3, align = "v", axis = "t",
                                  rel_widths = c(.4, .4, 0.25))
  title <- cowplot::ggdraw() +
    cowplot::draw_label(caption_text, fontface = "bold", size = 16, x = 0.5, hjust = 0.5)
  footnote <- cowplot::ggdraw() +
    cowplot::draw_label(
      "S and S\u2019 are segment station sets before and after refinement; |S\u207b| and |S\u207a| are stations redistributed out of and into each segment.",
      size = 8.5,
      x = 0.5,
      hjust = 0.5
    )
  combined <- cowplot::plot_grid(title, panel_row, footnote, ncol = 1, rel_heights = c(0.06, 0.72, 0.06))
  ggplot2::ggsave(output, combined, width = 11.5, height = 4.2, dpi = 300, bg = "white")
  message("Wrote ", output)
  invisible(TRUE)
}

main <- function() {
  load_gsp_plot_packages()
  opt <- parse_args(commandArgs(trailingOnly = TRUE))
  methods <- parse_methods(opt$method)
  l2seg_values <- parse_l2seg(opt$l2seg)

  con <- connect_gsp_db(opt$gsp_db, opt$solution_db)
  on.exit(DBI::dbDisconnect(con), add = TRUE)

  variants <- read_refinement_variants(con, methods, l2seg_values)
  if (nrow(variants) == 0L) {
    message("No refinement variants found.")
    return(invisible(FALSE))
  }

  for (i in seq_len(nrow(variants))) {
    variant <- variants[i, , drop = FALSE]
    output <- output_path_for_variant(variant$method, variant$l2seg, opt$output, nrow(variants))
    tryCatch(
      plot_variant(con, opt, variant, output),
      error = function(e) {
        message(
          "Skipping ", variant$method, " L2SEG=", variant$l2seg,
          " (unable to plot): ", conditionMessage(e)
        )
      }
    )
  }
}

main()
