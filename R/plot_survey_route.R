#!/usr/bin/env Rscript
# GSP Survey Route Plotter
# Visualizes survey routes exported from export_survey_json tool
# Usage: Rscript plot_survey_route.R <path/to/boat*.json>

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
  warning("Usage: Rscript plot_survey_route.R <path/to/boat*.json>\n", call. = FALSE)
  args <- c("sol/survey/boat1.json")  # Default for testing
}

survey_file <- args[1]

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

# Extract metadata
boat_id <- survey$metadata$boat_id
boat_name <- survey$metadata$boat_name
home_port <- survey$metadata$home_port
capacity <- survey$problem$capacity
num_stations <- survey$problem$num_stations
segment_count <- survey$solution$segment_count
total_distance <- survey$solution$total_distance_nm
feasible <- survey$solution$feasible
segment_catch <- survey$solution$segment_catch_amount
segment_distance <- survey$solution$segment_distance_nm

cat(sprintf("Boat: %s (ID: %d)\n", boat_name, boat_id))
cat(sprintf("Home port: %.6f°, %.6f°\n", home_port$lat, home_port$lon))
cat(sprintf("Capacity: %d\n", capacity))
cat(sprintf("Stations: %d\n", num_stations))
cat(sprintf("Segments: %d\n", segment_count))
cat(sprintf("Total distance: %.0f nm\n", total_distance))
cat(sprintf("Feasible: %s\n\n", ifelse(feasible, "YES", "NO")))

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

# Build route path from tour_segments_location_ids
cat("Building route path from segments...\n")
tour_segments <- survey$solution$tour_segments_location_ids
route_path <- tibble()
segment_id <- 1

for (segment in tour_segments) {
  # Each segment is a list of location IDs
  for (i in seq_along(segment)) {
    loc_id <- segment[i]

    # Find coordinates for this location
    loc_row <- locations %>% filter(id == loc_id)

    if (nrow(loc_row) > 0) {
      # Determine point type: first/last are ports, middle are stations
      if (i == 1) {
        point_type <- "Port (Start)"
      } else if (i == length(segment)) {
        point_type <- "Port (End)"
      } else if (i %% 2 == 0) {
        point_type <- "Station (Start)"
      } else {
        point_type <- "Station (End)"
      }

      route_path <- bind_rows(route_path,
                              tibble(
                                segment = segment_id,
                                sequence = nrow(route_path) + 1,
                                lat = loc_row$lat[1],
                                lon = loc_row$lon[1],
                                location_id = loc_id,
                                point_type = point_type
                              ))
    }
  }
  segment_id <- segment_id + 1
}

cat(sprintf("Built route path with %d waypoints across %d segments\n\n",
            nrow(route_path), segment_count))

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

# Add station points
station_points <- route_path %>%
  filter(grepl("Station", point_type))

p <- p +
  geom_point(data = station_points,
             aes(x = lon, y = lat),
             size = 1.5, color = "steelblue", alpha = 0.4, inherit.aes = FALSE)

# Add port points
port_points <- route_path %>%
  filter(grepl("Port", point_type)) %>%
  distinct(lat, lon, .keep_all = TRUE)

p <- p +
  geom_point(data = port_points,
             aes(x = lon, y = lat),
             size = 3, shape = 1, inherit.aes = FALSE)

# Add home port marker using annotate() to avoid data length warnings
p <- p +
  annotate("point", x = home_port$lon, y = home_port$lat,
           size = 3, shape = 16) +
  annotate("text", x = home_port$lon, y = home_port$lat,
           label = "Home", hjust = -0.2, vjust = 0.5,
           size = 3.5, fontface = "bold")

# Create subtitle with feasibility info
subtitle_text <- sprintf(
  "Distance: %.0f nm | Stations: %d | Segments: %d | Capacity: %d tons",
  total_distance,
  num_stations,
  segment_count,
  capacity/1000  # Convert kg to tons for subtitle
)

# Finalize plot
p <- p +
  coord_fixed_for_lat(route_path$lat, fallback_lat = 65.0) +
  labs(
    title = sprintf("Survey Route: %s", boat_name),
    subtitle = subtitle_text,
    x = NULL,
    y = NULL
  )

p <- apply_degree_axes(p)
p <- p + gsp_common_theme(legend_position = "bottom", legend_direction = "horizontal")

# Override legend to use 2 columns
p <- p + guides(color = guide_legend(ncol = 2, byrow = TRUE))

print(p)

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






