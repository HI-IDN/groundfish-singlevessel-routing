#!/usr/bin/env Rscript
# Build pass-by-pass refinement frames and an optional GIF.
# Usage: Rscript R/sandbox_refinement_pass_gif.R sol/noport/refinement_180.json [output_dir] [output.gif]

required_packages <- c("tidyverse", "DBI", "RSQLite", "jsonlite", "grid")

script_file_arg <- grep("^--file=", commandArgs(trailingOnly = FALSE), value = TRUE)
script_dir <- if (length(script_file_arg) > 0) {
  dirname(normalizePath(sub("^--file=", "", script_file_arg[1])))
} else {
  "R"
}
source(file.path(script_dir, "plot_utils.R"))
load_required_packages(required_packages)

cat("=== GSP Refinement Pass GIF Builder ===\n\n")

args <- commandArgs(trailingOnly = TRUE)
refinement_file <- if (length(args) >= 1) args[1] else "sol/noport/refinement_180.json"
frame_dir <- if (length(args) >= 2) args[2] else sub("\\.[Jj][Ss][Oo][Nn]$", "_pass_frames", refinement_file)
gif_file <- if (length(args) >= 3) args[3] else sub("\\.[Jj][Ss][Oo][Nn]$", "_pass_sequence.gif", refinement_file)

if (!file.exists(refinement_file)) {
  stop(sprintf("Refinement file not found: %s", refinement_file), call. = FALSE)
}

doc <- tryCatch({
  jsonlite::fromJSON(refinement_file)
}, error = function(e) {
  stop(sprintf("Failed to parse JSON: %s", e$message), call. = FALSE)
})

if (is.null(doc$solution$init)) {
  stop("Expected solution$init in the refinement JSON.", call. = FALSE)
}

solution_names <- names(doc$solution)
pass_names <- solution_names[solution_names == "init" | grepl("^pass[0-9]+$", solution_names)]
pass_order <- rep(NA_integer_, length(pass_names))
pass_order[pass_names == "init"] <- 0L
pass_order[grepl("^pass[0-9]+$", pass_names)] <- as.integer(sub("^pass", "", pass_names[grepl("^pass[0-9]+$", pass_names)]))
pass_names <- pass_names[order(pass_order)]

if (length(pass_names) < 2) {
  stop("Need at least init and one pass in the refinement JSON.", call. = FALSE)
}

solutions <- lapply(pass_names, function(name) {
  solution <- doc$solution[[name]]
  solution$tour_segments_location_ids <- ensure_segment_list(solution$tour_segments_location_ids)
  solution$tour_segments_station_ids <- normalize_station_segments(solution$tour_segments_station_ids)
  solution
})
names(solutions) <- pass_names

distances <- lapply(solutions, extract_solution_distance)
init_transit <- distances[[1]]$grand_transit
final_pass_name <- pass_names[length(pass_names)]

db_path <- "dat/gsp.db"
cat("Loading coastline, locations, and station endpoints...\n")
coastline <- read_db_table(db_path, "SELECT lat, lon FROM coastline")
locations <- read_db_table(db_path, "SELECT id, lat, lon FROM locations")
station_endpoints <- load_station_endpoints(db_path)

dir.create(frame_dir, recursive = TRUE, showWarnings = FALSE)
unlink(file.path(frame_dir, "*.png"))

build_route_path <- function(solution, locations) {
  route_path <- tibble::tibble()

  for (segment_idx in seq_along(solution$tour_segments_location_ids)) {
    loc_ids <- as.integer(unlist(solution$tour_segments_location_ids[[segment_idx]], use.names = FALSE))
    if (!length(loc_ids)) next

    segment_points <- locations %>%
      dplyr::filter(id %in% loc_ids) %>%
      dplyr::mutate(.input_order = match(id, loc_ids)) %>%
      dplyr::arrange(.input_order) %>%
      dplyr::transmute(
        segment = segment_idx,
        sequence = dplyr::row_number(),
        location_id = id,
        lat = lat,
        lon = lon
      )

    route_path <- dplyr::bind_rows(route_path, segment_points)
  }

  route_path
}

all_route_paths <- dplyr::bind_rows(lapply(solutions, build_route_path, locations = locations))
map_lon_range <- range(all_route_paths$lon, na.rm = TRUE)
map_lat_range <- range(all_route_paths$lat, na.rm = TRUE)
map_lon_pad <- diff(map_lon_range) * 0.04
map_lat_pad <- diff(map_lat_range) * 0.04
map_lon_range <- map_lon_range + c(-map_lon_pad, map_lon_pad)
map_lat_range <- map_lat_range + c(-map_lat_pad, map_lat_pad)
map_ratio <- 1.0 / cos(mean(map_lat_range, na.rm = TRUE) * pi / 180.0)

