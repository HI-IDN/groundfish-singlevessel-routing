#!/usr/bin/env Rscript
# GSP Solution Path Plotter
# Visualizes the tour from sol/init_nn.json on the survey map

# Silently load required packages, abort if not found
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
cat("=== GSP Solution Path Plotter ===\n\n")

# Parse command-line arguments
args <- commandArgs(trailingOnly = TRUE)
solution_file <- if (length(args) > 0) args[1] else "sol/init_nn.json"

# Check if solution file exists
if (!file.exists(solution_file)) {
  stop(sprintf("Solution file not found: %s", solution_file), call. = FALSE)
}

cat(sprintf("Solution file: %s\n", solution_file))

# Load solution JSON
cat("\nLoading solution from JSON...\n")
solution <- tryCatch({
  jsonlite::fromJSON(solution_file)
}, error = function(e) {
  stop(sprintf("Failed to parse JSON: %s", e$message), call. = FALSE)
})

# Extract tour and metadata
tour <- solution$solution$tour
strategy <- solution$metadata$strategy
boat_id <- solution$metadata$boat_id
total_distance <- solution$solution$total_distance_nm

cat(sprintf("Strategy: %s\n", strategy))
cat(sprintf("Boat ID: %d\n", boat_id))
cat(sprintf("Tour length: %d nodes\n", length(tour)))
cat(sprintf("Total distance: %.2f nm\n\n", total_distance))

# Load coastline data
cat("Loading coastline data...\n")
coastline <- read_db_table("dat/gsp_data.db",
                           "SELECT lat, lon FROM coastline")

# Load port and boat location data
cat("Loading ports and boat locations...\n")
ports_boats <- read_db_table("dat/gsp_data.db",
                       "SELECT p.id, p.name, l.lat, l.lon, 'Port' as type
                       FROM ports p
                       INNER JOIN locations l ON p.location_id = l.id
                       WHERE p.selected = 1
                       UNION ALL
                       SELECT b.id, b.name, l.lat, l.lon, 'Boat' as type
                       FROM boats b
                       INNER JOIN locations l ON b.location_id = l.id
                       WHERE b.id = ?",
                       params = list(boat_id))

