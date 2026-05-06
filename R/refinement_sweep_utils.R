#!/usr/bin/env Rscript
# Shared utilities for refinement sweep visualization.
# Sourced by refinement_sweep_gif.R and refinement_sweep_leaflet.R.

# ---------------------------------------------------------------------------
# Visual constants
# ---------------------------------------------------------------------------

ALPHA_STABLE             <- 0.12
ALPHA_ACTIVE             <- 0.44
ALPHA_MOVING             <- 0.98
ROUTE_LINEWIDTH_STABLE   <- 0.40
ROUTE_LINEWIDTH_ACTIVE   <- 0.48
STATION_LINEWIDTH_STABLE <- 0.75
STATION_LINEWIDTH_ACTIVE <- 0.95

# ---------------------------------------------------------------------------
# Pass / sweep helpers
# ---------------------------------------------------------------------------

pass_sweep <- function(pass_name) {
  if (pass_name == "init") return(0L)
  as.integer(sub("^pass", "", pass_name))
}

route_header <- function(init_transit, distance_info) {
  improvement     <- init_transit - distance_info$grand_transit
  improvement_pct <- if (init_transit == 0) 0 else improvement / init_transit * 100
  sprintf(
    "Transit %.0f nm | Overall improvement %.0f nm (%.1f%%)",
    distance_info$grand_transit,
    improvement,
    improvement_pct
  )
}

# ---------------------------------------------------------------------------
# Station assignment
# ---------------------------------------------------------------------------

station_assignment <- function(station_segments) {
  out          <- tibble::tibble()
  global_order <- 1L

  for (segment_idx in seq_along(station_segments)) {
    signed_ids <- as.integer(unlist(station_segments[[segment_idx]], use.names = FALSE))
    if (!length(signed_ids)) next

    out <- dplyr::bind_rows(
      out,
      tibble::tibble(
        segment           = segment_idx,
        station_sequence  = seq_along(signed_ids),
        global_sequence   = seq.int(global_order, length.out = length(signed_ids)),
        signed_station_id = signed_ids,
        station_id        = abs(signed_ids)
      )
    )
    global_order <- global_order + length(signed_ids)
  }

  out
}

# ---------------------------------------------------------------------------
# Route / point helpers
# ---------------------------------------------------------------------------

classify_point_factory <- function(solution, boat_location_id) {
  dock_loc_ids     <- unlist(solution$dock_location_ids, use.names = FALSE)
  waypoint_loc_ids <- unlist(solution$unique_waypoint_location_ids, use.names = FALSE)

  function(loc_id) {
    if (loc_id == boat_location_id) return("BOAT")
    if (loc_id %in% dock_loc_ids)     return("PORT")
    if (loc_id %in% waypoint_loc_ids) return("WAYP")
    "Station"
  }
}

build_route_path <- function(solution, locations) {
  route_path <- tibble::tibble()

  for (segment_idx in seq_along(solution$tour_segments_location_ids)) {
    loc_ids <- as.integer(unlist(solution$tour_segments_location_ids[[segment_idx]], use.names = FALSE))
    if (!length(loc_ids)) next

    segment_points <- locations |>
      dplyr::filter(id %in% loc_ids) |>
      dplyr::mutate(.input_order = match(id, loc_ids)) |>
      dplyr::arrange(.input_order) |>
      dplyr::transmute(
        segment     = segment_idx,
        sequence    = dplyr::row_number(),
        location_id = id,
        lat         = lat,
        lon         = lon
      )

    route_path <- dplyr::bind_rows(route_path, segment_points)
  }

  route_path
}

build_regular_route_path <- function(solution, locations, boat_location_id) {
  classify_point        <- classify_point_factory(solution, boat_location_id)
  route_path            <- build_route_path(solution, locations)
  route_path$point_type <- vapply(route_path$location_id, classify_point, character(1))
  route_path
}

route_display_points <- function(route_path) {
  list(
    ports     = route_path |> dplyr::filter(point_type == "PORT") |> dplyr::distinct(lat, lon, .keep_all = TRUE),
    waypoints = route_path |> dplyr::filter(point_type == "WAYP") |> dplyr::distinct(lat, lon, .keep_all = TRUE)
  )
}

