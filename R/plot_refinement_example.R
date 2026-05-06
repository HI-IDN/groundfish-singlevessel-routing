#!/usr/bin/env Rscript
# plot_refinement_example.R
#
# Plots the initial and final-pass routes for a single refinement JSON as
# camera-ready country maps with right-edge segment summary tables.
#
# Usage:
#   Rscript R/plot_refinement_example.R [path/to/refinement_NNN.json]
#   Default input: sol/noport/refinement_180.json
#
# Outputs (written next to the input JSON):
#   refinement_compare.png -- cowplot A + B with panel labels and caption

required_packages <- c("tidyverse", "DBI", "RSQLite", "jsonlite", "cowplot", "ggpp", "gridExtra")

script_file_arg <- grep("^--file=", commandArgs(trailingOnly = FALSE), value = TRUE)
script_dir <- if (length(script_file_arg) > 0) {
  dirname(normalizePath(sub("^--file=", "", script_file_arg[1])))
} else {
  "R"
}
source(file.path(script_dir, "plot_utils.R"))
source(file.path(script_dir, "refinement_sweep_utils.R"))
load_required_packages(required_packages)

# ---------------------------------------------------------------------------
# Args
# ---------------------------------------------------------------------------

args            <- commandArgs(trailingOnly = TRUE)
refinement_file <- if (length(args) >= 1) args[1] else "sol/noport/refinement_180.json"

if (!file.exists(refinement_file)) {
  stop(sprintf("Refinement JSON not found: %s", refinement_file), call. = FALSE)
}
cat(sprintf("Loading: %s\n", refinement_file))

# ---------------------------------------------------------------------------
# Load data
# ---------------------------------------------------------------------------

rf       <- load_refinement_json(refinement_file)
map_data <- load_map_data()

boat_location_id <- as.integer(rf$doc$metadata$boat_location_id)
boat_label <- tryCatch({
  nm <- rf$doc$metadata$boat_name
  if (is.null(nm) || !nzchar(nm)) "Vessel" else sprintf("Vessel: %s", nm)
}, error = function(cond) "Vessel")

# init  = solution.init  (== segment.json baseline before any sweep)
# final = last pass in the refinement
init_solution  <- rf$solutions[["init"]]
final_solution <- rf$solutions[[rf$final_pass_name]]
init_dist      <- rf$distances[["init"]]
final_dist     <- rf$distances[[rf$final_pass_name]]

n_sweeps <- length(rf$pass_names) - 1L   # pass_names includes "init"
n_palette_segments <- max(
  length(init_solution$tour_segments_location_ids),
  length(final_solution$tour_segments_location_ids)
)
segment_palette <- stats::setNames(
  scales::viridis_pal(option = "turbo")(n_palette_segments),
  as.character(seq_len(n_palette_segments))
)

# L₂seg label
l2seg_raw <- tryCatch(as.integer(rf$doc$metadata$mip_time_limit_seconds), error = function(cond) NA_integer_)
l2seg_expr <- if (!is.na(l2seg_raw) && l2seg_raw > 0L) {
  bquote(L[2*seg] == .(l2seg_raw)~s)
} else {
  quote(L[2*seg] == infinity)
}

# Method label from directory name
method_dir   <- basename(dirname(refinement_file))
method_label <- switch(method_dir,
  noport   = "MH-OPT",
  nn       = "MH-NN",
  ci       = "MH-CI",
  ge       = "MH-GE",
  method_dir
)

# ---------------------------------------------------------------------------
# Route build helper
# ---------------------------------------------------------------------------

build_paths_for <- function(solution) {
  build_regular_route_path(solution, map_data$locations, boat_location_id)
}

# ---------------------------------------------------------------------------
# Compute fixed map bounds (union of init + final paths)
# ---------------------------------------------------------------------------

all_pts <- dplyr::bind_rows(
  build_route_path(init_solution,  map_data$locations),
  build_route_path(final_solution, map_data$locations)
)
bounds <- compute_map_bounds(all_pts, pad_frac = 0.03)

# ---------------------------------------------------------------------------
# Right-edge segment summary tables.
# ---------------------------------------------------------------------------

