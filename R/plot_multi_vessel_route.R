#!/usr/bin/env Rscript

required_packages <- c("tidyverse", "DBI", "RSQLite", "jsonlite")

args <- commandArgs(trailingOnly = TRUE)
input_glob <- if (length(args) >= 1) args[1] else "sol/survey/boat*.json"
output_file <- if (length(args) >= 2) args[2] else "sol/survey/routes_combined.png"
db_path <- if (length(args) >= 3) args[3] else "dat/gsp.db"

script_file_arg <- grep("^--file=", commandArgs(trailingOnly = FALSE), value = TRUE)
script_dir <- if (length(script_file_arg) > 0) {
  dirname(normalizePath(sub("^--file=", "", script_file_arg[1])))
} else {
  "R"
}
source(file.path(script_dir, "plot_utils.R"))
load_required_packages(required_packages)

cat("=== GSP Multi-Vessel Route Plotter ===\n\n")
cat(sprintf("Input glob: %s\n", input_glob))
cat(sprintf("Output: %s\n", output_file))
cat(sprintf("Database: %s\n\n", db_path))

if (!file.exists(db_path)) {
  stop(sprintf("Database file not found: %s", db_path), call. = FALSE)
}

survey_files <- Sys.glob(input_glob)
if (length(survey_files) == 0) {
  stop(sprintf("No survey files matched: %s", input_glob), call. = FALSE)
}

mix_with_white <- function(color, amount) {
  rgb_vals <- grDevices::col2rgb(color) / 255
  mixed <- rgb_vals * (1 - amount) + amount
  grDevices::rgb(mixed[1], mixed[2], mixed[3])
}

first_word <- function(x) {
  sub("\\s+.*$", "", trimws(x))
}

read_solution_block <- function(path) {
  survey <- jsonlite::fromJSON(path)
  variant_name <- survey$summary$final
  if (is.null(variant_name) || is.null(survey$solution[[variant_name]])) {
    stop(sprintf("Missing final solution block in %s", path), call. = FALSE)
  }
  list(
    file = path,
    boat_id = as.integer(survey$metadata$boat_id),
    boat_name = survey$metadata$boat_name,
    boat_location_id = as.integer(survey$metadata$boat_location_id),
    boat_lat = survey$metadata$boat_docked_location$lat,
    boat_lon = survey$metadata$boat_docked_location$lon,
    capacity = as.numeric(survey$problem$capacity),
    num_nodes = as.integer(survey$problem$num_nodes),
    num_stations = as.integer(survey$problem$num_stations),
    solution = survey$solution[[variant_name]],
    variant_name = variant_name
  )
}

cat("Loading coastline and locations...\n")
coastline <- read_db_table(db_path, "SELECT lat, lon FROM coastline")
locations <- read_db_table(db_path, "SELECT id, lat, lon FROM locations")
cat("Loading station endpoint data...\n")
station_endpoints <- load_station_endpoints(db_path)

boats <- lapply(survey_files, read_solution_block)
boats <- boats[order(vapply(boats, `[[`, integer(1), "boat_id"))]

boat_base_colors <- c("#0B5FA5", "#C84C09", "#2A7F62", "#8B2E5F", "#6A4C93", "#B38B00")
route_path <- tibble()
station_lines <- tibble()
boat_summary <- tibble()
total_distance_nm <- 0
total_stations <- 0
total_segments <- 0
capacities <- numeric()