# ---------------------------------------------------------------------------
# JSON / data loading
# ---------------------------------------------------------------------------

load_refinement_json <- function(refinement_file) {
  if (!file.exists(refinement_file)) {
    stop(sprintf("Refinement file not found: %s", refinement_file), call. = FALSE)
  }

  doc <- tryCatch(
    jsonlite::fromJSON(refinement_file),
    error = function(e) stop(sprintf("Failed to parse JSON: %s", e$message), call. = FALSE)
  )

  if (is.null(doc$solution$init)) {
    stop("Expected solution$init in the refinement JSON.", call. = FALSE)
  }

  solution_names <- names(doc$solution)
  pass_names     <- solution_names[solution_names == "init" | grepl("^pass[0-9]+$", solution_names)]
  pass_order     <- rep(NA_integer_, length(pass_names))
  pass_order[pass_names == "init"]                          <- 0L
  pass_order[grepl("^pass[0-9]+$", pass_names)]             <- as.integer(sub("^pass", "", pass_names[grepl("^pass[0-9]+$", pass_names)]))
  pass_names <- pass_names[order(pass_order)]

  if (length(pass_names) < 2) {
    stop("Need at least init and one pass in the refinement JSON.", call. = FALSE)
  }

  solutions <- lapply(pass_names, function(name) {
    sol <- doc$solution[[name]]
    sol$tour_segments_location_ids <- ensure_segment_list(sol$tour_segments_location_ids)
    sol$tour_segments_station_ids  <- normalize_station_segments(sol$tour_segments_station_ids)
    sol
  })
  names(solutions) <- pass_names

  distances <- lapply(solutions, extract_solution_distance)
  names(distances) <- pass_names

  list(
    doc             = doc,
    solutions       = solutions,
    distances       = distances,
    pass_names      = pass_names,
    init_transit    = distances[[1]]$grand_transit,
    final_pass_name = pass_names[length(pass_names)]
  )
}

load_map_data <- function(db_path = "dat/gsp.db") {
  cat("Loading coastline, locations, and station endpoints...\n")
  list(
    coastline         = read_db_table(db_path, "SELECT lat, lon FROM coastline"),
    locations         = read_db_table(db_path, "SELECT id, lat, lon FROM locations"),
    station_endpoints = load_station_endpoints(db_path)
  )
}

compute_map_bounds <- function(all_route_paths, pad_frac = 0.04) {
  lon_range <- range(all_route_paths$lon, na.rm = TRUE)
  lat_range <- range(all_route_paths$lat, na.rm = TRUE)
  lon_pad   <- diff(lon_range) * pad_frac
  lat_pad   <- diff(lat_range) * pad_frac
  lon_range <- lon_range + c(-lon_pad, lon_pad)
  lat_range <- lat_range + c(-lat_pad, lat_pad)
  ratio     <- 1.0 / cos(mean(lat_range, na.rm = TRUE) * pi / 180.0)
  list(lon_range = lon_range, lat_range = lat_range, ratio = ratio)
}

make_fixed_map_coord <- function(bounds) {
  ggplot2::coord_fixed(
    ratio  = bounds$ratio,
    xlim   = bounds$lon_range,
    ylim   = bounds$lat_range,
    expand = FALSE
  )
}

# ---------------------------------------------------------------------------
# Misc ggplot helpers
# ---------------------------------------------------------------------------

save_plot <- function(plot, path, width = 9, height = 7) {
  ggplot2::ggsave(
    filename = path, plot = plot,
    width = width, height = height,
    dpi = 160, bg = "white"
  )
}

# ---------------------------------------------------------------------------
# ggplot frame builders  (used by refinement_sweep_gif.R)
# ctx must contain: doc, solutions, distances, pass_names, init_transit,
#                   final_pass_name, coastline, locations, station_endpoints,
#                   fixed_map_coord  (a zero-arg function returning a coord)
# ---------------------------------------------------------------------------