fixed_map_coord <- function() {
  ggplot2::coord_fixed(
    ratio = map_ratio,
    xlim = map_lon_range,
    ylim = map_lat_range,
    expand = FALSE
  )
}

alpha_stable <- 0.12
alpha_active <- 0.44
alpha_moving <- 0.98

route_linewidth_stable <- 0.40
route_linewidth_active <- 0.48
station_linewidth_stable <- 0.75
station_linewidth_active <- 0.95

pass_sweep <- function(pass_name) {
  if (pass_name == "init") return(0L)
  as.integer(sub("^pass", "", pass_name))
}

route_header <- function(distance_info) {
  overall_improvement <- init_transit - distance_info$grand_transit
  overall_improvement_pct <- if (init_transit == 0) 0 else overall_improvement / init_transit * 100

  sprintf(
    "Transit %.0f nm | Overall improvement %.0f nm (%.1f%%)",
    distance_info$grand_transit,
    overall_improvement,
    overall_improvement_pct
  )
}

station_assignment <- function(station_segments) {
  out <- tibble::tibble()
  global_order <- 1L

  for (segment_idx in seq_along(station_segments)) {
    signed_ids <- as.integer(unlist(station_segments[[segment_idx]], use.names = FALSE))
    if (!length(signed_ids)) next

    out <- dplyr::bind_rows(
      out,
      tibble::tibble(
        segment = segment_idx,
        station_sequence = seq_along(signed_ids),
        global_sequence = seq.int(global_order, length.out = length(signed_ids)),
        signed_station_id = signed_ids,
        station_id = abs(signed_ids)
      )
    )
    global_order <- global_order + length(signed_ids)
  }

  out
}

classify_point_factory <- function(solution, boat_location_id) {
  dock_loc_ids <- unlist(solution$dock_location_ids, use.names = FALSE)
  waypoint_loc_ids <- unlist(solution$unique_waypoint_location_ids, use.names = FALSE)

  function(loc_id) {
    if (loc_id == boat_location_id) return("BOAT")
    if (loc_id %in% dock_loc_ids) return("PORT")
    if (loc_id %in% waypoint_loc_ids) return("WAYP")
    "Station"
  }
}

build_regular_route_path <- function(solution, locations, boat_location_id) {
  classify_point <- classify_point_factory(solution, boat_location_id)
  route_path <- build_route_path(solution, locations)
  route_path$point_type <- vapply(route_path$location_id, classify_point, character(1))
  route_path
}

route_display_points <- function(route_path) {
  list(
    ports = route_path %>%
      dplyr::filter(point_type == "PORT") %>%
      dplyr::distinct(lat, lon, .keep_all = TRUE),
    waypoints = route_path %>%
      dplyr::filter(point_type == "WAYP") %>%
      dplyr::distinct(lat, lon, .keep_all = TRUE)
  )
}

save_plot <- function(plot, path, width = 9, height = 7) {
  ggplot2::ggsave(
    filename = path,
    plot = plot,
    width = width,
    height = height,
    dpi = 160,
    bg = "white"
  )
}

regular_route_plot <- function(solution, distance_info, pass_name, prev_distance_info = NULL) {
  boat_location_id <- as.integer(doc$metadata$boat_location_id)
  route_path <- build_regular_route_path(solution, locations, boat_location_id)
  station_lines <- build_station_line_segments(solution$tour_segments_station_ids, station_endpoints)
  segment_station_ids <- normalize_station_segments(solution$tour_segments_station_ids)
  segment_station_count <- vapply(segment_station_ids, length, integer(1))
  segment_catch <- as.numeric(solution$segment_catch_amount)
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

  title_text <- sprintf("Sweep %d", pass_sweep(pass_name))
  subtitle_text <- route_header(distance_info)

  p <- base_coastline_plot(coastline) +
    ggplot2::geom_path(
      data = route_path,
      ggplot2::aes(x = lon, y = lat, color = factor(segment), group = segment),
      linewidth = route_linewidth_stable,
      alpha = alpha_active,
      inherit.aes = FALSE
    ) +
    ggplot2::geom_segment(
      data = station_lines,
      ggplot2::aes(x = lon, y = lat, xend = lon_end, yend = lat_end, color = factor(segment)),
      linewidth = station_linewidth_active,
      alpha = alpha_active,
      lineend = "round",
      inherit.aes = FALSE,
      show.legend = FALSE
    ) +
    ggplot2::geom_point(
      data = display_points$ports,
      ggplot2::aes(x = lon, y = lat),
      size = 2.8,
      shape = 1,
      inherit.aes = FALSE
    ) +
    ggplot2::geom_point(
      data = display_points$waypoints,
      ggplot2::aes(x = lon, y = lat),
      shape = 42,
      size = 1.25,
      inherit.aes = FALSE
    ) +
    ggplot2::scale_color_viridis_d(
      option = "turbo",
      name = "Segment",
      labels = segment_labels
    ) +
    fixed_map_coord() +
    ggplot2::labs(title = title_text, subtitle = subtitle_text, caption = " ", x = NULL, y = NULL)

  p <- apply_degree_axes(p)
  p + gsp_common_theme(legend_position = "none") +
    ggplot2::theme(
      plot.title = ggplot2::element_text(hjust = 0.5, size = 18, face = "bold"),
      plot.subtitle = ggplot2::element_text(hjust = 0.5, size = 13),
      plot.caption = ggplot2::element_text(hjust = 0.5, size = 10, color = "grey25")
    )
}

