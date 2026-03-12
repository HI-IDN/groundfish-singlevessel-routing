#!/usr/bin/env Rscript
# Quick visualization of coastline and generated waypoints from gsp_data.db.

required_packages <- c("tidyverse", "DBI", "RSQLite")

script_file_arg <- grep("^--file=", commandArgs(trailingOnly = FALSE), value = TRUE)
script_dir <- if (length(script_file_arg) > 0) {
  dirname(normalizePath(sub("^--file=", "", script_file_arg[1])))
} else {
  "R"
}
source(file.path(script_dir, "plot_utils.R"))
load_required_packages(required_packages)

args <- commandArgs(trailingOnly = TRUE)
db_path <- if (length(args) >= 1) args[1] else "dat/gsp_data.db"
output_file <- if (length(args) >= 2) args[2] else "dat/country_waypoints.png"

cat("=== Country Waypoint Plot ===\n")
cat(sprintf("Database: %s\n", db_path))
cat(sprintf("Output:   %s\n\n", output_file))

coastline <- read_db_table(db_path, "SELECT lat, lon FROM coastline ORDER BY id")
if (nrow(coastline) == 0) {
  stop("No coastline rows found in database.", call. = FALSE)
}

waypoints <- read_db_table(
  db_path,
  "SELECT w.id AS waypoint_order, l.id AS location_id, l.lat, l.lon
   FROM waypoints w
   JOIN locations l ON l.id = w.location_id
   ORDER BY w.id"
)

if (nrow(waypoints) == 0) {
  stop("No waypoint rows found in database.", call. = FALSE)
}

# Close the waypoint ring for visualization.
waypoints_ring <- bind_rows(waypoints, waypoints[1, ])

p <- base_coastline_plot(coastline) +
  geom_path(
    data = waypoints_ring,
    aes(x = lon, y = lat),
    color = "#D55E00",
    linewidth = 0.6,
    alpha = 0.9,
    inherit.aes = FALSE
  ) +
  geom_point(
    data = waypoints,
    aes(x = lon, y = lat),
    color = "#0072B2",
    size = 1.8,
    alpha = 0.9,
    inherit.aes = FALSE
  ) +
  labs(
    title = "Generated Country Waypoints",
    subtitle = sprintf("Coastline points: %d | Waypoints: %d", nrow(coastline), nrow(waypoints)),
    x = NULL,
    y = NULL
  ) +
  coord_fixed_for_lat(coastline$lat, fallback_lat = 65.0)

p <- apply_degree_axes(p)
p <- p + gsp_common_theme(legend_position = "none")

print(p)

ggsave(
  filename = output_file,
  plot = p,
  width = 10,
  height = 8,
  dpi = 300,
  bg = "white"
)

cat(sprintf("\nSaved plot: %s\n", normalizePath(output_file)))

