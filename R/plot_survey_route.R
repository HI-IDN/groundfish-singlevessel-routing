#!/usr/bin/env Rscript
# GSP Survey Route Plotter
# Visualizes survey routes exported from export_survey_json tool
# Usage: Rscript plot_survey_route.R <path/to/boat*.json> [final|presolve]

# Load required packages
required_packages <- c("tidyverse", "DBI", "RSQLite", "jsonlite")

script_file_arg <- grep("^--file=", commandArgs(trailingOnly = FALSE), value = TRUE)
script_dir <- if (length(script_file_arg) > 0) {
  dirname(normalizePath(sub("^--file=", "", script_file_arg[1])))
} else {
  "R"
}
source(file.path(script_dir, "plot_utils.R"))

load_required_packages(required_packages)

# Main script execution
cat("=== GSP Survey Route Plotter ===\n\n")

# Parse command-line arguments
args <- commandArgs(trailingOnly = TRUE)
if (length(args) == 0) {
  warning("Usage: Rscript plot_survey_route.R <path/to/boat*.json> [final|presolve]\n", call. = FALSE)
  args <- c("sol/opt/noport.json", "final")  # Default for testing
}

survey_file <- args[1]
selected_variant <- if (length(args) >= 2) args[2] else "final"

# Check if survey file exists
if (!file.exists(survey_file)) {
  stop(sprintf("Survey file not found: %s", survey_file), call. = FALSE)
}

cat(sprintf("Survey file: %s\n", survey_file))

# Load survey JSON
cat("\nLoading survey data from JSON...\n")
survey <- tryCatch({
  jsonlite::fromJSON(survey_file)
}, error = function(e) {
  stop(sprintf("Failed to parse JSON: %s", e$message), call. = FALSE)
})

ensure_segment_list <- function(x) {
  if (is.null(x)) {
    return(list())
  }
  if (is.list(x) && !is.data.frame(x)) {
    return(x)
  }
  if (is.data.frame(x)) {
    return(split(x, seq_len(nrow(x))))
  }
  list(x)
}

resolve_solution_block <- function(survey, variant_name) {
  variant_name <- tolower(variant_name)
  if (variant_name %in% c("final", "main", "solution")) {
    return(survey$solution)
  }
  if (variant_name %in% c("presolve", "pre")) {
    if (!is.null(survey$presolve)) {
      return(survey$presolve)
    }
    if (!is.null(survey$solution_variants$before_capacity_fix)) {
      return(survey$solution_variants$before_capacity_fix)
    }
    stop("Requested variant 'presolve' is not available in this JSON.", call. = FALSE)
  }
  stop(sprintf("Unknown solution variant: %s", variant_name), call. = FALSE)
}

solution <- resolve_solution_block(survey, selected_variant)
solution$tour_segments_location_ids <- ensure_segment_list(solution$tour_segments_location_ids)
solution$tour_segments_station_ids <- ensure_segment_list(solution$tour_segments_station_ids)

cat(sprintf("Variant: %s\n", selected_variant))

# Extract metadata
boat_id <- survey$metadata$boat_id
boat_name <- survey$metadata$boat_name
home_port <- survey$metadata$home_port
capacity <- survey$problem$capacity
num_stations <- survey$problem$num_stations
segment_count <- solution$segment_count
total_distance <- solution$total_distance_nm
feasible <- solution$feasible
segment_catch <- solution$segment_catch_amount
segment_distance <- solution$segment_distance_nm
segment_length <- solution$tour_length
segment_station_ids <- solution$tour_segments_station_ids

cat(sprintf("Boat: %s (ID: %d)\n", boat_name, boat_id))
cat(sprintf("Home port: %.6f°, %.6f°\n", home_port$lat, home_port$lon))
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

# Check for capacity violations
if (!feasible) {
  cat("Capacity violations detected:\n")
  for (s in seq_along(segment_catch)) {
    if (segment_catch[s] > capacity) {
      cat(sprintf("  Segment %d: catch=%d exceeds capacity=%d\n", s, segment_catch[s], capacity))
    }
  }
  cat("\n")
}

# Load database for coastline
db_path <- "dat/gsp_data.db"
if (!file.exists(db_path)) {
  stop(sprintf("Database file not found: %s", db_path), call. = FALSE)
}

# Load coastline data
cat("Loading coastline data...\n")
coastline <- read_db_table(db_path, "SELECT lat, lon FROM coastline")

# Load all locations for mapping location IDs to coordinates
cat("Loading location data...\n")
locations <- read_db_table(db_path, "SELECT id, lat, lon FROM locations")

# Build lookup sets from JSON annotations
boat_loc_ids     <- unlist(survey$metadata$boat_location_ids)
dock_loc_ids     <- unlist(solution$dock_location_ids)
waypoint_loc_ids <- unlist(solution$unique_waypoint_location_ids)

classify_point <- function(loc_id) {
  if (loc_id %in% boat_loc_ids)     return("BOAT")
  if (loc_id %in% dock_loc_ids)     return("PORT")
  if (loc_id %in% waypoint_loc_ids) return("WAYP")
  return("Station")
}

