#!/usr/bin/env Rscript
# GSP Single-Vessel Route Plotter
# Usage: Rscript plot_single_vessel_route.R <path/to/boat*.json> [path/to/output.png] [final|capacity-feasible|presolve]

required_packages <- c("tidyverse", "DBI", "RSQLite", "jsonlite")

script_file_arg <- grep("^--file=", commandArgs(trailingOnly = FALSE), value = TRUE)
script_dir <- if (length(script_file_arg) > 0) {
  dirname(normalizePath(sub("^--file=", "", script_file_arg[1])))
} else {
  "R"
}
source(file.path(script_dir, "plot_utils.R"))
load_required_packages(required_packages)

cat("=== GSP Single-Vessel Route Plotter ===\n\n")

args <- commandArgs(trailingOnly = TRUE)
if (length(args) == 0) {
  warning("Usage: Rscript plot_single_vessel_route.R <path/to/boat*.json> [path/to/output.png] [final|capacity-feasible|presolve]\n", call. = FALSE)
  args <- c("sol/opt/noport.json", "sol/opt/noport.png", "final")
}

survey_file <- args[1]
output_file <- if (length(args) >= 2 && grepl("\\.png$", args[2], ignore.case = TRUE)) args[2] else gsub("\\.json$", ".png", survey_file)
log_file <- log_path_for_output(output_file)
selected_variant <- if (length(args) >= 3 && grepl("\\.png$", args[2], ignore.case = TRUE)) {
  args[3]
} else if (length(args) >= 2 && !grepl("\\.png$", args[2], ignore.case = TRUE)) {
  args[2]
} else {
  "final"
}

if (!file.exists(survey_file)) {
  stop(sprintf("Survey file not found: %s", survey_file), call. = FALSE)
}

cat(sprintf("Survey file: %s\n", survey_file))
cat("\nLoading survey data from JSON...\n")

survey <- tryCatch({
  jsonlite::fromJSON(survey_file)
}, error = function(e) {
  stop(sprintf("Failed to parse JSON: %s", e$message), call. = FALSE)
})

if (is.null(survey$metadata) || is.null(survey$solution)) {
  warning(sprintf("Skipping %s: not a single-vessel route JSON (no 'metadata'/'solution' keys).", survey_file), call. = FALSE)
  quit(status = 0)
}

resolve_solution_block <- function(survey, variant_name) {
  variant_name <- tolower(variant_name)
  if (variant_name %in% c("final", "solution")) {
    variant_name <- resolve_summary_final_variant(survey)
    if (is.null(variant_name) || is.null(survey$solution[[variant_name]])) {
      stop("Requested variant 'final' is missing from this JSON.", call. = FALSE)
    }
  }
  if (variant_name %in% c("presolve", "pre")) variant_name <- "presolve"
  if (is.null(survey$solution[[variant_name]])) {
    stop(sprintf("Requested variant '%s' is missing from this JSON.", variant_name), call. = FALSE)
  }
  list(
    solution = survey$solution[[variant_name]],
    feasible = survey$solution[[variant_name]]$feasible,
    pass_name = variant_name,
    variant_label = variant_name
  )
}

resolved <- resolve_solution_block(survey, selected_variant)
solution <- resolved$solution
solution$tour_segments_location_ids <- ensure_segment_list(solution$tour_segments_location_ids)
solution$tour_segments_station_ids <- normalize_station_segments(solution$tour_segments_station_ids)
distance_info <- extract_solution_distance(solution)

if (length(solution$tour_segments_location_ids) == 0) {
  stop("tour_segments_location_ids is missing from this JSON.", call. = FALSE)
}

