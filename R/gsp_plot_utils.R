#!/usr/bin/env Rscript

load_gsp_plot_packages <- function() {
  for (pkg in c("ggplot2", "grid", "gridExtra", "gtable", "ggpp", "scales")) {
    available <- suppressWarnings(suppressPackageStartupMessages(
      requireNamespace(pkg, quietly = TRUE)
    ))
    if (!available) {
      stop(sprintf("Missing required R package: %s", pkg), call. = FALSE)
    }
  }
  # Set a consistent global base theme so all ggplot objects share the same
  # font sizes without each call site having to repeat them.
  ggplot2::theme_set(
    ggplot2::theme_minimal(base_size = 10) +
      ggplot2::theme(
        plot.background = ggplot2::element_rect(fill = "white", color = NA)
      )
  )
}

GeomGspTable <- ggplot2::ggproto(
  "GeomGspTable",
  ggplot2::Geom,
  required_aes = c("x", "y", "label"),
  default_aes = ggplot2::aes(angle = 0),
  draw_key = ggplot2::draw_key_blank,
  draw_panel = function(data, panel_params, coord, table.theme,
                        table.rownames = FALSE, table.colnames = TRUE,
                        hjust = 0.5, vjust = 0.5) {
    if (nrow(data) == 0L) return(grid::nullGrob())
    data <- coord$transform(data, panel_params)
    grobs <- grid::gList()

    for (i in seq_len(nrow(data))) {
      table_data <- data$label[[i]]
      row_outlines <- attr(table_data, "row_outlines", exact = TRUE)
      user_grob <- gridExtra::tableGrob(
        d = table_data,
        theme = table.theme,
        rows = if (table.rownames) rownames(table_data) else NULL,
        cols = if (table.colnames) colnames(table_data) else NULL
      )

      if (!is.null(row_outlines) && length(row_outlines) > 0L) {
        for (row_idx in row_outlines) {
          table_row <- row_idx + if (table.colnames) 1L else 0L
          user_grob <- gtable::gtable_add_grob(
            user_grob,
            grid::rectGrob(gp = grid::gpar(fill = NA, col = "black", lwd = 1.4)),
            t = table_row,
            l = 1L,
            b = table_row,
            r = ncol(table_data),
            z = Inf,
            clip = "off",
            name = paste0("row-outline-", row_idx)
          )
        }
      }

      user_grob$vp <- grid::viewport(
        x = grid::unit(data$x[[i]], "native"),
        y = grid::unit(data$y[[i]], "native"),
        width = sum(user_grob$widths),
        height = sum(user_grob$heights),
        just = c(hjust, vjust),
        angle = data$angle[[i]]
      )
      grobs <- grid::gList(grobs, user_grob)
    }

    grid::grobTree(children = grobs)
  }
)

geom_gsp_table <- function(mapping = NULL, data = NULL, ..., table.theme,
                           hjust = 0.5, vjust = 0.5,
                           table.rownames = FALSE, table.colnames = TRUE,
                           inherit.aes = FALSE) {
  ggplot2::layer(
    geom = GeomGspTable,
    mapping = mapping,
    data = data,
    stat = "identity",
    position = "identity",
    show.legend = FALSE,
    inherit.aes = inherit.aes,
    params = list(
      table.theme = table.theme,
      hjust = hjust,
      vjust = vjust,
      table.rownames = table.rownames,
      table.colnames = table.colnames,
      ...
    )
  )
}

maximize_adjacent_contrast <- function(colors) {
  colors <- unname(colors)
  n <- length(colors)
  if (n <= 2L) return(colors)

  rgb <- t(grDevices::col2rgb(colors) / 255)
  lab <- grDevices::convertColor(rgb, from = "sRGB", to = "Lab", scale.in = 1)
  d <- as.matrix(stats::dist(lab))
  first_pair <- which(d == max(d), arr.ind = TRUE)[1, ]
  out <- c(first_pair[1], first_pair[2])
  remaining <- setdiff(seq_len(n), out)

  while (length(remaining) > 0L) {
    next_idx <- remaining[which.max(d[out[length(out)], remaining])]
    out <- c(out, next_idx)
    remaining <- setdiff(remaining, next_idx)
  }

  colors[out]
}