segment_table_inset <- function(dist_info, solution) {
  segment_catch <- as.numeric(solution$segment_catch_amount)
  n_segs        <- length(dist_info$segment_transit)
  has_catch     <- !is.null(segment_catch) && length(segment_catch) >= n_segs

  tbl <- tibble::tibble(
    `#` = as.character(seq_len(n_segs)),
    nm  = sprintf("%.0f", dist_info$segment_transit),
    t   = if (has_catch) sprintf("%.0f", segment_catch[seq_len(n_segs)] / 1e3) else "-"
  )

  total <- tibble::tibble(
    `#` = "Total",
    nm  = sprintf("%.0f", dist_info$grand_transit),
    t   = if (has_catch) sprintf("%.0f", sum(segment_catch[seq_len(n_segs)]) / 1e3) else "-"
  )

  tbl <- dplyr::bind_rows(tbl, total)
  fill_rows <- c(scales::alpha(unname(segment_palette[seq_len(n_segs)]), 0.30), "grey92")

  list(
    data = tibble::tibble(
      lon = Inf,
      lat = Inf,
      label = list(tbl)
    ),
    fill_matrix = matrix(fill_rows, nrow = length(fill_rows), ncol = ncol(tbl))
  )
}

# ---------------------------------------------------------------------------
# Core map builder - returns a ggplot without title/caption
# ---------------------------------------------------------------------------

build_route_map <- function(
  solution,
  dist_info,
  include_title = FALSE,
  title_text = NULL
) {
  route_path    <- build_paths_for(solution)
  station_lines <- build_station_line_segments(
    solution$tour_segments_station_ids, map_data$station_endpoints
  )
  display_pts  <- route_display_points(route_path)
  boat_point   <- map_data$locations |>
    dplyr::filter(id == boat_location_id) |>
    dplyr::mutate(label = boat_label)
  table_inset  <- segment_table_inset(dist_info, solution)

  color_scale <- ggplot2::scale_color_manual(
    values = segment_palette,
    guide  = "none"
  )

  p <- base_coastline_plot(map_data$coastline) +
    # Transit route (thin, semi-transparent)
    ggplot2::geom_path(
      data      = route_path,
      ggplot2::aes(x = lon, y = lat, color = factor(segment), group = segment),
      linewidth = 0.45,
      alpha     = 0.55,
      inherit.aes = FALSE
    ) +
    # Station tow lines (thicker)
    ggplot2::geom_segment(
      data = station_lines,
      ggplot2::aes(
        x = lon, y = lat, xend = lon_end, yend = lat_end,
        color = factor(segment)
      ),
      linewidth   = 1.25,
      alpha       = 0.85,
      lineend     = "round",
      inherit.aes = FALSE,
      show.legend = FALSE
    ) +
    # Port markers
    ggplot2::geom_point(
      data      = display_pts$ports,
      ggplot2::aes(x = lon, y = lat),
      shape     = 21,
      size      = 3.0,
      stroke    = 0.5,
      fill      = "white",
      color     = "grey20",
      inherit.aes = FALSE
    ) +
    # Vessel dock marker and label
    ggplot2::geom_point(
      data        = boat_point,
      ggplot2::aes(x = lon, y = lat),
      shape       = 23,
      size        = 3.6,
      stroke      = 0.7,
      fill        = "#FFD24A",
      color       = "grey15",
      inherit.aes = FALSE
    ) +
    ggplot2::geom_text(
      data = boat_point,
      ggplot2::aes(x = lon, y = lat, label = label),
      nudge_x = 0.28,
      size = 2.6,
      hjust = 0, vjust = 0.5,
      inherit.aes = FALSE
    ) +
    # Waypoint ticks
    ggplot2::geom_point(
      data      = display_pts$waypoints,
      ggplot2::aes(x = lon, y = lat),
      shape     = 3,
      size      = 0.8,
      color     = "grey50",
      alpha     = 0.5,
      inherit.aes = FALSE
    ) +
    ggpp::geom_table(
      data = table_inset$data,
      mapping = ggplot2::aes(x = lon, y = lat, label = label),
      hjust = 1.06,
      vjust = 0.85,
      table.theme = gridExtra::ttheme_default(
        base_size = 6,
        padding = grid::unit(c(2.1, 2.4), "pt"),
        core = list(
          bg_params = list(fill = table_inset$fill_matrix),
          fg_params = list(fontface = "plain", col = "grey10")
        ),
        colhead = list(
          bg_params = list(fill = "white"),
          fg_params = list(fontface = "bold", col = "grey10")
        )
      )
    ) +
    color_scale +
    make_fixed_map_coord(bounds)

  p <- apply_degree_axes(p)

  if (include_title && !is.null(title_text)) {
    p <- p + ggplot2::labs(title = title_text, x = NULL, y = NULL)
  } else {
    p <- p + ggplot2::labs(x = NULL, y = NULL)
  }

  p + gsp_common_theme(legend_position = "none") +
    ggplot2::theme(
      plot.margin = ggplot2::margin(2, 2, 2, 2, "pt"),
      plot.title  = ggplot2::element_text(hjust = 0.5, size = 11, face = "bold")
    )
}