transition_plot <- function(prev_name, curr_name, color_stage = c("leaving", "arriving")) {
  color_stage <- match.arg(color_stage)
  prev_solution <- solutions[[prev_name]]
  curr_solution <- solutions[[curr_name]]
  prev_distance <- distances[[prev_name]]
  curr_distance <- distances[[curr_name]]

  boat_location_id <- as.integer(doc$metadata$boat_location_id)
  prev_path <- build_regular_route_path(prev_solution, locations, boat_location_id)
  curr_path <- build_regular_route_path(curr_solution, locations, boat_location_id)

  prev_assign <- station_assignment(prev_solution$tour_segments_station_ids)
  curr_assign <- station_assignment(curr_solution$tour_segments_station_ids)
  compare <- curr_assign %>%
    dplyr::select(station_id, curr_segment = segment) %>%
    dplyr::left_join(
      prev_assign %>% dplyr::select(station_id, prev_segment = segment),
      by = "station_id"
    ) %>%
    dplyr::mutate(moved_segment = !is.na(prev_segment) & prev_segment != curr_segment)

  moved_compare <- compare %>% dplyr::filter(moved_segment)
  affected_segments <- sort(unique(c(moved_compare$prev_segment, moved_compare$curr_segment)))
  moved_count <- nrow(moved_compare)
  affected_segment_count <- length(affected_segments)

  station_lines <- build_station_line_segments(curr_solution$tour_segments_station_ids, station_endpoints) %>%
    dplyr::left_join(compare, by = "station_id") %>%
    dplyr::mutate(
      moved_segment = dplyr::coalesce(moved_segment, FALSE),
      affected_segment = segment %in% affected_segments,
      display_segment = dplyr::case_when(
        moved_segment ~ prev_segment,
        TRUE ~ segment
      ),
      station_alpha = dplyr::case_when(
        moved_segment ~ alpha_moving,
        affected_segment ~ alpha_active,
        TRUE ~ alpha_stable
      ),
      station_linewidth = dplyr::case_when(
        affected_segment | moved_segment ~ station_linewidth_active,
        TRUE ~ station_linewidth_stable
      )
    )

  transition_path <- if (color_stage == "leaving") prev_path else curr_path
  display_points <- route_display_points(transition_path)
  transition_path <- transition_path %>%
    dplyr::mutate(route_alpha = ifelse(segment %in% affected_segments, alpha_active, alpha_stable))

  pass_improvement <- prev_distance$grand_transit - curr_distance$grand_transit
  subtitle_text <- route_header(curr_distance)
  moving_label <- if (color_stage == "leaving") "leaving" else "arriving"
  footer_text <- sprintf(
    "Stations moving %d (%s) | Segments affected %d | Improvement %.0f nm",
    moved_count,
    moving_label,
    affected_segment_count,
    pass_improvement
  )

  p <- base_coastline_plot(coastline) +
    ggplot2::geom_path(
      data = transition_path,
      ggplot2::aes(x = lon, y = lat, color = factor(segment), group = segment, alpha = route_alpha),
      linewidth = route_linewidth_active,
      inherit.aes = FALSE
    ) +
    ggplot2::geom_segment(
      data = station_lines,
      ggplot2::aes(
        x = lon,
        y = lat,
        xend = lon_end,
        yend = lat_end,
        color = factor(display_segment),
        alpha = station_alpha,
        linewidth = station_linewidth
      ),
      lineend = "round",
      inherit.aes = FALSE
    ) +
    ggplot2::geom_point(
      data = display_points$ports,
      ggplot2::aes(x = lon, y = lat),
      size = 2.8,
      shape = 1,
      inherit.aes = FALSE
    ) +
    ggplot2::geom_point(
      data = display_points$waypoints,
      ggplot2::aes(x = lon, y = lat),
      shape = 42,
      size = 1.25,
      inherit.aes = FALSE
    ) +
    ggplot2::scale_color_viridis_d(option = "turbo", name = "Segment") +
    ggplot2::scale_alpha_identity() +
    ggplot2::scale_linewidth_identity() +
    fixed_map_coord() +
    ggplot2::labs(
      title = sprintf("Sweep %d", pass_sweep(curr_name)),
      subtitle = subtitle_text,
      caption = footer_text,
      x = NULL,
      y = NULL
    )

  p <- apply_degree_axes(p)
  p + gsp_common_theme(legend_position = "none") +
    ggplot2::theme(
      plot.title = ggplot2::element_text(hjust = 0.5, size = 18, face = "bold"),
      plot.subtitle = ggplot2::element_text(hjust = 0.5, size = 12),
      plot.caption = ggplot2::element_text(hjust = 0.5, size = 10, color = "grey25")
    )
}