make_segment_palette <- function(n) {
  if (n <= 0L) return(character())
  colors <- if (requireNamespace("Polychrome", quietly = TRUE)) {
    env <- new.env(parent = emptyenv())
    utils::data("Dark24", package = "Polychrome", envir = env)
    if (n <= length(env$Dark24)) {
      maximize_adjacent_contrast(env$Dark24)[seq_len(n)]
    } else {
      maximize_adjacent_contrast(Polychrome::createPalette(n, seedcolors = env$Dark24[1:6]))
    }
  } else {
    maximize_adjacent_contrast(grDevices::hcl.colors(n, "Dark 3"))
  }
  stats::setNames(colors, as.character(seq_len(n)))
}

make_multivessel_palette <- function(n) {
  colors <- grDevices::hcl.colors(max(n, 1L), palette = "Dynamic")
  stats::setNames(colors, as.character(seq_len(n)))
}

hide_map_axes <- function() {
  ggplot2::theme(
    axis.title = ggplot2::element_blank(),
    axis.text = ggplot2::element_blank(),
    axis.ticks = ggplot2::element_blank()
  )
}

gsp_map_theme <- function(legend_position = "right", legend_justification = "center") {
  ggplot2::theme_minimal(base_size = 10) +
    ggplot2::theme(
      panel.grid = ggplot2::element_blank(),
      plot.background = ggplot2::element_rect(fill = "white", color = NA),
      plot.title    = ggplot2::element_text(size = ggplot2::rel(1.2), hjust = 0),
      plot.subtitle = ggplot2::element_text(size = ggplot2::rel(1.0), hjust = 0),
      legend.position = legend_position,
      legend.justification = legend_justification,
      legend.box = "horizontal"
    )
}

degree_map_axes <- function() {
  list(
    ggplot2::scale_x_continuous(labels = function(x) paste0(x, "\u00b0")),
    ggplot2::scale_y_continuous(labels = function(y) paste0(y, "\u00b0"))
  )
}

plot_bounds <- function(coastline, route_path = NULL, extra_points = NULL, pad = 0.04) {
  if (!is.null(route_path) && nrow(route_path) > 0L) {
    x <- route_path$lon
    y <- route_path$lat
  } else {
    x <- coastline$lon
    y <- coastline$lat
  }
  if (!is.null(extra_points) && nrow(extra_points) > 0L) {
    x <- c(x, extra_points$lon)
    y <- c(y, extra_points$lat)
  }
  xr <- range(x, na.rm = TRUE)
  yr <- range(y, na.rm = TRUE)
  xp <- max(diff(xr) * pad, 0.05)
  yp <- max(diff(yr) * pad, 0.05)
  list(xmin = xr[1] - xp, xmax = xr[2] + xp, ymin = yr[1] - yp, ymax = yr[2] + yp)
}

fixed_map_coord <- function(bounds, clip = "on") {
  ratio <- 1 / cos(mean(c(bounds$ymin, bounds$ymax)) * pi / 180)
  ggplot2::coord_fixed(
    ratio = ratio,
    xlim = c(bounds$xmin, bounds$xmax),
    ylim = c(bounds$ymin, bounds$ymax),
    expand = FALSE,
    clip = clip
  )
}

table_position <- function(bounds, corner = "upper_right") {
  x_pad <- diff(c(bounds$xmin, bounds$xmax)) * 0.055
  x <- switch(corner,
    upper_left = bounds$xmin + x_pad,
    lower_left = bounds$xmin + x_pad,
    west = bounds$xmin + x_pad,
    upper_right = bounds$xmax + x_pad,
    lower_right = bounds$xmax + x_pad,
    east = bounds$xmax + x_pad,
    bounds$xmax
  )
  y <- switch(corner,
    lower_left = bounds$ymin,
    lower_right = bounds$ymin,
    south = bounds$ymin,
    upper_left = bounds$ymax,
    upper_right = bounds$ymax,
    north = bounds$ymax,
    bounds$ymax
  )
  list(
    x = x,
    y = y,
    hjust = if (grepl("left|west", corner)) -0.02 else 1.02,
    vjust = if (grepl("lower|south", corner)) -0.02 else 1.02
  )
}

table_theme <- function(fill_matrix, border_matrix = NULL, border_lwd_matrix = NULL,
                        base_size = 9, colhead_fill = "white") {
  if (is.null(border_matrix)) {
    border_matrix <- matrix(NA, nrow = nrow(fill_matrix), ncol = ncol(fill_matrix))
  }
  if (is.null(border_lwd_matrix)) {
    border_lwd_matrix <- matrix(0, nrow = nrow(fill_matrix), ncol = ncol(fill_matrix))
  }

  gridExtra::ttheme_default(
    base_size = base_size,
    padding = grid::unit(c(4.2, 5.0), "pt"),
    core = list(
      bg_params = list(fill = fill_matrix, col = border_matrix, lwd = border_lwd_matrix),
      fg_params = list(col = "grey10")
    ),
    colhead = list(
      bg_params = list(fill = colhead_fill, col = NA),
      fg_params = list(fontface = "bold", col = "grey10")
    )
  )
}