boat_id <- survey$metadata$boat_id
boat_name <- survey$metadata$boat_name
boat_docked_location <- survey$metadata$boat_docked_location
boat_location_id <- as.integer(survey$metadata$boat_location_id)
capacity <- survey$problem$capacity
num_stations <- survey$problem$num_stations
segment_count <- solution$segment_count
transit_distance <- distance_info$grand_transit
total_distance <- distance_info$grand_total
feasible <- resolved$feasible
if (is.null(feasible) || length(feasible) == 0) feasible <- solution$feasible
segment_catch <- solution$segment_catch_amount
segment_transit_distance <- distance_info$segment_transit
segment_distance <- distance_info$segment_total
segment_length <- solution$tour_length
segment_station_ids <- solution$tour_segments_station_ids
variant_label <- resolved$variant_label

cat(sprintf("Variant: %s\n", selected_variant))
if (!is.null(resolved$pass_name)) cat(sprintf("Pass: %s\n", resolved$pass_name))
cat(sprintf("Boat: %s (ID: %d)\n", boat_name, boat_id))
cat(sprintf("Boat docked location: %.6f°, %.6f°\n", boat_docked_location$lat, boat_docked_location$lon))
cat(sprintf("Capacity: %d\n", capacity))
cat(sprintf("Stations: %d\n", num_stations))
cat(sprintf("Segments: %d\n", segment_count))
cat(sprintf("Transit distance: %.0f nm\n", transit_distance))
cat(sprintf("Total distance: %.0f nm\n", total_distance))
cat(sprintf("Feasible: %s\n\n", ifelse(feasible, "YES", "NO")))

segment_station_count <- vapply(segment_station_ids, length, integer(1))
segment_summary <- tibble(
  Segment = seq_along(segment_length),
  Length = as.integer(segment_length),
  Stations = as.integer(segment_station_count),
  Catch = as.integer(segment_catch),
  `Transit (nm)` = as.numeric(segment_transit_distance),
  `Total (nm)` = as.numeric(segment_distance)
)

cat("## Segment Summary\n\n")
cat("| Segment | Length | Stations | Catch | Transit (nm) | Total (nm) |\n")
cat("|---:|---:|---:|---:|---:|---:|\n")
for (i in seq_len(nrow(segment_summary))) {
  cat(sprintf("| %d | %d | %d | %d | %.2f | %.2f |\n",
              segment_summary$Segment[i],
              segment_summary$Length[i],
              segment_summary$Stations[i],
              segment_summary$Catch[i],
              segment_summary$`Transit (nm)`[i],
              segment_summary$`Total (nm)`[i]))
}
cat(sprintf("| **Total** | **%d** | **%d** | **%d** | **%.2f** | **%.2f** |\n\n",
            sum(segment_summary$Length),
            sum(segment_summary$Stations),
            sum(segment_summary$Catch),
            transit_distance,
            total_distance))

segment_log_lines <- c(
  sprintf("Survey file: %s", survey_file),
  sprintf("Variant: %s", variant_label),
  sprintf("Boat: %s (ID: %d)", boat_name, boat_id),
  sprintf("Transit distance: %.2f nm", transit_distance),
  sprintf("Total distance: %.2f nm", total_distance),
  sprintf("Capacity: %d kg", capacity),
  sprintf("Feasible: %s", ifelse(feasible, "true", "false")),
  "",
  "Segment summary",
  "| Segment | Length | Stations | Catch | Transit (nm) | Total (nm) |",
  "|---:|---:|---:|---:|---:|---:|"
)
for (i in seq_len(nrow(segment_summary))) {
  segment_log_lines <- c(
    segment_log_lines,
    sprintf("| %d | %d | %d | %d | %.2f | %.2f |",
            segment_summary$Segment[i],
            segment_summary$Length[i],
            segment_summary$Stations[i],
            segment_summary$Catch[i],
            segment_summary$`Transit (nm)`[i],
            segment_summary$`Total (nm)`[i])
  )
}
segment_log_lines <- c(
  segment_log_lines,
  sprintf("| **Total** | **%d** | **%d** | **%d** | **%.2f** | **%.2f** |",
          sum(segment_summary$Length),
          sum(segment_summary$Stations),
          sum(segment_summary$Catch),
          transit_distance,
          total_distance)
)

