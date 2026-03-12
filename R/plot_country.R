#!/usr/bin/env Rscript
# Quick visualization of coastline and generated waypoints from gsp_data.db.
# Waypoint paths are drawn per granularity level:
#   0 = small  (coarse / low-resolution ring)
#   1 = medium (standard ring – default)
#   2 = fine   (high-resolution ring)

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
db_path     <- if (length(args) >= 1) args[1] else "dat/gsp_data.db"
output_file <- if (length(args) >= 2) args[2] else "dat/coastline_waypoints_ports.png"

cat("=== Country Waypoint Plot ===\n")
cat(sprintf("Database: %s\n", db_path))
cat(sprintf("Output:   %s\n\n", output_file))

# ── Data loading ────────────────────────────────────────────────────────────

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

# ── Granularity factor ───────────────────────────────────────────────────────
# Map integer codes to ordered labels so the legend reads small → medium → fine.

granularity_levels  <- c("0", "1", "2")
granularity_labels  <- c("small (0)", "medium (1)", "fine (2)")
granularity_colours <- c(
  "0" = "#E69F00",   # amber   – small
  "1" = "#0072B2",   # blue    – medium
  "2" = "#009E73"    # green   – fine
)

waypoints <- waypoints |>
  mutate(
    gran_fct = factor(
      as.character(granularity),
      levels  = granularity_levels,
      labels  = granularity_labels
    )
  )

# Close each ring independently so every granularity forms a closed loop.
close_ring <- function(df) bind_rows(df, df[1, ])

waypoints_ring <- waypoints |>
  group_by(gran_fct) |>
  group_modify(~ close_ring(.x)) |>
  ungroup()

# Rename colour vector keys to match the labelled factor levels.
names(granularity_colours) <- granularity_labels[
  match(names(granularity_colours), granularity_levels)
]

# ── Build subtitle ───────────────────────────────────────────────────────────

counts_by_gran <- waypoints |>
  count(gran_fct, name = "n_pts") |>
  mutate(label = sprintf("%s: %d pts", gran_fct, n_pts))

subtitle_text <- sprintf(
  "Coastline: %d pts | Ports: %d | Waypoints — %s",
  nrow(coastline),
  nrow(ports),
  paste(counts_by_gran$label, collapse = " | ")
)

# ── Plot ─────────────────────────────────────────────────────────────────────

p <- base_coastline_plot(coastline) +
  # One path per granularity level
  geom_path(
    data        = waypoints_ring,
    aes(x = lon, y = lat, color = gran_fct, group = gran_fct),
    linewidth   = 0.7,
    alpha       = 0.9,
    inherit.aes = FALSE
  ) +
  # Points coloured by granularity
  geom_point(
    data        = waypoints,
    aes(x = lon, y = lat, color = gran_fct),
    size        = 1.6,
    alpha       = 0.9,
    inherit.aes = FALSE
  ) +
  # Ports as hollow circles in a neutral green
  geom_point(
    data        = ports,
    aes(x = lon, y = lat),
    shape       = 21,
    stroke      = 0.35,
    size        = 2.2,
    color       = "#1B9E77",
    fill        = "#E6FFF2",
    alpha       = 0.95,
    inherit.aes = FALSE
  ) +
  scale_color_manual(
    name   = "Waypoint granularity",
    values = granularity_colours,
    drop   = TRUE   # hide unused levels
  ) +
  labs(
    title    = "Coastline, Inferred Waypoints, and Ports",
    subtitle = subtitle_text,
    x        = NULL,
    y        = NULL
  ) +
  coord_fixed_for_lat(coastline$lat, fallback_lat = 65.0)

p <- apply_degree_axes(p)
p <- p + gsp_common_theme(legend_position = "bottom", legend_direction = "horizontal")

print(p)

ggsave(
  filename = output_file,
  plot     = p,
  width    = 10,
  height   = 8,
  dpi      = 300,
  bg       = "white"
)

cat(sprintf("\nSaved plot: %s\n", normalizePath(output_file)))