for (idx in seq_along(boats)) {
  boat <- boats[[idx]]
  solution <- boat$solution
  segments <- ensure_segment_list(solution$tour_segments_location_ids)
  segment_count <- length(segments)
  station_segments <- normalize_station_segments(solution$tour_segments_station_ids)
  station_count <- sum(lengths(station_segments))
  segment_distance <- as.numeric(solution$segment_distance_nm)
  segment_catch <- as.numeric(solution$segment_catch_amount)
  base_color <- boat_base_colors[(idx - 1) %% length(boat_base_colors) + 1]
  lighten_values <- if (segment_count <= 1) {
    0.05
  } else {
    seq(0.0, 0.55, length.out = segment_count)
  }
  segment_colors <- vapply(lighten_values, function(x) mix_with_white(base_color, x), character(1))

  for (seg_idx in seq_along(segments)) {
    segment <- segments[[seg_idx]]
    seg_locs <- locations %>% filter(id %in% segment)
    seg_points <- tibble(location_id = segment) %>%
      left_join(seg_locs, by = c("location_id" = "id")) %>%
      filter(!is.na(lat), !is.na(lon)) %>%
      mutate(
        boat_id = boat$boat_id,
        boat_name = boat$boat_name,
        segment = seg_idx,
        point_order = seq_len(n()),
        segment_key = sprintf("%s #%d", first_word(boat$boat_name), seg_idx),
        segment_label = sprintf("%s #%d: %.0f nm, %.1f t",
                                first_word(boat$boat_name), seg_idx,
                                segment_distance[seg_idx],
                                segment_catch[seg_idx] / 1000),
        segment_color = segment_colors[seg_idx],
        base_color = base_color
      )
    route_path <- bind_rows(route_path, seg_points)
  }

  boat_station_lines <- build_station_line_segments(station_segments, station_endpoints) %>%
    mutate(
      boat_id = boat$boat_id,
      boat_name = boat$boat_name,
      segment_key = sprintf("%s #%d", first_word(boat$boat_name), segment),
      segment_label = sprintf("%s #%d: %.0f nm, %.1f t",
                              first_word(boat$boat_name), segment,
                              segment_distance[segment],
                              segment_catch[segment] / 1000),
      segment_color = segment_colors[segment],
      base_color = base_color
    )
  station_lines <- bind_rows(station_lines, boat_station_lines)

  boat_summary <- bind_rows(
    boat_summary,
    tibble(
      boat_id = boat$boat_id,
      boat_name = boat$boat_name,
      boat_lat = boat$boat_lat,
      boat_lon = boat$boat_lon,
      base_color = base_color,
      segment_count = solution$segment_count,
      num_nodes = boat$num_nodes,
      station_count = station_count,
      capacity = boat$capacity,
      total_catch = sum(segment_catch),
      total_distance_nm = solution$total_distance_nm
    )
  )

  total_distance_nm <- total_distance_nm + as.numeric(solution$total_distance_nm)
  total_stations <- total_stations + station_count
  total_segments <- total_segments + as.integer(solution$segment_count)
  capacities <- c(capacities, boat$capacity)
}

boat_summary <- boat_summary %>%
  mutate(
    label_lon = boat_lon + c(-0.38, 0.38, -0.38, 0.38, -0.38, 0.38)[seq_len(n())],
    label_lat = boat_lat + seq(0.24, -0.24, length.out = n())
  )

capacity_label <- if (length(unique(capacities)) > 1) {
  "mixed"
} else {
  sprintf("%d tons", round(capacities[1] / 1000))
}

if (nrow(route_path) == 0) {
  stop("No route locations were built from the survey JSON files.", call. = FALSE)
}

port_points <- route_path %>%
  group_by(location_id, lat, lon) %>%
  summarize(visits = n(), .groups = "drop") %>%
  filter(visits > 1)

cat(sprintf("Loaded %d boats, %d total route points, and %d station line segments\n\n",
            nrow(boat_summary), nrow(route_path), nrow(station_lines)))

summary_table <- boat_summary %>%
  transmute(
    ID = boat_id,
    Boat = boat_name,
    Segments = as.integer(segment_count),
    Nodes = as.integer(num_nodes),
    Stations = as.integer(station_count),
    `Total Catch` = as.integer(total_catch),
    `Distance (nm)` = sprintf("%.2f", as.numeric(total_distance_nm)),
    Feasible = ifelse(vapply(boats, function(x) isTRUE(x$solution$feasible), logical(1)), "true", "false[^1]")
  )

