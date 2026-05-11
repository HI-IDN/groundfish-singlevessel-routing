#!/usr/bin/env Rscript

source("R/gsp_db.R")
source("R/gsp_plot_utils.R")

parse_args <- function(args) {
  out <- list(
    gsp_db        = "dat/gsp.db",
    solution_db   = "dat/solution.db",
    method        = "all",
    phase         = "all",
    run_ids       = "final",   # "final" = auto-select; or comma-separated run_ids
    output        = NULL,
    ports         = "all",
    title         = NULL,
    skip_existing = "true"
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
  value <- tolower(trimws(x))
  if (value %in% c("1", "true",  "t", "yes", "y")) return(TRUE)
  if (value %in% c("0", "false", "f", "no",  "n")) return(FALSE)
  stop(sprintf("Invalid boolean value: %s", x), call. = FALSE)
}

parse_id_list <- function(x) {
  if (is.null(x) || x %in% c("all", "ALL", "*"))  return(NULL)
  if (x %in% c("none", "NONE", "-"))               return(integer())
  as.integer(strsplit(x, ",", fixed = TRUE)[[1]])
}

parse_str_list <- function(x) {
  if (is.null(x) || !nzchar(x) || x %in% c("all", "ALL", "*")) return(NULL)
  trimws(strsplit(x, ",", fixed = TRUE)[[1]])
}

parse_run_ids <- function(x) {
  if (is.null(x) || x %in% c("final", "FINAL", "")) return(NULL)
  trimws(strsplit(x, ",", fixed = TRUE)[[1]])
}

# ---------------------------------------------------------------------------
# Colour helpers — each boat keeps one base color; segments lighten gradually
# ---------------------------------------------------------------------------

mix_with_white <- function(color, amount) {
  v     <- grDevices::col2rgb(color) / 255
  mixed <- v * (1 - amount) + amount
  grDevices::rgb(mixed[1], mixed[2], mixed[3])
}

make_boat_segment_palette <- function(base_color, n_seg) {
  amounts <- if (n_seg <= 1L) 0.05 else seq(0.0, 0.55, length.out = n_seg)
  vapply(amounts, function(a) mix_with_white(base_color, a), character(1L))
}

# ---------------------------------------------------------------------------
# DB helpers
# ---------------------------------------------------------------------------

# One row per boat: the latest is_final run matching the supplied filters.
read_multivessel_runs <- function(con, methods = NULL, phases = NULL,
                                  run_ids = NULL) {
  if (!is.null(run_ids) && length(run_ids) > 0L) {
    ph  <- paste(rep("?", length(run_ids)), collapse = ",")
    sql <- sprintf("
      SELECT
        r.run_id,  r.method,  r.phase,  r.solution_key,
        r.boat_id, r.boat_name, r.n_segments, r.feasible,
        r.runtime_seconds,
        b.capacity AS boat_capacity,
        l.lon AS boat_lon, l.lat AS boat_lat
      FROM solution.runs r
      LEFT JOIN boats     b ON b.id = r.boat_id
      LEFT JOIN locations l ON l.id = b.location_id
      WHERE r.run_id IN (%s)
      ORDER BY r.boat_id, r.run_id
    ", ph)
    return(db_read(con, sql, as.list(run_ids)))
  }

  filters <- "WHERE is_final = 1"
  params  <- list()
  if (!is.null(methods) && length(methods) > 0L) {
    ph      <- paste(rep("?", length(methods)), collapse = ",")
    filters <- paste(filters, sprintf("AND method IN (%s)", ph))
    params  <- c(params, as.list(methods))
  }
  if (!is.null(phases) && length(phases) > 0L) {
    ph      <- paste(rep("?", length(phases)), collapse = ",")
    filters <- paste(filters, sprintf("AND phase IN (%s)", ph))
    params  <- c(params, as.list(phases))
  }

  # Take the latest final run per boat to avoid duplicates across reruns
  sql <- sprintf("
    SELECT
      r.run_id,  r.method,  r.phase,  r.solution_key,
      r.boat_id, r.boat_name, r.n_segments, r.feasible,
      r.runtime_seconds,
      b.capacity AS boat_capacity,
      l.lon AS boat_lon, l.lat AS boat_lat
    FROM solution.runs r
    INNER JOIN (
      SELECT boat_id, MAX(run_id) AS max_run_id
      FROM solution.runs
      %s
      GROUP BY boat_id
    ) latest ON latest.boat_id  = r.boat_id
           AND latest.max_run_id = r.run_id
    LEFT JOIN boats     b ON b.id = r.boat_id
    LEFT JOIN locations l ON l.id = b.location_id
    ORDER BY r.boat_id
  ", filters)

  db_read(con, sql, if (length(params) > 0L) params else NULL)
}

grand_transit_nm <- function(route) {
  tot <- route$distances[is.na(route$distances$segment), , drop = FALSE]
  if (nrow(tot) > 0L) return(tot$transit_nm[[1L]])
  sum(route$distances$transit_nm, na.rm = TRUE)
}

# ---------------------------------------------------------------------------
# Per-boat segment stat table inset
# ---------------------------------------------------------------------------

make_boat_table <- function(route, seg_colors) {
  d       <- route$distances[!is.na(route$distances$segment), , drop = FALSE]
  counts  <- stats::aggregate(station_id  ~ segment, route$station_lines, length)
  catches <- stats::aggregate(catch_amount ~ segment, route$station_lines,
                               sum, na.rm = TRUE)
  names(counts)[2L]  <- "stations"
  names(catches)[2L] <- "catch"

  tbl <- merge(d, counts,  by = "segment", all.x = TRUE)
  tbl <- merge(tbl, catches, by = "segment", all.x = TRUE)
  tbl$stations[is.na(tbl$stations)] <- 0L
  tbl$catch[is.na(tbl$catch)]       <- 0
  tbl <- tbl[order(tbl$segment), , drop = FALSE]

  out <- data.frame(
    `#`   = as.character(tbl$segment),
    `|S|` = as.integer(tbl$stations),
    t     = sprintf("%.0f", tbl$catch / 1000),
    nm    = sprintf("%.0f", tbl$transit_nm),
    check.names = FALSE
  )
  total <- data.frame(
    `#`   = "\u03a3",
    `|S|` = sum(tbl$stations),
    t     = sprintf("%.0f", sum(tbl$catch) / 1000),
    nm    = sprintf("%.0f", sum(tbl$transit_nm)),
    check.names = FALSE
  )
  names(total) <- names(out)
  out <- rbind(out, total)

  n_seg    <- nrow(out) - 1L   # last row is the Σ total
  seg_fill <- scales::alpha(seg_colors[seq_len(n_seg)], 0.30)
  fill_mat <- matrix(
    c(rep(seg_fill, each = ncol(out)),
      rep("grey92",  ncol(out))),
    nrow = nrow(out), ncol = ncol(out), byrow = TRUE
  )
  list(data = out, fills = fill_mat)
}

# ---------------------------------------------------------------------------
# Default output path
# ---------------------------------------------------------------------------

output_path_default <- function(methods, phases) {
  file.path("sol", "survey", "multivessel.png")
}

# ---------------------------------------------------------------------------
# Main plot function
# ---------------------------------------------------------------------------

plot_multivessel <- function(con, opt) {
  methods <- parse_str_list(opt$method)
  phases  <- parse_str_list(opt$phase)
  run_ids <- parse_run_ids(opt$run_ids)
  output  <- if (is.null(opt$output) || !nzchar(opt$output)) {
    output_path_default(methods, phases)
  } else {
    opt$output
  }

  if (parse_bool(opt$skip_existing) && file.exists(output)) {
    message("Skipping (file exists): ", output)
    return(invisible(FALSE))
  }

  runs <- read_multivessel_runs(con, methods, phases, run_ids)
  if (nrow(runs) == 0L) {
    message("No final runs found for method=", opt$method,
            " phase=", opt$phase)
    return(invisible(FALSE))
  }

  n_boats     <- nrow(runs)
  boat_colors <- make_multivessel_palette(n_boats)
  country     <- read_country_layers(con)
  all_ports   <- read_ports(con, parse_id_list(opt$ports))

  all_route   <- NULL
  all_stlines <- NULL
  boat_rows   <- vector("list", n_boats)
  tbl_insets  <- vector("list", n_boats)

  for (i in seq_len(n_boats)) {
    r        <- runs[i, , drop = FALSE]
    base_col <- boat_colors[[i]]
    route    <- read_route_run(con, r$run_id)
    n_seg    <- max(route$route_path$segment, na.rm = TRUE)
    seg_cols <- make_boat_segment_palette(base_col, n_seg)

    rp            <- route$route_path
    rp$boat_idx   <- i
    rp$seg_color  <- seg_cols[rp$segment]
    rp$seg_key    <- paste0(r$boat_name, "_", rp$segment)
    all_route     <- rbind(all_route, rp)

    sl           <- route$station_lines
    sl$boat_idx  <- i
    sl$seg_color <- seg_cols[sl$segment]
    sl$seg_key   <- paste0(r$boat_name, "_", sl$segment)
    all_stlines  <- rbind(all_stlines, sl)

    tbl <- make_boat_table(route, seg_cols)
    tbl_insets[[i]] <- list(data = tbl$data, fills = tbl$fills,
                             boat_name = r$boat_name)

    boat_rows[[i]] <- data.frame(
      boat_id    = r$boat_id,
      boat_name  = r$boat_name,
      boat_lon   = r$boat_lon,
      boat_lat   = r$boat_lat,
      base_color = base_col,
      transit_nm = grand_transit_nm(route),
      n_stations = length(unique(route$station_lines$station_id)),
      n_segments = n_seg,
      feasible   = as.logical(r$feasible),
      stringsAsFactors = FALSE
    )
  }

  boat_summary <- do.call(rbind, boat_rows)

  # Colour mapping: one entry per unique segment key
  color_map <- stats::setNames(all_route$seg_color, all_route$seg_key)
  color_map <- color_map[!duplicated(names(color_map))]

  bounds <- plot_bounds(country$coastline, all_route[, c("lon", "lat")])

  # Only show ports that appear as waypoints in the combined route
  vis_ports <- all_ports[
    all_ports$location_id %in% unique(all_route$location_id), , drop = FALSE]

  title_text <- if (!is.null(opt$title) && nzchar(opt$title)) {
    opt$title
  } else {
    "Multi-Vessel Survey Routes 2023"
  }
  capacity_label <- {
    caps <- runs$boat_capacity[!is.na(runs$boat_capacity)]
    if (length(caps) > 0L && length(unique(caps)) == 1L) {
      sprintf("%d t", as.integer(caps[[1L]]) %/% 1000L)
    } else {
      "mixed"
    }
  }
  subtitle_text <- sprintf(
    "Stations %d | Vessels %d | Capacity %s | Segments %d | Transit %.0f nm",
    sum(boat_summary$n_stations),
    n_boats,
    capacity_label,
    sum(boat_summary$n_segments),
    sum(boat_summary$transit_nm)
  )

  # ---- Build ggplot --------------------------------------------------------
  p <- ggplot2::ggplot() +
    ggplot2::geom_path(
      data = country$coastline,
      ggplot2::aes(x = lon, y = lat),
      color = "grey35", linewidth = 0.25
    ) +
    ggplot2::geom_path(
      data = all_route,
      ggplot2::aes(x = lon, y = lat,
                   group = interaction(boat_idx, segment),
                   color = seg_key),
      linewidth = 0.30, alpha = 0.45, lineend = "round"
    ) +
    ggplot2::geom_segment(
      data = all_stlines,
      ggplot2::aes(x = start_lon, y = start_lat,
                   xend = end_lon, yend = end_lat,
                   color = seg_key),
      linewidth = 0.80, alpha = 0.88, lineend = "round"
    ) +
    ggplot2::scale_color_manual(values = color_map, guide = "none")

  # Port markers
  if (nrow(vis_ports) > 0L) {
    p <- p + ggplot2::geom_point(
      data = vis_ports,
      ggplot2::aes(x = lon, y = lat),
      shape = 22, size = 2.4, stroke = 0.5,
      fill = "#D62728", color = "grey15",
      inherit.aes = FALSE
    )
  }

  # Boat home markers + name labels nudged slightly north
  nudge_y <- diff(c(bounds$ymin, bounds$ymax)) * 0.025
  p <- p +
    ggplot2::geom_point(
      data = boat_summary,
      ggplot2::aes(x = boat_lon, y = boat_lat, fill = boat_name),
      shape = 23, size = 3.0, stroke = 0.6, color = "grey15",
      inherit.aes = FALSE
    ) +
    ggplot2::geom_label(
      data = boat_summary,
      ggplot2::aes(x = boat_lon, y = boat_lat + nudge_y,
                   label = boat_name, fill = boat_name),
      size = 2.8, linewidth = 0.2,
      label.padding = grid::unit(0.15, "lines"),
      inherit.aes = FALSE, show.legend = FALSE
    ) +
    ggplot2::scale_fill_manual(
      values = stats::setNames(boat_summary$base_color, boat_summary$boat_name),
      guide  = "none"
    )

  # Per-boat segment stat tables — cycle four corners
  corners <- list(
    list(x = bounds$xmax, y = bounds$ymax, hjust =  1.02, vjust =  1.02),
    list(x = bounds$xmin, y = bounds$ymax, hjust = -0.02, vjust =  1.02),
    list(x = bounds$xmax, y = bounds$ymin, hjust =  1.02, vjust = -0.02),
    list(x = bounds$xmin, y = bounds$ymin, hjust = -0.02, vjust = -0.02)
  )
  for (i in seq_along(tbl_insets)) {
    inset  <- tbl_insets[[i]]
    corner <- corners[[(i - 1L) %% length(corners) + 1L]]
    p <- p + geom_gsp_table(
      data    = data.frame(lon   = corner$x,
                           lat   = corner$y,
                           label = I(list(inset$data))),
      ggplot2::aes(x = lon, y = lat, label = label),
      hjust       = corner$hjust,
      vjust       = corner$vjust,
      table.theme = table_theme(inset$fills, base_size = 7.5)
    )
  }

  p <- p +
    fixed_map_coord(bounds, clip = "off") +
    ggplot2::labs(
      title    = title_text,
      subtitle = subtitle_text,
      caption  = paste(
        "Each boat keeps one base color; segments fade lighter as the route progresses.",
        "Source: Icelandic Marine and Freshwater Research Institute (Hafranns\u00f3knastofnun) \u00a9 2023",
        sep = "\n"
      ),
      x = NULL, y = NULL
    ) +
    gsp_map_theme(legend_position = "none") +
    hide_map_axes() +
    ggplot2::theme(
      plot.caption = ggplot2::element_text(size = 7.5, hjust = 0.5,
                                           color = "grey40",
                                           margin = ggplot2::margin(t = 6)),
      # Extra right margin gives physical space for corner table insets
      plot.margin  = ggplot2::margin(5, 60, 5, 5)
    )

  dir.create(dirname(output), showWarnings = FALSE, recursive = TRUE)
  ggplot2::ggsave(output, p, width = 10.0, height = 7.5, dpi = 300, bg = "white")
  message("Wrote ", output)
  invisible(TRUE)
}

main <- function() {
  load_gsp_plot_packages()
  opt <- parse_args(commandArgs(trailingOnly = TRUE))
  con <- connect_gsp_db(opt$gsp_db, opt$solution_db)
  on.exit(DBI::dbDisconnect(con), add = TRUE)
  plot_multivessel(con, opt)
}

main()

