#!/usr/bin/env Rscript
# plot_stations.R (robust)
# Reads parsed_data.sqlite and plots island boundary and arrows for stations
# Writes PNG and (optionally) TikZ into Paper/figs

suppressPackageStartupMessages({
  require(DBI)
  require(RSQLite)
  require(ggplot2)
  require(viridis)
  require(dplyr)
})

## Ensure default CRAN repo so install.packages() is non-interactive
options(repos = c(CRAN = "https://cran.r-project.org"))

# Try to ensure tikzDevice is available (non-interactive). If installation fails, we continue
if (!requireNamespace("tikzDevice", quietly = TRUE)) {
  message("Package 'tikzDevice' not installed. Attempting non-interactive install from CRAN...")
  tryCatch({
    install.packages("tikzDevice", repos = getOption("repos"), dependencies = TRUE)
  }, error = function(e) {
    message("Automatic installation of tikzDevice failed: ", conditionMessage(e))
  })
}
has_tikz <- require(tikzDevice, quietly = TRUE)
if (!has_tikz) message("tikzDevice not available; .tex output will be skipped.")

# Detect database path: try a few likely locations depending on where script is run from
possible_db_paths <- c("dat/parsed_data.sqlite", "Code/dat/parsed_data.sqlite", "../dat/parsed_data.sqlite")
db_path <- NULL
for (p in possible_db_paths) {
  if (file.exists(p)) { db_path <- p; break }
}
if (is.null(db_path)) stop("Could not find parsed_data.sqlite in expected locations. Run the parser first or set db_path manually.")

# Determine output directory: prefer repo-root Paper/figs, else try ../Paper/figs
possible_outdirs <- c("Paper/figs", "../Paper/figs")
out_dir <- NULL
for (d in possible_outdirs) {
  if (dir.exists(d)) { out_dir <- d; break }
}
if (is.null(out_dir)) {
  # create the primary location (Paper/figs) relative to current working dir
  out_dir <- "Paper/figs"
  dir.create(out_dir, recursive = TRUE, showWarnings = FALSE)
}
png_out <- file.path(out_dir, "survey_overview.png")
tex_out <- file.path(out_dir, "survey_overview.tikz")

message("Using DB: ", db_path)
message("Output dir: ", out_dir)

con <- dbConnect(RSQLite::SQLite(), db_path)

# Read island polygon (if present)
island_df <- NULL
if (dbExistsTable(con, "island")) {
  island_df <- dbGetQuery(con, "SELECT lat, lon FROM island ORDER BY id ASC;")
  if (nrow(island_df) == 0) island_df <- NULL
}

# Read v_locations and stations
vloc <- dbGetQuery(con, "SELECT id, lat, lon, type FROM v_locations;")
stations <- dbGetQuery(con, "SELECT id, ext_id, start_loc, end_loc, catch, c1, c2, c3, bottom_depth_cast, bottom_depth_haul, comment FROM stations;")

# Read boats to identify special boat locations (start/end locs)
boats <- NULL
if (dbExistsTable(con, "boats")) {
  boats <- dbGetQuery(con, "SELECT id, start_loc, end_loc, name FROM boats;")
} else {
  boats <- data.frame(id=integer(0), start_loc=integer(0), end_loc=integer(0), name=character(0), stringsAsFactors = FALSE)
}

dbDisconnect(con)

if (nrow(stations) == 0) stop("No stations found in DB.")

vl <- as.data.frame(vloc)
st <- as.data.frame(stations)

# Prepare markers for ports and a special boat named exactly "Árni Friðriksson"
special_boat_name <- "Árni Friðriksson"
special_locs <- integer(0)
if (!is.null(boats) && nrow(boats) > 0) {
  special <- boats[!is.na(boats$name) & boats$name == special_boat_name, , drop = FALSE]
  if (nrow(special) > 0) special_locs <- unique(c(special$start_loc, special$end_loc))
}

