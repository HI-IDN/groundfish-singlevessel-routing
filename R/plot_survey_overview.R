#!/usr/bin/env Rscript

required_packages <- c("tidyverse", "DBI", "RSQLite")

script_file_arg <- grep("^--file=", commandArgs(trailingOnly = FALSE), value = TRUE)
script_dir <- if (length(script_file_arg) > 0) {
  dirname(normalizePath(sub("^--file=", "", script_file_arg[1])))
} else {
  "R"
}
source(file.path(script_dir, "plot_utils.R"))

load_required_packages(required_packages)

cat("=== Survey Overview Plotter ===\n\n")

cat("Loading coastline data...\n")
coastline <- read_db_table("dat/gsp_data.db", "SELECT lat, lon FROM coastline")

cat("\nPlotting coastline...\n")
p <- base_coastline_plot(coastline)

cat("\nLoading locations...\n")
locations <- read_db_table(
  "dat/gsp_data.db",
  "SELECT p.name, l.lat, l.lon, 'Port' as type
   FROM ports p
   INNER JOIN locations l ON p.location_id = l.id
   UNION ALL
   SELECT b.name, l.lat, l.lon, 'Boat' as type
   FROM boats b
   INNER JOIN locations l ON b.start_location_id = l.id
   ORDER BY type DESC"
)

cat("\nLoading trawl station locations...\n")
stations <- read_db_table(
  "dat/gsp_data.db",
  "SELECT s.id, s.amount,
          start.lat as start_lat, start.lon as start_lon,
          end.lat as end_lat, end.lon as end_lon
   FROM stations s
   INNER JOIN locations start ON s.start_location_id = start.id
   INNER JOIN locations end ON s.end_location_id = end.id
   ORDER BY s.id"
)

cat("\nCreating final overview plot...\n")
final_plot <- p +
  geom_segment(
    data = stations,
    aes(
      x = start_lon,
      y = start_lat,
      xend = end_lon,
      yend = end_lat,
      color = log10(amount + 1)
    ),
    arrow = grid::arrow(length = grid::unit(0.05, "cm")),
    lineend = "round",
    linewidth = 0.6,
    inherit.aes = FALSE
  ) +
  scale_colour_gradientn(
    colours = rev(rainbow(7)),
    name = "Catch amount\n(log10 scale)",
    na.value = "grey50",
    guide = "colourbar"
  ) +
  geom_point(
    data = locations,
    aes(x = lon, y = lat, shape = type),
    size = 3,
    inherit.aes = FALSE
  ) +
  scale_shape_manual(
    values = c("Port" = 1, "Boat" = 16),
    name = "Locations"
  ) +
  labs(
    title = "Iceland Groundfish Survey Overview",
    subtitle = sprintf(
      "Coastline, Ports (%d), Boats (%d), and Trawl Stations (%d)",
      nrow(filter(locations, type == "Port")),
      nrow(filter(locations, type == "Boat")),
      nrow(stations)
    ),
    x = NULL,
    y = NULL
  ) +
  coord_fixed_for_lat(c(stations$start_lat, stations$end_lat), fallback_lat = 65.0)

final_plot <- apply_degree_axes(final_plot)
final_plot <- final_plot + gsp_common_theme(
  legend_position = "bottom",
  legend_direction = "horizontal"
)

print(final_plot)

output_file <- "dat/survey_overview.png"
cat(sprintf("\nSaving plot to %s...\n", output_file))

ggsave(
  filename = output_file,
  plot = final_plot,
  width = 8,
  height = 6,
  dpi = 300,
  bg = "white"
)

cat(sprintf("OK Plot saved to: %s\n", normalizePath(output_file)))
cat("OK Survey overview plot complete!\n")