regular_route_plot <- function(solution, distance_info, pass_name, ctx) {
  boat_location_id <- as.integer(ctx$doc$metadata$boat_location_id)
  route_path       <- build_regular_route_path(solution, ctx$locations, boat_location_id)
  station_lines    <- build_station_line_segments(solution$tour_segments_station_ids, ctx$station_endpoints)

  segment_station_ids   <- normalize_station_segments(solution$tour_segments_station_ids)
  segment_station_count <- vapply(segment_station_ids, length, integer(1))
  segment_catch         <- as.numeric(solution$segment_catch_amount)
  if (is.null(segment_catch) || !length(segment_catch)) {
    segment_catch <- rep(NA_real_, length(segment_station_count))
  }
  segment_labels <- sprintf(
    "#%d | %.0f nm | %.0f t",
    seq_along(distance_info$segment_transit),
    distance_info$segment_transit,
    segment_catch / 1e3
  )

  display_points <- route_display_points(route_path)

  p <- base_coastline_plot(ctx$coastline) +
    ggplot2::geom_path(
      data = route_path,
      ggplot2::aes(x = lon, y = lat, color = factor(segment), group = segment),
      linewidth = ROUTE_LINEWIDTH_STABLE, alpha = ALPHA_ACTIVE, inherit.aes = FALSE
    ) +
    ggplot2::geom_segment(
      data = station_lines,
      ggplot2::aes(x = lon, y = lat, xend = lon_end, yend = lat_end, color = factor(segment)),
      linewidth = STATION_LINEWIDTH_ACTIVE, alpha = ALPHA_ACTIVE,
      lineend = "round", inherit.aes = FALSE, show.legend = FALSE
    ) +
    ggplot2::geom_point(
      data = display_points$ports,
      ggplot2::aes(x = lon, y = lat), size = 2.8, shape = 1, inherit.aes = FALSE
    ) +
    ggplot2::geom_point(
      data = display_points$waypoints,
      ggplot2::aes(x = lon, y = lat), shape = 42, size = 1.25, inherit.aes = FALSE
    ) +
    ggplot2::scale_color_viridis_d(option = "turbo", name = "Segment", labels = segment_labels) +
    ctx$fixed_map_coord() +
    ggplot2::labs(
      title    = sprintf("Sweep %d", pass_sweep(pass_name)),
      subtitle = route_header(ctx$init_transit, distance_info),
      caption  = " ", x = NULL, y = NULL
    )

  p <- apply_degree_axes(p)
  p + gsp_common_theme(legend_position = "none") +
    ggplot2::theme(
      plot.title    = ggplot2::element_text(hjust = 0.5, size = 18, face = "bold"),
      plot.subtitle = ggplot2::element_text(hjust = 0.5, size = 13),
      plot.caption  = ggplot2::element_text(hjust = 0.5, size = 10, color = "grey25")
    )
}