cat("## Combined Survey Summary\n\n")
cat("|    ID     | Boat              | Segments |   Nodes | Stations | Total Catch | Distance (nm) | Feasible  |\n")
cat("|:---------:|-------------------|---------:|--------:|---------:|------------:|--------------:|-----------|\n")
for (i in seq_len(nrow(summary_table))) {
  cat(sprintf(
    "| %9d | %-17s | %8d | %7d | %8d | %11d | %13s | %-9s |\n",
    summary_table$ID[i],
    summary_table$Boat[i],
    summary_table$Segments[i],
    summary_table$Nodes[i],
    summary_table$Stations[i],
    summary_table$`Total Catch`[i],
    summary_table$`Distance (nm)`[i],
    summary_table$Feasible[i]
  ))
}
cat(sprintf(
  "| **Total** |                   | **%d** | **%d** | **%d** | **%d** | **%.2f** |           |\n\n",
  sum(boat_summary$segment_count),
  sum(boat_summary$num_nodes),
  sum(boat_summary$station_count),
  sum(boat_summary$total_catch),
  sum(as.numeric(boat_summary$total_distance_nm))
))
if (any(summary_table$Feasible == "false[^1]")) {
  cat("[^1]: The exported survey route is retained for plotting even though at least one boat exceeds capacity.\n\n")
}

segment_legend <- route_path %>%
  distinct(segment_key, segment_label, segment_color) %>%
  arrange(segment_key)

legend_breaks <- segment_legend$segment_key
legend_labels <- setNames(segment_legend$segment_label, segment_legend$segment_key)
legend_colors <- setNames(segment_legend$segment_color, segment_legend$segment_key)

p <- base_coastline_plot(coastline) +
  geom_path(
    data = route_path,
    aes(
      x = lon,
      y = lat,
      group = interaction(boat_id, segment),
      color = segment_key
    ),
    linewidth = 0.35,
    alpha = 0.45,
    lineend = "round",
    inherit.aes = FALSE
  ) +
  scale_color_manual(
    values = legend_colors,
    breaks = legend_breaks,
    labels = legend_labels,
    name = "Segment Stats"
  ) +
  geom_segment(
    data = station_lines,
    aes(
      x = lon,
      y = lat,
      xend = lon_end,
      yend = lat_end,
      color = segment_key
    ),
    linewidth = 1.35,
    alpha = 0.9,
    lineend = "round",
    inherit.aes = FALSE
  ) +
  geom_point(
    data = port_points,
    aes(x = lon, y = lat),
    size = 2.4,
    shape = 21,
    stroke = 0.5,
    fill = "white",
    color = "black",
    inherit.aes = FALSE
  ) +
  geom_point(
    data = boat_summary,
    aes(x = boat_lon, y = boat_lat, fill = boat_name),
    shape = 21,
    size = 3.4,
    stroke = 0.7,
    color = "black",
    inherit.aes = FALSE
  ) +
  geom_segment(
    data = boat_summary,
    aes(x = boat_lon, y = boat_lat, xend = label_lon, yend = label_lat),
    linewidth = 0.45,
    color = "grey35",
    inherit.aes = FALSE,
    show.legend = FALSE
  ) +
  geom_label(
    data = boat_summary,
    aes(x = label_lon, y = label_lat, label = boat_name, fill = boat_name),
    size = 3.1,
    label.size = 0.25,
    fontface = "bold",
    inherit.aes = FALSE,
    show.legend = FALSE
  ) +
  scale_fill_manual(
    values = setNames(boat_summary$base_color, boat_summary$boat_name),
    guide = "none"
  ) +
  coord_fixed_for_lat(route_path$lat, fallback_lat = 65.0) +
  labs(
    title = "Combined Survey Routes",
    subtitle = sprintf(
      "Distance %.0f nm | Stations %d | Segments %d | Capacity: %s",
      total_distance_nm,
      total_stations,
      total_segments,
      capacity_label
    ),
    x = NULL,
    y = NULL,
    caption = paste(
      "Each boat keeps one base color; segments fade lighter as the route progresses.",
      sprintf("Files: %s", paste(basename(survey_files), collapse = ", ")),
      sep = "\n"
    )
  )

p <- apply_degree_axes(p)
p <- p + gsp_common_theme(legend_position = "bottom", legend_direction = "horizontal")
p <- p + guides(
  color = guide_legend(ncol = 3, byrow = TRUE, override.aes = list(linewidth = 1.4, alpha = 1))
)

cat(sprintf("Saving plot to %s...\n", output_file))
ggsave(
  filename = output_file,
  plot = p,
  width = 9.5,
  height = 7.5,
  dpi = 300,
  bg = "white"
)

cat(sprintf("OK Plot saved to: %s\n", normalizePath(output_file)))
