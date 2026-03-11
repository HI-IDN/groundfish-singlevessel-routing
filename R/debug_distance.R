#!/usr/bin/env Rscript
# Plot all pairwise distance links among: {all waypoints + selected from/to locations}.
# Style rule:
#   dashed  => crosses_land=1 and waypoint_path is NULL
#   solid   => otherwise

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

# Defaults requested: from=2, to=100
from_id <- 2L
to_id <- 100L
db_path <- "dat/gsp_data.db"

# Optional overrides: <from> <to> [db_path]
if (length(args) >= 2) {
  from_id <- as.integer(args[1])
  to_id <- as.integer(args[2])
  if (length(args) >= 3) db_path <- args[3]
} else if (length(args) == 1) {
  stop("Provide both from and to IDs, or none to use defaults (from=2, to=100).", call. = FALSE)
}

if (is.na(from_id) || is.na(to_id) || from_id <= 0 || to_id <= 0) {
  stop("from_location_id and to_location_id must be positive integers.", call. = FALSE)
}

if (from_id == to_id) {
  stop("from_location_id and to_location_id must be different.", call. = FALSE)
}

cat("=== Distance Pair Debug Plot ===\n")
cat(sprintf("Database: %s\n", db_path))
cat(sprintf("Pair: %d <-> %d\n\n", from_id, to_id))

# Load coastline data
cat("Loading coastline data...\n")
coastline <- read_db_table(db_path, "SELECT lat, lon FROM coastline ORDER BY rowid")

# Load nodes (waypoints + selected from/to locations)
cat("Loading node locations...\n")
nodes <- read_db_table(
  db_path,
  "SELECT l.id, l.lat, l.lon,
          CASE
            WHEN l.id = ? THEN 'from'
            WHEN l.id = ? THEN 'to'
            ELSE 'waypoint'
          END AS role
   FROM locations l
   WHERE l.id IN (?, ?)
      OR l.id IN (SELECT location_id FROM waypoints)
   ORDER BY role DESC, l.id",
  params = list(from_id, to_id, from_id, to_id)
)

if (!all(c(from_id, to_id) %in% nodes$id)) {
  stop("Could not load one or both endpoint locations from database.", call. = FALSE)
}

ids_csv <- paste(nodes$id, collapse = ",")

# Build undirected pair links by taking one row per unordered pair (min id / max id).
cat("Loading distance links...\n")
links <- read_db_table(db_path, sprintf(
  "WITH base AS (
      SELECT
        CASE WHEN d.from_location_id < d.to_location_id THEN d.from_location_id ELSE d.to_location_id END AS a,
        CASE WHEN d.from_location_id < d.to_location_id THEN d.to_location_id ELSE d.from_location_id END AS b,
        CASE WHEN d.waypoint_path is null THEN NULL else d.distance_nm end AS distance_nm,
        d.crosses_land,
        d.waypoint_path,
        ROW_NUMBER() OVER (
          PARTITION BY
            CASE WHEN d.from_location_id < d.to_location_id THEN d.from_location_id ELSE d.to_location_id END,
            CASE WHEN d.from_location_id < d.to_location_id THEN d.to_location_id ELSE d.from_location_id END
          ORDER BY d.id DESC
        ) AS rn
      FROM distances d
      WHERE d.from_location_id IN (%s)
        AND d.to_location_id IN (%s)
        AND d.from_location_id <> d.to_location_id
    )
    SELECT a, b, distance_nm, crosses_land, waypoint_path
    FROM base
    WHERE rn = 1",
  ids_csv, ids_csv
))
if (nrow(links) == 0) {
  stop("No distance rows found among selected nodes.", call. = FALSE)
}

links <- links %>%
  filter(crosses_land == 0)

if (nrow(links) == 0) {
  stop("No valid links found (crosses_land = 0) among selected nodes.", call. = FALSE)
}

links_plot <- links %>%
  left_join(nodes %>% select(id, lon, lat), by = c("a" = "id")) %>%
  rename(lon_a = lon, lat_a = lat) %>%
  left_join(nodes %>% select(id, lon, lat), by = c("b" = "id")) %>%
  rename(lon_b = lon, lat_b = lat)

# Create distance debug plot
cat("Creating distance debug plot...\n")
p <- base_coastline_plot(coastline) +
  geom_segment(
    data = links_plot,
    aes(
      x = lon_a, y = lat_a,
      xend = lon_b, yend = lat_b,
      color = distance_nm
    ),
    linewidth = 0.55,
    alpha = 0.9,
    inherit.aes = FALSE
  ) +
  geom_point(
    data = nodes %>% filter(role == "waypoint"),
    aes(x = lon, y = lat),
    shape = 3,
    size = 1.7,
    color = "black",
    alpha = 0.8,
    inherit.aes = FALSE
  ) +
  geom_point(
    data = nodes %>% filter(role == "from"),
    aes(x = lon, y = lat),
    shape = 16,
    size = 3.2,
    color = "#D55E00",
    inherit.aes = FALSE
  ) +
  geom_point(
    data = nodes %>% filter(role == "to"),
    aes(x = lon, y = lat),
    shape = 16,
    size = 3.2,
    color = "#0072B2",
    inherit.aes = FALSE
  ) +
  # Annotate all nodes with location IDs.
  geom_text(
    data = nodes,
    aes(x = lon, y = lat, label = id),
    nudge_y = 0.02,
    size = 2.3,
    alpha = 0.9,
    inherit.aes = FALSE
  ) +
  scale_color_viridis_c(name = "distance_nm") +
  labs(
    title = sprintf("Distance Debug: %d <-> %d", from_id, to_id),
    subtitle = sprintf(
      "Nodes: %d (waypoints=%d, endpoints=2) | Valid links: %d (crosses_land=0 only)",
      nrow(nodes),
      sum(nodes$role == "waypoint"),
      nrow(links_plot)
    ),
    x = NULL,
    y = NULL
  ) +
  coord_fixed_for_lat(nodes$lat, fallback_lat = 65.0)

p <- apply_degree_axes(p)
p <- p + gsp_common_theme(legend_position = "right", legend_direction = "vertical")

print(p)

# Save plot to dat folder
output_file <- sprintf("dat/debug_distance_%d_%d.png", from_id, to_id)
cat(sprintf("\nSaving plot to %s...\n", output_file))

ggsave(
  filename = output_file,
  plot = p,
  width = 10,
  height = 8,
  dpi = 300,
  bg = "white"
)

cat(sprintf("✓ Plot saved to: %s\n", normalizePath(output_file)))
cat("✓ Distance debug plot complete!\n")
