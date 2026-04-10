#!/usr/bin/env Rscript
# Compare two no-port JSON solutions side-by-side on the same map.
#
# Usage (from repo root):
#   Rscript debug/plot_noport_compare.R \
#       sol/noport/noport.json \
#       debug/legacy_noport.json \
#       dat/gsp.db \
#       debug/noport_compare.png

suppressPackageStartupMessages({
  library(DBI)
  library(RSQLite)
  library(ggplot2)
  library(jsonlite)
  library(dplyr)
  library(tibble)
})

# ---- args -------------------------------------------------------------------
args        <- commandArgs(trailingOnly = TRUE)
new_json    <- if (length(args) >= 1) args[1] else "sol/noport/noport.json"
legacy_json <- if (length(args) >= 2) args[2] else "debug/legacy_noport.json"
db_path     <- if (length(args) >= 3) args[3] else "dat/gsp.db"
out_png     <- if (length(args) >= 4) args[4] else "debug/noport_compare.png"

# Source shared utils (look next to this script, then fall back to R/)
script_arg  <- grep("^--file=", commandArgs(FALSE), value = TRUE)
script_dir  <- if (length(script_arg)) dirname(normalizePath(sub("^--file=", "", script_arg[1]))) else "debug"
utils_paths <- c(file.path(script_dir, "../../R/plot_utils.R"),
                 "R/plot_utils.R")
utils_found <- Filter(file.exists, utils_paths)[1]
if (!is.na(utils_found)) source(utils_found) else stop("plot_utils.R not found", call. = FALSE)

stopifnot(file.exists(new_json), file.exists(legacy_json), file.exists(db_path))

