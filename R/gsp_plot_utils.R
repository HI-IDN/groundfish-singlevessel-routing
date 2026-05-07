#!/usr/bin/env Rscript

load_gsp_plot_packages <- function() {
  for (pkg in c("ggplot2", "grid", "gridExtra", "ggpp", "scales")) {
    if (!requireNamespace(pkg, quietly = TRUE)) {
      stop(sprintf("Missing required R package: %s", pkg), call. = FALSE)
    }
  }
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

fixed_map_coord <- function(bounds) {
  ratio <- 1 / cos(mean(c(bounds$ymin, bounds$ymax)) * pi / 180)
  ggplot2::coord_fixed(
    ratio = ratio,
    xlim = c(bounds$xmin, bounds$xmax),
    ylim = c(bounds$ymin, bounds$ymax),
    expand = FALSE
  )
}

table_position <- function(bounds, corner = "upper_right") {
  x <- switch(corner,
    upper_left = bounds$xmin,
    lower_left = bounds$xmin,
    west = bounds$xmin,
    upper_right = bounds$xmax,
    lower_right = bounds$xmax,
    east = bounds$xmax,
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

table_theme <- function(fill_matrix, base_size = 9) {
  gridExtra::ttheme_default(
    base_size = base_size,
    padding = grid::unit(c(4.2, 5.0), "pt"),
    core = list(
      bg_params = list(fill = fill_matrix, col = NA),
      fg_params = list(col = "grey10")
    ),
    colhead = list(
      bg_params = list(fill = "white", col = NA),
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

segment_summary_table <- function(route, palette) {
  seg_d <- route$distances[!is.na(route$distances$segment), , drop = FALSE]
  total_d <- route$distances[is.na(route$distances$segment), , drop = FALSE]
  counts <- stats::aggregate(station_id ~ segment, route$station_lines, length)
  names(counts)[2] <- "stations"
  tbl <- merge(seg_d, counts, by = "segment", all.x = TRUE)
  tbl$stations[is.na(tbl$stations)] <- 0L

  out <- data.frame(
    `#` = as.character(tbl$segment),
    `|S|` = as.integer(tbl$stations),
    nm = sprintf("%.0f", tbl$transit_nm),
    check.names = FALSE
  )

  if (nrow(seg_d) > 1L && nrow(total_d) > 0L) {
    out <- rbind(out, data.frame(
      `#` = "\u03a3",
      `|S|` = sum(tbl$stations),
      nm = sprintf("%.0f", total_d$transit_nm[1]),
      check.names = FALSE
    ))
  }

  fills <- unname(palette[as.character(tbl$segment)])
  if (nrow(seg_d) > 1L && nrow(total_d) > 0L) fills <- c(fills, "grey92")
  list(data = out, fills = matrix(scales::alpha(fills, 0.30), nrow = nrow(out), ncol = ncol(out)))
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
        drop = FALSE
      )
  }

  p
}

plot_country_or_route <- function(country, route = NULL, ports = NULL, vessels = NULL,
                                  table_corner = "upper_right", palette = NULL,
                                  title = NULL, show_degree_axes = FALSE,
                                  legend_position = NULL,
                                  legend_justification = NULL) {
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
  bounds <- plot_bounds(country$coastline, if (has_route) route$route_path else NULL, extra_points)
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
    table <- segment_summary_table(route, palette)

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
  p <- p +
    ggpp::geom_table(
      data = data.frame(lon = pos$x, lat = pos$y, label = I(list(table$data))),
      ggplot2::aes(x = lon, y = lat, label = label),
      hjust = pos$hjust,
      vjust = pos$vjust,
      table.theme = table_theme(table$fills, base_size = if (has_route) 8.5 else 11)
    ) +
    fixed_map_coord(bounds) +
    ggplot2::labs(title = title, x = NULL, y = NULL) +
    gsp_map_theme(
      legend_position = legend_position,
      legend_justification = legend_justification
    ) +
    ggplot2::guides(
      shape = ggplot2::guide_legend(order = 1, override.aes = list(size = 3.2)),
      fill = ggplot2::guide_legend(order = 1, override.aes = list(size = 3.2)),
      color = ggplot2::guide_colorbar(
        order = 2,
        title.position = "left",
        title.vjust = 0.5,
        barwidth = grid::unit(52, "pt"),
        barheight = grid::unit(6, "pt")
      )
    )

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
      ggplot2::scale_fill_manual(name = NULL, values = c(Ports = "#D62728"))
  }

  p <- p +
    fixed_map_coord(bounds) +
    ggplot2::labs(title = title, x = NULL, y = NULL) +
    gsp_map_theme(legend_position = "bottom", legend_justification = "center") +
    ggplot2::guides(
      shape = ggplot2::guide_legend(order = 1, override.aes = list(size = 3.2)),
      fill = ggplot2::guide_legend(order = 1, override.aes = list(size = 3.2)),
      color = ggplot2::guide_legend(order = 2, override.aes = list(size = 2.3, alpha = 1))
    )

  if (show_degree_axes) {
    p + degree_map_axes()
  } else {
    p + hide_map_axes()
  }
}