if (!feasible) {
  cat("Capacity violations detected:\n")
  for (s in seq_along(segment_catch)) {
    if (segment_catch[s] > capacity) {
      cat(sprintf("  Segment %d: catch=%d exceeds capacity=%d\n", s, segment_catch[s], capacity))
    }
  }
  cat("\n")
}

db_path <- "dat/gsp.db"
if (!file.exists(db_path)) {
  stop(sprintf("Database file not found: %s", db_path), call. = FALSE)
}

cat("Loading coastline data...\n")
coastline <- read_db_table(db_path, "SELECT lat, lon FROM coastline")

cat("Loading location data...\n")
locations <- read_db_table(db_path, "SELECT id, lat, lon FROM locations")

cat("Loading station endpoint data...\n")
station_endpoints <- load_station_endpoints(db_path)

dock_loc_ids <- unlist(solution$dock_location_ids)
waypoint_loc_ids <- unlist(solution$unique_waypoint_location_ids)

classify_point <- function(loc_id) {
  if (loc_id == boat_location_id) return("BOAT")
  if (loc_id %in% dock_loc_ids) return("PORT")
  if (loc_id %in% waypoint_loc_ids) return("WAYP")
  "Station"
}

cat("Building route path from segments...\n")
route_path <- tibble()
segment_id <- 1
for (segment in solution$tour_segments_location_ids) {
  for (loc_id in segment) {
    loc_row <- locations %>% filter(id == loc_id)
    if (nrow(loc_row) > 0) {
      route_path <- bind_rows(
        route_path,
        tibble(
          segment = segment_id,
          sequence = nrow(route_path) + 1,
          lat = loc_row$lat[1],
          lon = loc_row$lon[1],
          location_id = loc_id,
          point_type = classify_point(loc_id)
        )
      )
    }
  }
  segment_id <- segment_id + 1
}

cat(sprintf("Built route path with %d locations across %d segments\n\n", nrow(route_path), segment_count))

station_lines <- build_station_line_segments(solution$tour_segments_station_ids, station_endpoints)
cat(sprintf("Built %d station line segments\n\n", nrow(station_lines)))

p <- base_coastline_plot(coastline)
boat_label <- tibble(
  boat_name = boat_name,
  boat_lon = boat_docked_location$lon,
  boat_lat = boat_docked_location$lat
) %>%
  bind_cols(compute_interior_label_position(
    x = boat_docked_location$lon,
    y = boat_docked_location$lat,
    ref_x = coastline$lon,
    ref_y = coastline$lat
  ))

segment_labels <- sprintf("#%d\n%.0f nm | %.0f nm | %d kg",
                          seq_along(segment_distance),
                          segment_transit_distance,
                          segment_distance,
                          segment_catch)

p <- p +
  geom_path(
    data = route_path,
    aes(x = lon, y = lat, color = factor(segment)),
    linewidth = 0.35,
    alpha = 0.45,
    inherit.aes = FALSE
  ) +
  scale_color_viridis_d(
    option = "turbo",
    name = "Segment stats\n(order | transit | total | catch)",
    labels = segment_labels
  )

port_points <- route_path %>% filter(point_type == "PORT") %>% distinct(lat, lon, .keep_all = TRUE)
waypoints <- route_path %>% filter(point_type == "WAYP") %>% distinct(lat, lon, .keep_all = TRUE)

