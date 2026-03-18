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

args <- commandArgs(trailingOnly = TRUE)
db_path <- if (length(args) >= 1) args[1] else "dat/gsp_data.db"
output_file <- if (length(args) >= 2) args[2] else "dat/coastline_waypoints_ports.png"

cat("=== Country Waypoint Plot ===\n")
cat(sprintf("Database: %s\n", db_path))
cat(sprintf("Output:   %s\n\n", output_file))

coastline <- read_db_table(db_path, "SELECT lat, lon FROM coastline ORDER BY id")
if (nrow(coastline) == 0) stop("No coastline rows found in database.", call. = FALSE)

waypoints <- read_db_table(
  db_path,
  "SELECT w.id AS waypoint_order, w.granularity, l.lat, l.lon
   FROM waypoints w
   JOIN locations l ON l.id = w.location_id
   ORDER BY w.granularity, w.id"
)
if (nrow(waypoints) == 0) stop("No waypoint rows found in database.", call. = FALSE)

ports <- read_db_table(
  db_path,
  "SELECT p.id AS port_id, p.name, l.lat, l.lon
   FROM ports p
   JOIN locations l ON l.id = p.location_id
   ORDER BY p.id"
)

granularity_levels <- c("0", "1", "2")
granularity_labels <- c("coarse ring", "buffered support", "manual")
granularity_colours <- c(
  "0" = "#E69F00",
  "1" = "#0072B2",
  "2" = "#009E73"
)

waypoints <- waypoints |>
  mutate(
    waypoint_group = as.character(granularity),
    gran_fct = factor(
      waypoint_group,
      levels = granularity_levels,
      labels = granularity_labels
    )
  )

close_ring <- function(df) bind_rows(df, df[1, ])

waypoints_ring <- waypoints |>
  filter(waypoint_group != "2") |>
  group_by(gran_fct) |>
  group_modify(~ close_ring(.x)) |>
  ungroup()

names(granularity_colours) <- granularity_labels[
  match(names(granularity_colours), granularity_levels)
]

counts_by_group <- waypoints |>
  count(gran_fct, name = "n_pts") |>
  mutate(label = sprintf("%s: %d pts", gran_fct, n_pts))

subtitle_text <- sprintf(
  "Coastline: %d pts | Ports: %d | Waypoints - %s",
  nrow(coastline),
  nrow(ports),
  paste(counts_by_group$label, collapse = " | ")
)

coastline_waypoints <- waypoints |>
  filter(waypoint_group != "2")

manual_waypoint_points <- waypoints |>
  filter(waypoint_group == "2")

p <- base_coastline_plot(coastline) +
  geom_path(
    data = waypoints_ring,
    aes(x = lon, y = lat, color = gran_fct, group = gran_fct),
    linewidth = 0.8,
    alpha = 0.9,
    inherit.aes = FALSE
  ) +
  geom_point(
    data = coastline_waypoints,
    aes(x = lon, y = lat, color = gran_fct),
    size = 1.3,
    alpha = 0.85,
    inherit.aes = FALSE
  ) +
  geom_point(
    data = manual_waypoint_points,
    aes(x = lon, y = lat, color = gran_fct),
    size = 2.0,
    alpha = 1.0,
    inherit.aes = FALSE
  ) +
  geom_point(
    data = ports,
    aes(x = lon, y = lat),
    shape = 21,
    stroke = 0.35,
    size = 2.2,
    color = "#1B9E77",
    fill = "#E6FFF2",
    alpha = 0.95,
    inherit.aes = FALSE
  ) +
  scale_color_manual(
    name = "Waypoint set",
    values = granularity_colours,
    drop = TRUE
  ) +
  labs(
    title = "Coastline, Waypoint Rings, and Ports",
    subtitle = subtitle_text,
    x = NULL,
    y = NULL
  ) +
  coord_fixed_for_lat(coastline$lat, fallback_lat = 65.0)

p <- apply_degree_axes(p)
p <- p + gsp_common_theme(legend_position = "bottom", legend_direction = "horizontal")

ggsave(
  filename = output_file,
  plot = p,
  width = 10,
  height = 8,
  dpi = 300,
  bg = "white"
)

cat(sprintf("\nSaved plot: %s\n", normalizePath(output_file)))