# markers: all ports + any special boat locations
markers <- vl %>% filter(type == 'P' | id %in% special_locs)
markers$marker_fill <- ifelse(markers$id %in% special_locs, "black", "white")
# marker stroke color
markers$stroke_col <- "black"

st2 <- st %>%
  left_join(vl %>% select(id, lat, lon), by = c("start_loc" = "id")) %>%
  rename(start_lat = lat, start_lon = lon) %>%
  left_join(vl %>% select(id, lat, lon), by = c("end_loc" = "id")) %>%
  rename(end_lat = lat, end_lon = lon)

# Drop rows with missing coords
st2 <- st2 %>% filter(!is.na(start_lat) & !is.na(end_lat))

# compute alpha from catch to emphasize high-catch stations
st2$catch_num <- as.numeric(st2$catch)
maxc <- max(st2$catch_num, na.rm = TRUE)
if (is.finite(maxc) && maxc > 0) {
  st2$alpha <- 0.2 + 0.8 * (sqrt(pmax(0, st2$catch_num)) / sqrt(maxc))
} else {
  st2$alpha <- 0.5
}

## Use continuous numeric catch for coloring (no binning
#  Map lower catches to blue and higher to red.
st2$catch_num <- as.numeric(st2$catch)

# Build plot
p <- ggplot()
if (!is.null(island_df)) {
  p <- p + geom_path(data = island_df, aes(x = lon, y = lat), color = "gray40", size = 0.4)
}

# arrows from start->end, color by numeric catch (blue -> red)
p <- p + geom_segment(data = st2,
                      aes(x = start_lon, y = start_lat, xend = end_lon, yend = end_lat, color = catch_num, alpha = alpha),
                      arrow = grid::arrow(length = unit(0.15, "cm")), lineend = "round",
                      size = 0.6)

# Continuous color scale: blue (low) -> red (high)
p <- p + scale_color_gradient(low = "#2b83ba", high = "#d7191c", name = "Catch")
p <- p + scale_alpha_identity(guide = "none")

## Plot ports first (white-filled), then the special boat locations on top (black-filled)
ports_df <- markers %>% filter(!(id %in% special_locs))
boats_df <- markers %>% filter(id %in% special_locs)

if (nrow(ports_df) > 0) {
  p <- p + geom_point(data = ports_df, aes(x = lon, y = lat), shape = 21, fill = "white", color = "black", stroke = 0.5, size = 3)
}
if (nrow(boats_df) > 0) {
  # draw boats slightly larger so they sit visibly on top
  p <- p + geom_point(data = boats_df, aes(x = lon, y = lat), shape = 21, fill = "black", color = "black", stroke = 0.6, size = 3.5)
}

# Compute mean latitude from plotted points and set an exact fixed ratio: 1/cos(mean_lat)
# coord_fixed(ratio) expects data units: one y unit equals 'ratio' x units, so to compensate
# for longitude degree shrinking with latitude use ratio = 1 / cos(mean_lat_in_radians)
mean_lat <- NA_real_
if (nrow(st2) > 0) {
  mean_lat <- mean(c(st2$start_lat, st2$end_lat), na.rm = TRUE)
}
if (is.na(mean_lat)) mean_lat <- 65.0 # fallback latitude
mean_lat_rad <- mean_lat * pi / 180.0
ratio <- 1.0 / cos(mean_lat_rad)
p <- p + coord_fixed(ratio = ratio) + theme_minimal() + labs(title = NULL, x = NULL, y = NULL)

# Save PNG
ggsave(filename = png_out, plot = p, width = 8, height = 6, dpi = 300)
message("Saved PNG: ", png_out)

# Save TikZ if available
if (has_tikz) {
  tikz(file = tex_out, width = 5, height = 3)
  print(p)
  dev.off()
  message("Saved TikZ: ", tex_out)
} else {
  message("Skipping TikZ output; install tikzDevice and LaTeX to enable .tex export.")
}

invisible(p)