# ---- db ---------------------------------------------------------------------
cat("Loading DB...\n")
con       <- dbConnect(SQLite(), dbname = db_path)
coastline <- dbGetQuery(con, "SELECT lat, lon FROM coastline")
locations <- dbGetQuery(con, "SELECT id, lat, lon FROM locations")
station_endpoints <- dbGetQuery(con,
  "SELECT s.id AS station_id,
          s.start_location_id, s.end_location_id,
          ls.lat AS start_lat, ls.lon AS start_lon,
          le.lat AS end_lat,   le.lon AS end_lon
   FROM stations s
   JOIN locations ls ON ls.id = s.start_location_id
   JOIN locations le ON le.id = s.end_location_id")
dbDisconnect(con)

# ---- load a solution from JSON ----------------------------------------------
load_solution <- function(path) {
  j <- fromJSON(path, simplifyVector = FALSE)
  variant <- j$summary$final
  if (is.null(variant) || is.null(j$solution[[variant]]))
    stop(sprintf("Cannot find solution variant '%s' in %s", variant, path), call. = FALSE)
  sol <- j$solution[[variant]]
  list(
    path          = path,
    label         = basename(dirname(path)),   # e.g. "noport" or "debug"
    variant       = variant,
    boat_name     = j$metadata$boat_name %||% "unknown",
    boat_loc_id   = as.integer(j$metadata$boat_location_id %||% -1L),
    boat_lat      = j$metadata$boat_docked_location$lat %||% NA_real_,
    boat_lon      = j$metadata$boat_docked_location$lon %||% NA_real_,
    distance_nm   = as.numeric(sol$total_distance_nm %||% NA_real_),
    seg_locs      = ensure_segment_list(sol$tour_segments_location_ids),
    seg_stations  = normalize_station_segments(sol$tour_segments_station_ids)
  )
}

`%||%` <- function(a, b) if (!is.null(a)) a else b

cat(sprintf("Loading new:    %s\n", new_json))
new_sol <- load_solution(new_json)
cat(sprintf("Loading legacy: %s\n", legacy_json))
leg_sol <- load_solution(legacy_json)

solutions <- list(
  list(key = "new",    sol = new_sol, color = "#1565C0", lty = "solid",  label = sprintf("New   (%s) - %.0f nm", new_sol$variant, new_sol$distance_nm)),
  list(key = "legacy", sol = leg_sol, color = "#C62828", lty = "dashed", label = sprintf("Legacy (%s) - %.0f nm", leg_sol$variant, leg_sol$distance_nm))
)

# ---- build route paths and station lines ------------------------------------
route_path    <- tibble()
station_lines <- tibble()

for (entry in solutions) {
  sol   <- entry$sol
  key   <- entry$key

  for (seg_idx in seq_along(sol$seg_locs)) {
    seg  <- as.integer(unlist(sol$seg_locs[[seg_idx]]))
    pts  <- tibble(location_id = seg) %>%
      left_join(locations, by = c("location_id" = "id")) %>%
      filter(!is.na(lat), !is.na(lon)) %>%
      mutate(
        source      = key,
        variant     = sol$variant,
        color_key   = key,
        segment     = seg_idx,
        point_order = seq_len(n())
      )
    route_path <- bind_rows(route_path, pts)
  }

  sl <- build_station_line_segments(sol$seg_stations, station_endpoints) %>%
    mutate(source = key, color_key = key)
  station_lines <- bind_rows(station_lines, sl)
}

# ---- dock points ------------------------------------------------------------
dock_pts <- tibble(
  lat      = c(new_sol$boat_lat, leg_sol$boat_lat),
  lon      = c(new_sol$boat_lon, leg_sol$boat_lon),
  label    = c(new_sol$boat_name, leg_sol$boat_name)
) %>% filter(!is.na(lat))

# ---- color / linetype scales ------------------------------------------------
color_vals <- setNames(
  sapply(solutions, `[[`, "color"),
  sapply(solutions, `[[`, "key")
)
lty_vals <- setNames(
  sapply(solutions, `[[`, "lty"),
  sapply(solutions, `[[`, "key")
)
legend_labels <- setNames(
  sapply(solutions, `[[`, "label"),
  sapply(solutions, `[[`, "key")
)

cat(sprintf("\nNew    distance: %.2f nm  (%s)\n", new_sol$distance_nm, new_sol$variant))
cat(sprintf("Legacy distance: %.2f nm  (%s)\n",  leg_sol$distance_nm, leg_sol$variant))
cat(sprintf("Delta (new-leg): %+.2f nm\n\n", new_sol$distance_nm - leg_sol$distance_nm))

# ---- plot -------------------------------------------------------------------
p <- base_coastline_plot(coastline) +

  # routes
  geom_path(
    data = route_path,
    aes(x = lon, y = lat,
        group    = interaction(source, segment),
        colour   = color_key,
        linetype = color_key),
    linewidth = 0.5, alpha = 0.55, lineend = "round"
  ) +

  # station tow lines
  geom_segment(
    data = station_lines,
    aes(x = lon, y = lat, xend = lon_end, yend = lat_end,
        colour   = color_key,
        linetype = color_key),
    linewidth = 1.1, alpha = 0.85, lineend = "round"
  ) +

  # dock marker
  geom_point(
    data = dock_pts,
    aes(x = lon, y = lat),
    shape = 23, size = 3.5, fill = "gold", colour = "black", stroke = 0.7,
    inherit.aes = FALSE
  ) +

  scale_colour_manual(
    values = color_vals, labels = legend_labels, name = NULL
  ) +
  scale_linetype_manual(
    values = lty_vals,   labels = legend_labels, name = NULL
  ) +

  coord_fixed_for_lat(route_path$lat) +

  labs(
    title    = "No-port route comparison: new vs legacy",
    subtitle = sprintf(
      "New %.0f nm (%s)   |   Legacy %.0f nm (%s)   |   Δ %+.0f nm",
      new_sol$distance_nm, new_sol$variant,
      leg_sol$distance_nm, leg_sol$variant,
      new_sol$distance_nm - leg_sol$distance_nm
    ),
    x = NULL, y = NULL,
    caption = sprintf("new: %s\nlegacy: %s", new_json, legacy_json)
  )

p <- apply_degree_axes(p)
p <- p + gsp_common_theme(legend_position = "bottom", legend_direction = "horizontal")

dir.create(dirname(out_png), recursive = TRUE, showWarnings = FALSE)
ggsave(out_png, p, width = 11, height = 8, dpi = 200, bg = "white")
cat(sprintf("Saved: %s\n", normalizePath(out_png)))