transition_plot <- function(prev_name, curr_name, color_stage = c("leaving", "arriving"), ctx) {
  color_stage   <- match.arg(color_stage)
  prev_solution <- ctx$solutions[[prev_name]]
  curr_solution <- ctx$solutions[[curr_name]]
  prev_distance <- ctx$distances[[prev_name]]
  curr_distance <- ctx$distances[[curr_name]]

  boat_location_id <- as.integer(ctx$doc$metadata$boat_location_id)
  prev_path <- build_regular_route_path(prev_solution, ctx$locations, boat_location_id)
  curr_path <- build_regular_route_path(curr_solution, ctx$locations, boat_location_id)

  prev_assign <- station_assignment(prev_solution$tour_segments_station_ids)
  curr_assign <- station_assignment(curr_solution$tour_segments_station_ids)
  compare <- curr_assign |>
    dplyr::select(station_id, curr_segment = segment) |>
    dplyr::left_join(
      prev_assign |> dplyr::select(station_id, prev_segment = segment),
      by = "station_id"
    ) |>
    dplyr::mutate(moved_segment = !is.na(prev_segment) & prev_segment != curr_segment)

  moved_compare          <- compare |> dplyr::filter(moved_segment)
  affected_segments      <- sort(unique(c(moved_compare$prev_segment, moved_compare$curr_segment)))
  moved_count            <- nrow(moved_compare)
  affected_segment_count <- length(affected_segments)

  station_lines <- build_station_line_segments(curr_solution$tour_segments_station_ids, ctx$station_endpoints) |>
    dplyr::left_join(compare, by = "station_id") |>
    dplyr::mutate(
      moved_segment     = dplyr::coalesce(moved_segment, FALSE),
      affected_segment  = segment %in% affected_segments,
      display_segment   = dplyr::case_when(moved_segment ~ prev_segment, TRUE ~ segment),
      station_alpha     = dplyr::case_when(
        moved_segment    ~ ALPHA_MOVING,
        affected_segment ~ ALPHA_ACTIVE,
        TRUE             ~ ALPHA_STABLE
      ),
      station_linewidth = dplyr::case_when(
        affected_segment | moved_segment ~ STATION_LINEWIDTH_ACTIVE,
        TRUE                             ~ STATION_LINEWIDTH_STABLE
      )
    )

  transition_path <- if (color_stage == "leaving") prev_path else curr_path
  display_points  <- route_display_points(transition_path)
  transition_path <- transition_path |>
    dplyr::mutate(route_alpha = ifelse(segment %in% affected_segments, ALPHA_ACTIVE, ALPHA_STABLE))

  pass_improvement <- prev_distance$grand_transit - curr_distance$grand_transit
  moving_label     <- if (color_stage == "leaving") "leaving" else "arriving"
  footer_text      <- sprintf(
    "Stations moving %d (%s) | Segments affected %d | Improvement %.0f nm",
    moved_count, moving_label, affected_segment_count, pass_improvement
  )

  p <- base_coastline_plot(ctx$coastline) +
    ggplot2::geom_path(
      data = transition_path,
      ggplot2::aes(x = lon, y = lat, color = factor(segment), group = segment, alpha = route_alpha),
      linewidth = ROUTE_LINEWIDTH_ACTIVE, inherit.aes = FALSE
    ) +
    ggplot2::geom_segment(
      data = station_lines,
      ggplot2::aes(
        x = lon, y = lat, xend = lon_end, yend = lat_end,
        color = factor(display_segment), alpha = station_alpha, linewidth = station_linewidth
      ),
      lineend = "round", inherit.aes = FALSE
    ) +
    ggplot2::geom_point(
      data = display_points$ports,
      ggplot2::aes(x = lon, y = lat), size = 2.8, shape = 1, inherit.aes = FALSE
    ) +
    ggplot2::geom_point(
      data = display_points$waypoints,
      ggplot2::aes(x = lon, y = lat), shape = 42, size = 1.25, inherit.aes = FALSE
    ) +
    ggplot2::scale_color_viridis_d(option = "turbo", name = "Segment") +
    ggplot2::scale_alpha_identity() +
    ggplot2::scale_linewidth_identity() +
    ctx$fixed_map_coord() +
    ggplot2::labs(
      title    = sprintf("Sweep %d", pass_sweep(curr_name)),
      subtitle = route_header(ctx$init_transit, curr_distance),
      caption  = footer_text, x = NULL, y = NULL
    )

  p <- apply_degree_axes(p)
  p + gsp_common_theme(legend_position = "none") +
    ggplot2::theme(
      plot.title    = ggplot2::element_text(hjust = 0.5, size = 18, face = "bold"),
      plot.subtitle = ggplot2::element_text(hjust = 0.5, size = 12),
      plot.caption  = ggplot2::element_text(hjust = 0.5, size = 10, color = "grey25")
    )
}