station_summary_table <- function(stations) {
  out <- data.frame(
    metric = c("Stations", "Catch (t)"),
    value = c(nrow(stations), sprintf("%.0f", sum(stations$catch_amount, na.rm = TRUE) / 1000)),
    check.names = FALSE
  )
  list(data = out, fills = matrix("grey95", nrow = nrow(out), ncol = ncol(out)))
}

segment_summary_table <- function(route, palette, moved_by_segment = NULL,
                                  station_col_label = "|S|") {
  seg_d <- route$distances[!is.na(route$distances$segment), , drop = FALSE]
  total_d <- route$distances[is.na(route$distances$segment), , drop = FALSE]
  counts <- stats::aggregate(station_id ~ segment, route$station_lines, length)
  names(counts)[2] <- "stations"
  catches <- stats::aggregate(catch_amount ~ segment, route$station_lines, sum, na.rm = TRUE)
  names(catches)[2] <- "catch_amount"
  tbl <- merge(seg_d, counts, by = "segment", all.x = TRUE)
  tbl <- merge(tbl, catches, by = "segment", all.x = TRUE)
  if (!is.null(moved_by_segment)) {
    tbl <- merge(tbl, moved_by_segment, by = "segment", all.x = TRUE)
  }
  tbl$stations[is.na(tbl$stations)] <- 0L
  tbl$catch_amount[is.na(tbl$catch_amount)] <- 0
  if ("moved" %in% names(tbl)) tbl$moved[is.na(tbl$moved)] <- 0L
  if ("moved_out" %in% names(tbl)) tbl$moved_out[is.na(tbl$moved_out)] <- 0L
  if ("moved_in" %in% names(tbl)) tbl$moved_in[is.na(tbl$moved_in)] <- 0L

  out <- data.frame(
    `#` = as.character(tbl$segment),
    stations = as.integer(tbl$stations),
    t = sprintf("%.0f", tbl$catch_amount / 1000),
    nm = sprintf("%.0f", tbl$transit_nm),
    check.names = FALSE
  )
  names(out)[names(out) == "stations"] <- station_col_label
  if ("moved" %in% names(tbl)) {
    out$`ΔS` <- as.integer(tbl$moved)
  }
  if (all(c("moved_out", "moved_in") %in% names(tbl))) {
    out$`|S^-|` <- as.integer(tbl$moved_out)
    out$`|S^+|` <- as.integer(tbl$moved_in)
  }

  if (nrow(seg_d) > 1L && nrow(total_d) > 0L) {
    total_row <- data.frame(
      `#` = "\u03a3",
      stations = sum(tbl$stations),
      t = sprintf("%.0f", sum(tbl$catch_amount) / 1000),
      nm = sprintf("%.0f", total_d$transit_nm[1]),
      check.names = FALSE
    )
    names(total_row)[names(total_row) == "stations"] <- station_col_label
    if ("moved" %in% names(tbl)) {
      total_row$`ΔS` <- sum(tbl$moved)
    }
    if (all(c("moved_out", "moved_in") %in% names(tbl))) {
      total_row$`|S^-|` <- sum(tbl$moved_out)
      total_row$`|S^+|` <- sum(tbl$moved_in)
    }
    names(total_row) <- names(out)
    out <- rbind(out, total_row)
  }

  fills <- unname(palette[as.character(tbl$segment)])
  if (nrow(seg_d) > 1L && nrow(total_d) > 0L) fills <- c(fills, "grey92")
  fill_matrix <- matrix(scales::alpha(fills, 0.30), nrow = nrow(out), ncol = ncol(out))
  border_matrix <- matrix(NA, nrow = nrow(out), ncol = ncol(out))
  border_lwd_matrix <- matrix(0, nrow = nrow(out), ncol = ncol(out))

  capacity <- route$run$boat_capacity[[1]]
  if (!is.null(capacity) && length(capacity) > 0L && !is.na(capacity)) {
    over_capacity <- tbl$catch_amount > capacity
    if (any(over_capacity)) {
      tonnage_col <- which(names(out) == "t")
      border_matrix[seq_len(nrow(tbl))[over_capacity], tonnage_col] <- "#d62728"
      border_lwd_matrix[seq_len(nrow(tbl))[over_capacity], tonnage_col] <- 2
    }
  }

  if ("moved" %in% names(tbl)) {
    moved_rows <- seq_len(nrow(tbl))[tbl$moved != 0L]
    if (length(moved_rows) > 0L) {
      attr(out, "row_outlines") <- moved_rows
    }
  }
  if (all(c("moved_out", "moved_in") %in% names(tbl))) {
    moved_rows <- seq_len(nrow(tbl))[tbl$moved_out != 0L | tbl$moved_in != 0L]
    if (length(moved_rows) > 0L) {
      attr(out, "row_outlines") <- moved_rows
    }
  }

  list(
    data = out,
    fills = fill_matrix,
    borders = border_matrix,
    border_lwd = border_lwd_matrix
  )
}