# ---------------------------------------------------------------------------
# Headline stats helpers
# ---------------------------------------------------------------------------

transit_delta_str <- function(init_d, final_d) {
  delta_nm  <- init_d$grand_transit - final_d$grand_transit
  delta_pct <- if (init_d$grand_transit == 0) 0 else delta_nm / init_d$grand_transit * 100
  sprintf("\u2212%.0f\u202fnm (\u2212%.1f%%)", delta_nm, delta_pct)
}

# ---------------------------------------------------------------------------
# Build the panels
# ---------------------------------------------------------------------------

cat("Building init route map...\n")
p_init <- build_route_map(
  init_solution, init_dist,
  include_title = FALSE
)

cat("Building final route map...\n")
p_final <- build_route_map(
  final_solution, final_dist,
  include_title = FALSE
)

# ---------------------------------------------------------------------------
# Output paths
# ---------------------------------------------------------------------------

out_dir   <- dirname(normalizePath(refinement_file, mustWork = TRUE))
out_cmp   <- file.path(out_dir, "refinement_compare.png")

# ---------------------------------------------------------------------------
# Save camera-ready singles (no title, no caption)
# ---------------------------------------------------------------------------

# (singles not written - only the comparison figure is produced)

# ---------------------------------------------------------------------------
# Compare plot: cowplot A + B with panel labels and captions
# ---------------------------------------------------------------------------

# Rebuild with panel title for the cowplot version
p_init_titled <- p_init +
  ggplot2::labs(
    title    = sprintf("A  \u2014  Before refinement (%s)", method_label),
    subtitle = sprintf(
      "Transit\u202f%.0f\u202fnm | %d segments",
      init_dist$grand_transit,
      length(init_solution$tour_segments_location_ids)
    )
  ) +
  ggplot2::theme(
    plot.title    = ggplot2::element_text(hjust = 0, size = 10, face = "bold"),
    plot.subtitle = ggplot2::element_text(hjust = 0, size = 8.5, color = "grey30"),
    plot.margin   = ggplot2::margin(1, 1, 1, 1, "pt")
  )

p_final_titled <- p_final +
  ggplot2::labs(
    title    = sprintf(
      "B  \u2014  After %d sweep%s (%s)",
      n_sweeps, if (n_sweeps == 1L) "" else "s",
      method_label
    ),
    subtitle = bquote(
      "Transit " * .(sprintf("%.0f nm", final_dist$grand_transit)) *
        " | Improvement " * .(transit_delta_str(init_dist, final_dist)) *
        " | " * .(l2seg_expr)
    )
  ) +
  ggplot2::theme(
    plot.title    = ggplot2::element_text(hjust = 0, size = 10, face = "bold"),
    plot.subtitle = ggplot2::element_text(hjust = 0, size = 8.5, color = "grey30"),
    plot.margin   = ggplot2::margin(1, 1, 1, 1, "pt")
  )

footnote_text <- paste0(
  "Right-edge tables show segment index, transit distance (nm), and catch (t). ",
  "Station tow lines are drawn thicker; thin lines show transit routing between ports and waypoints."
)

footnote_grob <- cowplot::ggdraw() +
  cowplot::draw_grob(
    grid::textGrob(
      footnote_text,
      x   = grid::unit(0.02, "npc"),
      y   = grid::unit(0.5, "npc"),
      hjust = 0, vjust = 0.5,
      gp  = grid::gpar(fontsize = 7, col = "grey35")
    )
  )

p_cmp <- cowplot::plot_grid(
  p_init_titled,
  p_final_titled,
  ncol       = 2,
  rel_widths = c(1, 1),
  align      = "hv",
  axis       = "tblr"
)

p_cmp_with_note <- cowplot::plot_grid(
  p_cmp,
  footnote_grob,
  ncol        = 1,
  rel_heights = c(1, 0.035)
)

ggplot2::ggsave(out_cmp, plot = p_cmp_with_note, width = 14, height = 5.2, dpi = 200, bg = "white")
cat(sprintf("Saved: %s\n", normalizePath(out_cmp, winslash = "/")))

cat("\nDone.\n")