init_to_final_plot <- function(ctx) {
  init_name      <- ctx$pass_names[1]
  final_name     <- ctx$final_pass_name
  init_solution  <- ctx$solutions[[init_name]]
  final_solution <- ctx$solutions[[final_name]]
  init_distance  <- ctx$distances[[init_name]]
  final_distance <- ctx$distances[[final_name]]

  boat_location_id <- as.integer(ctx$doc$metadata$boat_location_id)
  init_path        <- build_regular_route_path(init_solution, ctx$locations, boat_location_id)
  final_path       <- build_regular_route_path(final_solution, ctx$locations, boat_location_id)
  display_points   <- route_display_points(final_path)

  init_assign  <- station_assignment(init_solution$tour_segments_station_ids)
  final_assign <- station_assignment(final_solution$tour_segments_station_ids)
  compare <- final_assign |>
    dplyr::select(station_id, final_segment = segment) |>
    dplyr::left_join(
      init_assign |> dplyr::select(station_id, init_segment = segment),
      by = "station_id"
    ) |>
    dplyr::mutate(moved_segment = !is.na(init_segment) & init_segment != final_segment)

  final_station_lines <- build_station_line_segments(final_solution$tour_segments_station_ids, ctx$station_endpoints) |>
    dplyr::left_join(compare, by = "station_id") |>
    dplyr::mutate(
      moved_segment = dplyr::coalesce(moved_segment, FALSE),
      init_segment  = dplyr::coalesce(init_segment, final_segment)
    )
  moved_station_lines <- final_station_lines |> dplyr::filter(moved_segment)

  total_moved           <- nrow(moved_station_lines)
  total_improvement     <- init_distance$grand_transit - final_distance$grand_transit
  total_improvement_pct <- if (init_distance$grand_transit == 0) 0 else total_improvement / init_distance$grand_transit * 100

  subtitle_text <- sprintf(
    "Transit %.0f nm | Overall improvement %.0f nm (%.1f%%)",
    final_distance$grand_transit, total_improvement, total_improvement_pct
  )
  footer_text <- sprintf(
    "Stations moved %d | Total improvement %.0f nm (%.1f%%)",
    total_moved, total_improvement, total_improvement_pct
  )

  p <- base_coastline_plot(ctx$coastline) +
    ggplot2::geom_path(
      data = init_path,
      ggplot2::aes(x = lon, y = lat, group = segment),
      color = "grey45", linewidth = ROUTE_LINEWIDTH_STABLE, alpha = ALPHA_STABLE,
      linetype = "22", inherit.aes = FALSE
    ) +
    ggplot2::geom_path(
      data = final_path,
      ggplot2::aes(x = lon, y = lat, color = factor(segment), group = segment),
      linewidth = ROUTE_LINEWIDTH_ACTIVE, alpha = ALPHA_ACTIVE, inherit.aes = FALSE
    ) +
    ggplot2::geom_segment(
      data = final_station_lines,
      ggplot2::aes(x = lon, y = lat, xend = lon_end, yend = lat_end, color = factor(final_segment)),
      linewidth = STATION_LINEWIDTH_STABLE, alpha = ALPHA_STABLE,
      lineend = "round", inherit.aes = FALSE, show.legend = FALSE
    ) +
    ggplot2::geom_segment(
      data = moved_station_lines,
      ggplot2::aes(x = lon, y = lat, xend = lon_end, yend = lat_end, color = factor(init_segment)),
      linewidth = STATION_LINEWIDTH_ACTIVE, alpha = ALPHA_MOVING,
      lineend = "round", inherit.aes = FALSE, show.legend = FALSE
    ) +
    ggplot2::geom_point(
      data = display_points$ports,
      ggplot2::aes(x = lon, y = lat), size = 2.8, shape = 1, inherit.aes = FALSE
    ) +
    ggplot2::geom_point(
      data = display_points$waypoints,
      ggplot2::aes(x = lon, y = lat), shape = 42, size = 1.25, inherit.aes = FALSE
    ) +
    ggplot2::scale_color_viridis_d(option = "turbo", name = "Segment") +
    ctx$fixed_map_coord() +
    ggplot2::labs(
      title    = "Initial to Final",
      subtitle = subtitle_text,
      caption  = footer_text, x = NULL, y = NULL
    )

  p <- apply_degree_axes(p)
  p + gsp_common_theme(legend_position = "none") +
    ggplot2::theme(
      plot.title    = ggplot2::element_text(hjust = 0.5, size = 18, face = "bold"),
      plot.subtitle = ggplot2::element_text(hjust = 0.5, size = 12),
      plot.caption  = ggplot2::element_text(hjust = 0.5, size = 10, color = "grey25")
    )
}

