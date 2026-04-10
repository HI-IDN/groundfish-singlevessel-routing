#!/usr/bin/env Rscript

suppressPackageStartupMessages({
  library(DBI)
  library(RSQLite)
  library(ggplot2)
})

args <- commandArgs(trailingOnly = TRUE)

# positional: legacy_db [plot_out]
legacy_db <- if (length(args) >= 1) args[1] else {
  if (file.exists("legacy_distances.db")) "legacy_distances.db" else "dat/legacy_distances.db"
}
plot_out <- if (length(args) >= 2) args[2] else NULL

if (!file.exists(legacy_db)) stop(sprintf("DB not found: %s", legacy_db), call. = FALSE)

read_sql <- function(db_path, sql) {
  con <- DBI::dbConnect(RSQLite::SQLite(), dbname = db_path)
  on.exit(DBI::dbDisconnect(con), add = TRUE)
  DBI::dbGetQuery(con, sql)
}

# ---- coastline ---------------------------------------------------------------
coast <- read_sql(legacy_db, "SELECT seq, lat, lon FROM coastline ORDER BY seq")
cat(sprintf("coastline: %d points  lat [%.3f, %.3f]  lon [%.3f, %.3f]\n",
            nrow(coast), min(coast$lat), max(coast$lat),
            min(coast$lon), max(coast$lon)))

# ---- feasibility / routing summary ------------------------------------------
feas <- read_sql(legacy_db,
  "SELECT feasible, distance_nm FROM legacy_distances")

total        <- nrow(feas)
n_direct     <- sum(feas$feasible == 1)
n_land       <- sum(feas$feasible == 0)
n_routed     <- sum(feas$feasible == 0 & feas$distance_nm < 1e9)
n_infeasible <- sum(feas$feasible == 0 & feas$distance_nm >= 1e9)

cat(sprintf("\ndistance matrix: %d pairs\n", total))
cat(sprintf("  direct (no land crossing): %d  (%.1f%%)\n",
            n_direct, 100 * n_direct / total))
cat(sprintf("  crosses land -> Dijkstra:  %d  (%.1f%%)\n",
            n_land, 100 * n_land / total))
cat(sprintf("    routed successfully:      %d\n", n_routed))
cat(sprintf("    no path (infeasible):     %d\n\n", n_infeasible))

rm(feas)

# ---- unique locations from distance matrix -----------------------------------
locs <- read_sql(legacy_db,
  "SELECT from_lat_deg AS lat, from_lon_deg AS lon,
          from_type    AS type, from_name    AS name
   FROM legacy_distances
   UNION
   SELECT to_lat_deg, to_lon_deg, to_type, to_name
   FROM legacy_distances")

type_labels <- c("1" = "ship", "2" = "station", "3" = "waypoint",
                 "4" = "endpoint", "5" = "port")
locs$type_label <- type_labels[as.character(locs$type)]
locs$type_label[is.na(locs$type_label)] <- "other"
locs <- unique(locs)

cat(sprintf("  raw lon range from DB: [%.3f, %.3f]\n", min(locs$lon), max(locs$lon)))

# legacy longitudes are stored positive-west (west = +); coastline uses standard
# negative-west convention — flip sign to align the two layers
locs$lon <- -locs$lon

cat(sprintf("  lon after sign fix:    [%.3f, %.3f]\n", min(locs$lon), max(locs$lon)))

cat(sprintf("locations:  %d unique points\n", nrow(locs)))
cat(sprintf("  types: %s\n", paste(sort(unique(locs$type_label)), collapse = ", ")))

# ---- plot --------------------------------------------------------------------
lat_mid <- mean(locs$lat, na.rm = TRUE)

p <- ggplot() +
  geom_path(data = coast, aes(x = lon, y = lat),
            colour = "grey40", linewidth = 0.35) +
  geom_point(data = locs, aes(x = lon, y = lat,
                               colour = type_label, shape = type_label),
             size = 2, alpha = 0.85) +
  coord_fixed(ratio = 1 / cos(lat_mid * pi / 180)) +
  scale_colour_brewer(palette = "Set1") +
  labs(title = sprintf("Legacy DB locations  (n = %d)", nrow(locs)),
       x = "Longitude (°)", y = "Latitude (°)",
       colour = "Type", shape = "Type") +
  theme_minimal(base_size = 11)

if (!is.null(plot_out)) {
  ggsave(plot_out, p, width = 10, height = 7, dpi = 150)
  cat(sprintf("Plot saved: %s\n", plot_out))
} else {
  print(p)
}


