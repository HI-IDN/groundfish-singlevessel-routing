#!/usr/bin/env Rscript
# Survey Overview Plotter
# Visualizes coastline, ports, and boat locations from GSP database

# Silently load required packages, abort if not found
required_packages <- c("tidyverse", "DBI", "RSQLite")

for (pkg in required_packages) {
  if (!requireNamespace(pkg, quietly = TRUE)) {
    stop(sprintf("Package '%s' is required but not installed.\nInstall with: install.packages('%s')",
                 pkg, pkg), call. = FALSE)
  }
  suppressPackageStartupMessages(library(pkg, character.only = TRUE))
}

# Read gsp_data.db using SQLite
read_db_table <- function(db_path, sql) {
  # Check if database file exists
  if (!file.exists(db_path)) {
    stop(sprintf("Database file not found: %s", db_path), call. = FALSE)
  }

  # Connect to the SQLite database
  con <- dbConnect(RSQLite::SQLite(), dbname = db_path)

  # Ensure connection is closed on exit
  on.exit(dbDisconnect(con), add = TRUE)

  # Read the SQL query result into a data frame
  data <- tryCatch({
    dbGetQuery(con, sql)
  }, error = function(e) {
    stop(sprintf("SQL query failed: %s\nQuery: %s", e$message, sql), call. = FALSE)
  })

  # Print summary information
  cat(sprintf("Loaded %d rows with columns: %s\n",
              nrow(data),
              paste(colnames(data), collapse = ", ")))

  # Process the data
  return(tibble(data))
}

# Main script execution
cat("=== Survey Overview Plotter ===\n\n")


# Load coastline data
cat("Loading coastline data...\n")
coastline <- read_db_table("dat/gsp_data.db",
                           "SELECT lat, lon FROM coastline")

# Plot coastline
cat("\nPlotting coastline...\n")
p <- ggplot(coastline, aes(x = lon, y = lat)) +
  geom_path() +
  coord_fixed()
print(p)

# Load port and boat location data
cat("\nLoading ports and boat locations...\n")
ports <- read_db_table("dat/gsp_data.db",
                       "SELECT p.name, l.lat, l.lon, 'Port' as type
                       FROM ports p
                       INNER JOIN locations l ON p.location_id = l.id
                       UNION ALL
                       SELECT b.name, l.lat, l.lon, 'Boat' as type
                       FROM boats b
                       INNER JOIN locations l ON b.location_id = l.id
                       WHERE b.name = 'Árni Friðriksson'
                       ORDER BY type ASC")

# Load the stations data
cat("\nLoading trawl station locations...\n")
stations <- read_db_table("dat/gsp_data.db",
                          "SELECT s.id, s.amount,
                          start.lat, start.lon, end.lat as lat_end, end.lon as lon_end
                          FROM stations s
                          INNER JOIN locations start ON s.start_location_id = start.id
                          INNER JOIN locations end ON s.end_location_id = end.id")

# Create final plot with coastline, ports, and boats
cat("\nCreating final overview plot...\n")
final_plot <- p +
  geom_point(data = ports, aes(shape = type), size = 2) +
  geom_segment(data = stations, aes(xend = lon_end, yend = lat_end, color=log(amount))
    , linewidth = 0.5, alpha = 0.7) +
  scale_x_continuous(labels = function(x) paste0(x, "°")) +
  scale_y_continuous(labels = function(x) paste0(x, "°")) +
  scale_shape_manual(
    values = c("Boat" = 16, "Port" = 1),  # 16 = filled circle, 1 = empty circle
    name = "Location Type"
  ) +
  scale_color_viridis_c(option = "turbo", direction = 1) +
  labs(
    title = "Iceland Groundfish Survey Overview",
    subtitle = "Coastline, Ports, Boats and Trawl Stations Locations",
    x = NULL,
    y = NULL,
    color = "Catch (log scale)"
  ) +
  theme_minimal() +
  coord_fixed() +
  theme(
    legend.position = "bottom",
    legend.direction = "horizontal",
    legend.box = "horizontal",
    plot.title = element_text(hjust = 0.5, size = 16, face = "bold"),
    plot.subtitle = element_text(hjust = 0.5, size = 12),
    axis.text.x = element_text(size = 10),
    axis.text.y = element_text(size = 10),
    axis.title.x = element_text(size = 12, margin = margin(t = 10)),
    axis.title.y = element_text(size = 12, margin = margin(r = 10)),
    panel.grid.major = element_blank(),
    panel.grid.minor = element_blank(),
    panel.background = element_blank(),
    plot.background = element_rect(fill = "white", color = NA)
  )

print(final_plot)

# Save plot to dat folder
output_file <- "dat/survey_overview.png"
cat(sprintf("\nSaving plot to %s...\n", output_file))

ggsave(
  filename = output_file,
  plot = final_plot,
  width = 10,
  height = 4,
  dpi = 300,
  bg = "white"
)

cat(sprintf("✓ Plot saved to: %s\n", normalizePath(output_file)))
cat("✓ Plot complete!\n")