# Build route path from tour_segments_location_ids
cat("Building route path from segments...\n")
tour_segments <- solution$tour_segments_location_ids
route_path <- tibble()
segment_id <- 1

for (segment in tour_segments) {
  for (i in seq_along(segment)) {
    loc_id <- segment[i]
    loc_row <- locations %>% filter(id == loc_id)

    if (nrow(loc_row) > 0) {
      route_path <- bind_rows(route_path,
                              tibble(
                                segment    = segment_id,
                                sequence   = nrow(route_path) + 1,
                                lat        = loc_row$lat[1],
                                lon        = loc_row$lon[1],
                                location_id = loc_id,
                                point_type = classify_point(loc_id)
                              ))
    }
  }
  segment_id <- segment_id + 1
}

cat(sprintf("Built route path with %d waypoints across %d segments\n\n",
            nrow(route_path), segment_count))

# Add cumulative metrics
cat("Calculating cumulative distance and catch...\n")
route_path <- route_path %>%
  mutate(
    cumulative_distance_nm = cumsum(ifelse(point_type %in% c("PORT", "BOAT"),
                                           segment_distance[segment],
                                           0)),
    cumulative_catch_kg = cumsum(ifelse(point_type %in% c("PORT", "BOAT"),
                                        segment_catch[segment],
                                        0))
  ) %>%
  # Forward-fill cumulative values within each segment
  group_by(segment) %>%
  fill(cumulative_distance_nm, cumulative_catch_kg, .direction = "down") %>%
  ungroup()

# Create base plot with coastline
cat("Creating survey route visualization...\n")
p <- base_coastline_plot(coastline)

# Create legend labels with segment statistics
segment_labels <- sprintf("#%d: %.0f nm, %d kg",
                          seq_along(segment_distance),
                          segment_distance,
                          segment_catch)

# Add route path with segment coloring
p <- p +
  geom_path(data = route_path,
            aes(x = lon, y = lat, color = factor(segment)),
            linewidth = 0.8, alpha = 0.7, inherit.aes = FALSE) +
  scale_color_viridis_d(option = "turbo",
                        name = "Segment Stats",
                        labels = segment_labels)

# Add station points with cumsum tooltip
station_points <- route_path %>%
  filter(point_type == "Station")

p <- p +
  geom_point(data = station_points,
             aes(x = lon, y = lat,
                 text = sprintf("Loc: %d\nCum Dist: %.1f nm\nCum Catch: %.0f kg",
                                location_id, cumulative_distance_nm, cumulative_catch_kg)),
             size = 1.5, color = "steelblue", alpha = 0.4, inherit.aes = FALSE)

# Add port points with cumsum tooltip
port_points <- route_path %>%
  filter(point_type == "PORT") %>%
  distinct(lat, lon, .keep_all = TRUE)

p <- p +
  geom_point(data = port_points,
             aes(x = lon, y = lat,
                 text = sprintf("PORT\nLoc: %d\nCum Dist: %.1f nm\nCum Catch: %.0f kg",
                                location_id, cumulative_distance_nm, cumulative_catch_kg)),
             size = 3, shape = 1, inherit.aes = FALSE)

# Add home port marker using annotate() to avoid data length warnings
p <- p +
  annotate("point", x = home_port$lon, y = home_port$lat,
           size = 3, shape = 16) +
  annotate("text", x = home_port$lon, y = home_port$lat,
           label = "Home", hjust = -0.2, vjust = 0.5,
           size = 3.5, fontface = "bold")

# Plot waypoints with shape 42
waypoints <- route_path %>%
  filter(point_type == "WAYP") %>%
  distinct(lat, lon, .keep_all = TRUE)

p <- p +
  geom_point(data = waypoints,
             aes(x = lon, y = lat),
             shape=42, size = 1.5, inherit.aes = FALSE)


# Create subtitle with feasibility info
subtitle_text <- sprintf(
  "Distance: %.0f nm | Stations: %d | Segments: %d | Capacity: %d tons",
  total_distance,
  num_stations,
  segment_count,
  capacity / 1000  # Convert kg to tons for subtitle
)

# Finalize plot
p <- p +
  coord_fixed_for_lat(route_path$lat, fallback_lat = 65.0) +
  labs(
    title = sprintf("Survey Route: %s", boat_name),
    subtitle = sprintf("%s | %s", tools::toTitleCase(gsub("_", " ", selected_variant)), subtitle_text),
    x = NULL,
    y = NULL
  )

p <- apply_degree_axes(p)
p <- p + gsp_common_theme(legend_position = "bottom", legend_direction = "horizontal")

# Override legend to use 2 columns
p <- p + guides(color = guide_legend(ncol = 3, byrow = TRUE))

# Save plot
output_file <- gsub("\\.json$", ".png", survey_file)
cat(sprintf("\nSaving plot to %s...\n", output_file))

ggsave(
  filename = output_file,
  plot = p,
  width = 8,
  height = 7,
  dpi = 300,
  bg = "white"
)

cat(sprintf("✓ Plot saved to: %s\n", normalizePath(output_file)))
cat("✓ Visualization complete!\n")

# Interactive usage: In R console, run:
# plotly::ggplotly(p, tooltip = "text")
# This will show cumulative distance and catch on hover