frame_paths <- character()
frame_idx <- 1L

add_frame <- function(plot, label) {
  path <- file.path(frame_dir, sprintf("%02d_%s.png", frame_idx, label))
  save_plot(plot, path)
  frame_paths <<- c(frame_paths, path)
  frame_idx <<- frame_idx + 1L
  cat(sprintf("Saved frame: %s\n", normalizePath(path, winslash = "/", mustWork = FALSE)))
}

add_frame(regular_route_plot(solutions[[1]], distances[[1]], pass_names[1]), pass_names[1])
for (i in seq_len(length(pass_names) - 1L)) {
  prev_name <- pass_names[i]
  curr_name <- pass_names[i + 1L]
  add_frame(transition_plot(prev_name, curr_name, "leaving"), sprintf("%s_to_%s_leaving", prev_name, curr_name))
  add_frame(transition_plot(prev_name, curr_name, "arriving"), sprintf("%s_to_%s_arriving", prev_name, curr_name))
  add_frame(
    regular_route_plot(
      solutions[[curr_name]],
      distances[[curr_name]],
      curr_name,
      distances[[prev_name]]
    ),
    curr_name
  )
}

if (requireNamespace("magick", quietly = TRUE)) {
  cat(sprintf("\nBuilding GIF with 3s frame delay and 10s final frame: %s\n", gif_file))
  images <- magick::image_read(frame_paths)
  frame_delays <- c(rep(300, length(frame_paths) - 1L), 1000)
  animation <- magick::image_animate(images, delay = frame_delays)
  magick::image_write(animation, path = gif_file)
  cat(sprintf("OK GIF saved to: %s\n", normalizePath(gif_file, winslash = "/", mustWork = FALSE)))
} else if (nzchar(Sys.which("py"))) {
  cat(sprintf("\nBuilding GIF with Python/Pillow fallback: %s\n", gif_file))
  python_code <- paste(
    "from PIL import Image",
    "import sys",
    "out = sys.argv[1]",
    "paths = sys.argv[2:]",
    "frames = [Image.open(p).convert('RGB').copy() for p in paths]",
    "durations = [3000] * (len(frames) - 1) + [10000]",
    "[frame.info.update({'duration': duration}) for frame, duration in zip(frames, durations)]",
    "frames[0].save(out, save_all=True, append_images=frames[1:], duration=durations, loop=0, disposal=[2] * len(frames))",
    "[frame.close() for frame in frames]",
    sep = "; "
  )
  gif_status <- system2(
    Sys.which("py"),
    args = c("-c", shQuote(python_code, type = "cmd"), normalizePath(gif_file, winslash = "/", mustWork = FALSE), normalizePath(frame_paths, winslash = "/")),
    stdout = TRUE,
    stderr = TRUE
  )
  if (!is.null(attr(gif_status, "status")) && attr(gif_status, "status") != 0) {
    warning(paste(gif_status, collapse = "\n"), call. = FALSE)
  } else {
    cat(sprintf("OK GIF saved to: %s\n", normalizePath(gif_file, winslash = "/", mustWork = FALSE)))
  }
} else {
  warning("Package 'magick' is not installed and Python/Pillow fallback is unavailable; PNG frames were created, but GIF was skipped.", call. = FALSE)
}

cat(sprintf("\nOK Frames saved in: %s\n", normalizePath(frame_dir, winslash = "/", mustWork = FALSE)))