# Load the stations data
cat("Loading trawl station locations...\n")
stations <- read_db_table("dat/gsp_data.db",
                          "SELECT s.id, s.amount,
                          start.lat, start.lon
                          FROM stations s
                          INNER JOIN locations start ON s.start_location_id = start.id
                          ORDER BY s.id")

# Build node list in solver order: boat, then stations, then ports
# The solver loads nodes in this specific order for the tour indices
cat("Building node list in solver order...\n")

# Get boat location (index 0)
boat_node <- read_db_table("dat/gsp_data.db",
                           "SELECT l.id, l.lat, l.lon, 0 as type
                            FROM boats b
                            INNER JOIN locations l ON b.location_id = l.id
                            WHERE b.id = ?",
                           params = list(boat_id))

# Get stations (indices 1+)
station_nodes <- read_db_table("dat/gsp_data.db",
                               "SELECT start.id, start.lat, start.lon, 1 as type
                                FROM stations s
                                INNER JOIN locations start ON s.start_location_id = start.id
                                ORDER BY s.id")

# Get ports (indices after stations)
port_nodes <- read_db_table("dat/gsp_data.db",
                            "SELECT l.id, l.lat, l.lon, 2 as type
                             FROM ports p
                             INNER JOIN locations l ON p.location_id = l.id
                             WHERE p.selected = 1
                             ORDER BY p.id")

# Combine in solver order
all_nodes <- bind_rows(boat_node, station_nodes, port_nodes)

# Extract tour node indices
cat(sprintf("\nExtracting tour nodes from solution...\n"))
tour_nodes <- as.integer(tour)

# Build tour path by mapping tour indices to coordinates
tour_path <- tibble()

for (i in seq_along(tour_nodes)) {
  node_idx <- tour_nodes[i] + 1  # Convert 0-based to 1-based for R indexing

  if (node_idx > 0 && node_idx <= nrow(all_nodes)) {
    node_row <- all_nodes[node_idx, ]
    node_type_code <- node_row$type

    # Map type codes to descriptive names
    node_type_name <- case_when(
      node_type_code == 0 ~ "Boat",
      node_type_code == 1 ~ "Station",
      node_type_code == 2 ~ "Port",
      TRUE ~ "Unknown"
    )

    tour_path <- bind_rows(tour_path,
                          tibble(sequence = i,
                                 lat = node_row$lat,
                                 lon = node_row$lon,
                                 node_type = node_type_name,
                                 location_id = node_row$id))
  }
}

cat(sprintf("Built tour path with %d waypoints\n\n", nrow(tour_path)))

check_tour_path_closure <- function(tour_path) {
  # Retrieve first and last points
  first_point <- tour_path %>% slice(1)
  last_point <- tour_path %>% slice(n())

  # Sanity check that first and last points are the same (closed tour)
  if (first_point$lat != last_point$lat || first_point$lon != last_point$lon) {
    warning(sprintf(
      paste0(
        "Tour does not appear to be closed (start and end points differ). ",
        "\nFirst point: seq=%d lat=%.6f lon=%.6f\nLast point: seq=%d lat=%.6f lon=%.6f"
      ),
      first_point$sequence,
      first_point$lat,
      first_point$lon,
      last_point$sequence,
      last_point$lat,
      last_point$lon
    ))
  }
  return(first_point)
}
home_base <- check_tour_path_closure(tour_path)

# Create base plot with coastline
cat("Creating tour visualization...\n")
p <- base_coastline_plot(coastline)

# Add tour path as a connected line
p <- p +
  geom_path(data = tour_path, aes(x = lon, y = lat, color = sequence),
            linewidth = 0.8, alpha = 0.7, inherit.aes = FALSE) +
  scale_color_viridis_c(option = "turbo", direction = 1, name = "Tour Order")

# Add station points (largest)
p <- p +
  geom_point(data = filter(tour_path, node_type == "Station"),
             aes(x = lon, y = lat), size = 2, color = "steelblue", alpha = 0.2, inherit.aes = FALSE)

# Add port points (medium, different color)
p <- p +
  geom_point(data = filter(tour_path, node_type == "Port"),
             aes(x = lon, y = lat), size = 3, color = "orange", alpha = 0.8, inherit.aes = FALSE,
             shape = 17)

# Add boat points (largest, black)
p <- p +
  geom_point(data = filter(tour_path, node_type == "Boat"),
             aes(x = lon, y = lat), shape = 16, size = 4, inherit.aes = FALSE)

# Annotate home base (should be the same as the boat start/end)
p <- p +
  geom_text(data = home_base, aes(x = lon, y = lat), label = "Base",
            hjust = -0.15, vjust = 0.5, size = 3, inherit.aes = FALSE)

# Finalize plot
p <- p +
  coord_fixed_for_lat(tour_path$lat, fallback_lat = 65.0) +
  labs(
    title = sprintf("GSP Solution Tour (%s)", strategy),
    subtitle = sprintf("Total Distance: %.2f nm | %d Stations", total_distance, nrow(tour_path) - 1),
    x = NULL,
    y = NULL
  )

p <- apply_degree_axes(p)
p <- p + gsp_common_theme(legend_position = "right", legend_direction = "vertical")

print(p)

# Save plot
output_file <- gsub("\\.json$", ".png", solution_file)
cat(sprintf("\nSaving plot to %s...\n", output_file))

ggsave(
  filename = output_file,
  plot = p,
  width = 12,
  height = 6,
  dpi = 300,
  bg = "white"
)

cat(sprintf("✓ Plot saved to: %s\n", normalizePath(output_file)))
cat("✓ Visualization complete!\n")