p <- p +
  geom_segment(
    data = station_lines,
    aes(x = lon, y = lat, xend = lon_end, yend = lat_end, color = factor(segment)),
    linewidth = 1.35,
    alpha = 0.9,
    lineend = "round",
    inherit.aes = FALSE
  ) +
  geom_point(
    data = port_points,
    aes(x = lon, y = lat),
    size = 3,
    shape = 1,
    inherit.aes = FALSE
  ) +
  geom_point(
    data = waypoints,
    aes(x = lon, y = lat),
    shape = 42,
    size = 1.5,
    inherit.aes = FALSE
  ) +
  geom_point(
    data = boat_label,
    aes(x = boat_lon, y = boat_lat),
    shape = 21,
    size = 3.4,
    stroke = 0.7,
    fill = "#0B5FA5",
    color = "black",
    inherit.aes = FALSE
  ) +
  geom_segment(
    data = boat_label,
    aes(x = boat_lon, y = boat_lat, xend = label_lon, yend = label_lat),
    linewidth = 0.45,
    color = "grey35",
    inherit.aes = FALSE,
    show.legend = FALSE
  ) +
  geom_label(
    data = boat_label,
    aes(x = label_lon, y = label_lat, label = boat_name),
    size = 3.1,
    label.size = 0.25,
    fontface = "bold",
    fill = "#0B5FA5",
    color = "white",
    inherit.aes = FALSE,
    show.legend = FALSE
  )

subtitle_text <- sprintf(
  "Transit: %.0f nm | Total: %.0f nm | Stations: %d | Segments: %d | Capacity: %d tons",
  transit_distance,
  total_distance,
  num_stations,
  segment_count,
  capacity / 1000
)

title_text <- describe_single_route_title(survey_file)
variant_text <- title_case_variant(variant_label)
subtitle_parts <- c(boat_name)
solver_status <- survey$solver_stats$gurobi_status
if (is.null(solver_status) || !nzchar(solver_status)) {
  solver_status <- survey$solver_stats$status
}
if (identical(basename(survey_file), "fixedport.json")) {
  mip_label <- if (!is.null(solver_status) && nzchar(solver_status)) solver_status else "Incumbent"
  title_text <- "Capacity MIP Based on Fixed Port Visits from 2023 Survey"
  subtitle_text <- sprintf(
    "%s | Transit: %.0f nm | Total: %.0f nm | Stations: %d | Segments: %d",
    mip_label,
    transit_distance,
    total_distance,
    num_stations,
    segment_count
  )
  subtitle_parts <- c(subtitle_text)
} else if (identical(basename(survey_file), "noport.json")) {
  mip_label <- if (!is.null(solver_status) && nzchar(solver_status)) solver_status else "Incumbent"
  if (!is.null(variant_text) && nzchar(variant_text)) {
    title_text <- sprintf("No-Port MIP Model (%s)", variant_text)
  }
  subtitle_text <- sprintf(
    "%s | Transit: %.0f nm | Total: %.0f nm | Stations: %d | Segments: %d | Capacity: %d tons",
    mip_label,
    transit_distance,
    total_distance,
    num_stations,
    segment_count,
    capacity / 1000
  )
  subtitle_parts <- c(subtitle_text)
} else if (!is.null(variant_text) && !identical(title_text, "Observed Survey Route 2023")) {
  subtitle_parts <- c(variant_text)
  subtitle_parts <- c(subtitle_parts, subtitle_text)
} else {
  subtitle_parts <- c(subtitle_parts, subtitle_text)
}

p <- p +
  coord_fixed_for_lat(route_path$lat, fallback_lat = 65.0) +
  labs(
    title = title_text,
    subtitle = paste(subtitle_parts, collapse = " | "),
    x = NULL,
    y = NULL
  )

p <- apply_degree_axes(p)
p <- p + gsp_common_theme(legend_position = "bottom", legend_direction = "horizontal")
p <- p + guides(color = guide_legend(ncol = 3, byrow = TRUE))

cat(sprintf("\nSaving plot to %s...\n", output_file))

ggsave(
  filename = output_file,
  plot = p,
  width = 8,
  height = 7,
  dpi = 300,
  bg = "white"
)

cat(sprintf("OK Plot saved to: %s\n", normalizePath(output_file)))
write_log_lines(log_file, segment_log_lines)
cat("OK Visualization complete!\n")