add_ports_and_vessels <- function(p, ports = NULL, vessels = NULL) {
  markers <- data.frame()
  if (!is.null(ports) && nrow(ports) > 0L) {
    markers <- rbind(markers, data.frame(
      lon = ports$lon,
      lat = ports$lat,
      marker = "Ports",
      stringsAsFactors = FALSE
    ))
  }

  if (!is.null(vessels) && nrow(vessels) > 0L) {
    markers <- rbind(markers, data.frame(
      lon = vessels$lon,
      lat = vessels$lat,
      marker = "Boats",
      stringsAsFactors = FALSE
    ))
  }

  if (nrow(markers) > 0L) {
    markers$marker <- factor(markers$marker, levels = c("Ports", "Boats"))
    p <- p +
      ggplot2::geom_point(
        data = markers,
        ggplot2::aes(x = lon, y = lat, shape = marker, fill = marker),
        size = 2.8,
        stroke = 0.6,
        color = "grey15",
        inherit.aes = FALSE
      ) +
      ggplot2::scale_shape_manual(
        name = NULL,
        values = c(Ports = 22, Boats = 23),
        drop = FALSE
      ) +
      ggplot2::scale_fill_manual(
        name = NULL,
        values = c(Ports = "#D62728", Boats = "#FFD24A"),
        drop = FALSE,
        guide = "none"
      )
  }

  p
}

plot_country_or_route <- function(country, route = NULL, ports = NULL, vessels = NULL,
                                  table_corner = "upper_right", palette = NULL,
                                  title = NULL, subtitle = NULL, show_degree_axes = FALSE,
                                  legend_position = NULL,
                                  legend_justification = NULL,
                                  bounds_override = NULL,
                                  moved_by_segment = NULL,
                                  station_col_label = "|S|",
                                  show_table = TRUE) {
  has_route <- !is.null(route) && nrow(route$route_path) > 0L
  if (is.null(legend_position)) legend_position <- if (has_route) "right" else "bottom"
  if (is.null(legend_justification)) legend_justification <- "center"
  extra_points <- NULL
  if (!has_route) {
    station_points <- rbind(
      data.frame(lon = country$stations$start_lon, lat = country$stations$start_lat),
      data.frame(lon = country$stations$end_lon, lat = country$stations$end_lat)
    )
    marker_points <- rbind(
      if (!is.null(ports) && nrow(ports) > 0L) data.frame(lon = ports$lon, lat = ports$lat) else NULL,
      if (!is.null(vessels) && nrow(vessels) > 0L) data.frame(lon = vessels$lon, lat = vessels$lat) else NULL
    )
    extra_points <- rbind(station_points, marker_points)
  }
  bounds <- if (is.null(bounds_override)) {
    plot_bounds(country$coastline, if (has_route) route$route_path else NULL, extra_points)
  } else {
    bounds_override
  }
  pos <- table_position(bounds, table_corner)

  p <- ggplot2::ggplot() +
    ggplot2::geom_path(
      data = country$coastline,
      ggplot2::aes(x = lon, y = lat),
      color = "grey35",
      linewidth = 0.25
    )

  if (has_route) {
    n_seg <- max(route$route_path$segment, na.rm = TRUE)
    if (is.null(palette)) palette <- make_segment_palette(n_seg)
    route$route_path$segment_f <- factor(route$route_path$segment, levels = seq_len(n_seg))
    route$station_lines$segment_f <- factor(route$station_lines$segment, levels = seq_len(n_seg))
    table <- segment_summary_table(
      route,
      palette,
      moved_by_segment = moved_by_segment,
      station_col_label = station_col_label
    )

    p <- p +
      ggplot2::geom_path(
        data = route$route_path,
        ggplot2::aes(x = lon, y = lat, color = segment_f, group = segment),
        linewidth = 0.25,
        alpha = 0.40
      ) +
      ggplot2::geom_segment(
        data = route$station_lines,
        ggplot2::aes(x = start_lon, y = start_lat, xend = end_lon, yend = end_lat, color = segment_f),
        linewidth = 0.75,
        alpha = 0.85,
        lineend = "round"
      ) +
      ggplot2::scale_color_manual(values = palette, guide = "none")
  } else {
    table <- station_summary_table(country$stations)
    p <- p +
      ggplot2::geom_segment(
        data = country$stations,
        ggplot2::aes(x = start_lon, y = start_lat, xend = end_lon, yend = end_lat, color = log10_catch),
        linewidth = 0.85,
        alpha = 0.88,
        lineend = "round"
      ) +
      ggplot2::scale_color_viridis_c(option = "turbo", name = expression(log[10]~catch))
  }

  p <- add_ports_and_vessels(p, ports, vessels)
  marker_fill <- c(Ports = "#D62728", Boats = "#FFD24A")
  guide_list <- list(
    shape = ggplot2::guide_legend(
      order = 1,
      override.aes = list(size = 3.2, fill = unname(marker_fill))
    )
  )
  if (!has_route) {
    guide_list$color <- ggplot2::guide_colorbar(
      order = 2,
      title.position = "left",
      title.vjust = 0.5,
      barwidth = grid::unit(52, "pt"),
      barheight = grid::unit(6, "pt")
    )
  }
  if (show_table) {
    p <- p +
      geom_gsp_table(
        data = data.frame(lon = pos$x, lat = pos$y, label = I(list(table$data))),
        ggplot2::aes(x = lon, y = lat, label = label),
        hjust = pos$hjust,
        vjust = pos$vjust,
        table.theme = table_theme(
          table$fills,
          border_matrix = table$borders,
          border_lwd_matrix = table$border_lwd,
          base_size = if (has_route) 8.5 else 11
        )
      )
  }

  p <- p +
    fixed_map_coord(bounds) +
    ggplot2::labs(title = title, subtitle = subtitle, x = NULL, y = NULL) +
    gsp_map_theme(
      legend_position = legend_position,
      legend_justification = legend_justification
    ) +
    do.call(ggplot2::guides, guide_list)

  if (show_degree_axes) {
    p + degree_map_axes()
  } else {
    p + hide_map_axes()
  }
}

