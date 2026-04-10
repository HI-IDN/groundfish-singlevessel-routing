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

resolve_solution_block <- function(survey, variant_name) {
  variant_name <- tolower(variant_name)
  if (variant_name %in% c("final", "solution")) {
    variant_name <- survey$summary$final
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
total_distance <- solution$total_distance_nm
feasible <- resolved$feasible
if (is.null(feasible) || length(feasible) == 0) feasible <- solution$feasible
segment_catch <- solution$segment_catch_amount
segment_distance <- solution$segment_distance_nm
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
cat(sprintf("Total distance: %.0f nm\n", total_distance))
cat(sprintf("Feasible: %s\n\n", ifelse(feasible, "YES", "NO")))

segment_station_count <- vapply(segment_station_ids, length, integer(1))
segment_summary <- tibble(
  Segment = seq_along(segment_length),
  Length = as.integer(segment_length),
  Stations = as.integer(segment_station_count),
  Catch = as.integer(segment_catch),
  `Distance (nm)` = as.numeric(segment_distance)
)

cat("## Segment Summary\n\n")
cat("| Segment | Length | Stations | Catch | Distance (nm) |\n")
cat("|---:|---:|---:|---:|---:|\n")
for (i in seq_len(nrow(segment_summary))) {
  cat(sprintf("| %d | %d | %d | %d | %.2f |\n",
              segment_summary$Segment[i],
              segment_summary$Length[i],
              segment_summary$Stations[i],
              segment_summary$Catch[i],
              segment_summary$`Distance (nm)`[i]))
}
cat(sprintf("| **Total** | **%d** | **%d** | **%d** | **%.2f** |\n\n",
            sum(segment_summary$Length),
            sum(segment_summary$Stations),
            sum(segment_summary$Catch),
            total_distance))

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

segment_labels <- sprintf("#%d: %.0f nm, %d kg",
                          seq_along(segment_distance),
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
    name = "Segment Stats",
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
  annotate(
    "point",
    x = boat_docked_location$lon,
    y = boat_docked_location$lat,
    size = 3,
    shape = 16
  ) +
  annotate(
    "text",
    x = boat_docked_location$lon,
    y = boat_docked_location$lat,
    label = "Boat docked",
    hjust = -0.2,
    vjust = 0.5,
    size = 3.5,
    fontface = "bold"
  )

subtitle_text <- sprintf(
  "Distance: %.0f nm | Stations: %d | Segments: %d | Capacity: %d tons",
  total_distance,
  num_stations,
  segment_count,
  capacity / 1000
)

p <- p +
  coord_fixed_for_lat(route_path$lat, fallback_lat = 65.0) +
  labs(
    title = sprintf("Survey Route: %s", boat_name),
    subtitle = sprintf("%s | %s", tools::toTitleCase(gsub("[-_]", " ", variant_label)), subtitle_text),
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
cat("OK Visualization complete!\n")