plot_survey_waypoints <- function(country, waypoints, ports = NULL,
                                  title = "Coastline, Waypoints, and Ports",
                                  show_degree_axes = TRUE) {
  extra_points <- rbind(
    if (!is.null(waypoints) && nrow(waypoints) > 0L) data.frame(lon = waypoints$lon, lat = waypoints$lat) else NULL,
    if (!is.null(ports) && nrow(ports) > 0L) data.frame(lon = ports$lon, lat = ports$lat) else NULL
  )
  bounds <- plot_bounds(country$coastline, extra_points = extra_points, pad = 0.035)

  p <- ggplot2::ggplot() +
    ggplot2::geom_path(
      data = country$coastline,
      ggplot2::aes(x = lon, y = lat),
      color = "grey30",
      linewidth = 0.28
    )

  if (!is.null(waypoints) && nrow(waypoints) > 0L) {
    p <- p +
      ggplot2::geom_point(
        data = waypoints,
        ggplot2::aes(x = lon, y = lat, color = factor(granularity)),
        size = 0.85,
        alpha = 0.72,
        inherit.aes = FALSE
      ) +
      ggplot2::scale_color_brewer(palette = "Set1", name = "Waypoint granularity")
  }

  if (!is.null(ports) && nrow(ports) > 0L) {
    p <- p +
      ggplot2::geom_point(
        data = ports,
        ggplot2::aes(x = lon, y = lat, shape = "Ports", fill = "Ports"),
        size = 2.5,
        stroke = 0.55,
        color = "grey15",
        inherit.aes = FALSE
      ) +
      ggplot2::scale_shape_manual(name = NULL, values = c(Ports = 22)) +
      ggplot2::scale_fill_manual(name = NULL, values = c(Ports = "#D62728"), guide = "none")
  }

  p <- p +
    fixed_map_coord(bounds) +
    ggplot2::labs(title = title, x = NULL, y = NULL) +
    gsp_map_theme(legend_position = "bottom", legend_justification = "center") +
    ggplot2::guides(
      shape = ggplot2::guide_legend(order = 1, override.aes = list(size = 3.2, fill = "#D62728")),
      color = ggplot2::guide_legend(order = 2, override.aes = list(size = 2.3, alpha = 1))
    )

  if (show_degree_axes) {
    p + degree_map_axes()
  } else {
    p + hide_map_axes()
  }
}
