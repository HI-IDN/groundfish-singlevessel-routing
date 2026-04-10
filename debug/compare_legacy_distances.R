#!/usr/bin/env Rscript

suppressPackageStartupMessages({
  library(DBI)
  library(RSQLite)
  library(ggplot2)
})

args <- commandArgs(trailingOnly = TRUE)

# positional: legacy_db [gsp_db [plot_out]]
legacy_db <- if (length(args) >= 1) args[1] else {
  if (file.exists("legacy_distances.db")) "legacy_distances.db" else "dat/legacy_distances.db"
}
gsp_db <- if (length(args) >= 2) args[2] else {
  if (file.exists("gsp.db")) "gsp.db" else "dat/gsp.db"
}
plot_out <- if (length(args) >= 3) args[3] else NULL

if (!file.exists(legacy_db)) stop(sprintf("DB not found: %s", legacy_db), call. = FALSE)

read_sql <- function(db_path, sql) {
  con <- DBI::dbConnect(RSQLite::SQLite(), dbname = db_path)
  on.exit(DBI::dbDisconnect(con), add = TRUE)
  DBI::dbGetQuery(con, sql)
}

# ---- haversine helper (nautical miles) --------------------------------------
haversine_nm <- function(lat1, lon1, lat2, lon2) {
  R <- 3440.065
  dlat <- (lat2 - lat1) * pi / 180
  dlon <- (lon2 - lon1) * pi / 180
  a <- sin(dlat / 2)^2 + cos(lat1 * pi / 180) * cos(lat2 * pi / 180) * sin(dlon / 2)^2
  2 * R * asin(pmin(1, sqrt(a)))
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

# ============================================================
# Coordinate comparison: legacy  vs  gsp.db
# ============================================================
if (!file.exists(gsp_db)) {
  cat(sprintf("\ngsp_db not found (%s), skipping comparison.\n", gsp_db))
  quit(save = "no", status = 0)
}

cat(sprintf("\n=== Coordinate comparison: legacy vs gsp ===\n"))
cat(sprintf("  legacy: %s\n  gsp:    %s\n\n", legacy_db, gsp_db))

report_worst <- function(cmp, label, threshold = 0.1, max_rows = 10) {
  bad <- cmp[cmp$dist_nm > threshold, ]
  if (nrow(bad) == 0) return(invisible(NULL))
  bad <- bad[order(-bad$dist_nm), ]
  cat(sprintf("  WARNING: %d %s with coord diff > %.2f nm:\n",
              nrow(bad), label, threshold))
  for (i in seq_len(min(max_rows, nrow(bad)))) {
    cat(sprintf("    %-12s  leg=(%.5f, %.5f)  gsp=(%.5f, %.5f)  %.4f nm\n",
                bad$name[i],
                bad$lat_leg[i], bad$lon_leg[i],
                bad$lat_gsp[i], bad$lon_gsp[i],
                bad$dist_nm[i]))
  }
}

# ---- boats ------------------------------------------------------------------
# legacy: type=1 (ship), side=0 (both sides same dock; take 0 for uniqueness)
leg_boats <- read_sql(legacy_db,
  "SELECT DISTINCT from_name AS name,
          from_lat_deg       AS lat,
          from_lon_deg       AS lon
   FROM legacy_distances
   WHERE from_type = 1 AND from_side = 0")
leg_boats$lon  <- -leg_boats$lon   # positive-west -> negative-west
leg_boats$name <- gsub('^"|"$', "", leg_boats$name)  # strip surrounding quotes

gsp_boats <- read_sql(gsp_db,
  "SELECT b.name, l.lat, l.lon
   FROM boats b
   JOIN locations l ON l.id = b.location_id")

boat_cmp <- merge(leg_boats, gsp_boats, by = "name", suffixes = c("_leg", "_gsp"))
boat_cmp$dist_nm <- haversine_nm(boat_cmp$lat_leg, boat_cmp$lon_leg,
                                  boat_cmp$lat_gsp, boat_cmp$lon_gsp)

cat(sprintf("Boats\n"))
cat(sprintf("  legacy: %d   gsp: %d   matched: %d\n",
            nrow(leg_boats), nrow(gsp_boats), nrow(boat_cmp)))

unmatched_leg_b <- setdiff(leg_boats$name, gsp_boats$name)
unmatched_gsp_b <- setdiff(gsp_boats$name, leg_boats$name)
if (length(unmatched_leg_b) > 0)
  cat(sprintf("  in legacy only: %s\n", paste(unmatched_leg_b, collapse = ", ")))
if (length(unmatched_gsp_b) > 0)
  cat(sprintf("  in gsp only:    %s\n", paste(unmatched_gsp_b, collapse = ", ")))

if (nrow(boat_cmp) > 0) {
  cat(sprintf("  coord diff (nm):  mean=%.5f  max=%.5f\n",
              mean(boat_cmp$dist_nm), max(boat_cmp$dist_nm)))
  report_worst(boat_cmp, "boat(s)")
}

# ---- stations ---------------------------------------------------------------
# legacy: type=2 (station), side=0 -> start location, side=1 -> end location
leg_sta <- read_sql(legacy_db,
  "SELECT DISTINCT from_name AS name, from_side AS side,
          from_lat_deg AS lat, from_lon_deg AS lon
   FROM legacy_distances
   WHERE from_type = 2")
leg_sta$lon  <- -leg_sta$lon   # positive-west -> negative-west
leg_sta$name <- gsub('^"|"$', "", leg_sta$name)  # strip surrounding quotes

gsp_sta <- read_sql(gsp_db,
  "SELECT s.ext_id        AS name,
          sl.lat          AS start_lat,
          sl.lon          AS start_lon,
          el.lat          AS end_lat,
          el.lon          AS end_lon
   FROM stations s
   JOIN locations sl ON sl.id = s.start_location_id
   JOIN locations el ON el.id = s.end_location_id")

leg_sta0 <- leg_sta[leg_sta$side == 0, c("name", "lat", "lon")]
leg_sta1 <- leg_sta[leg_sta$side == 1, c("name", "lat", "lon")]

cmp_start <- merge(leg_sta0,
                   gsp_sta[, c("name", "start_lat", "start_lon")],
                   by = "name")
names(cmp_start)[names(cmp_start) == "start_lat"] <- "lat_gsp"
names(cmp_start)[names(cmp_start) == "start_lon"] <- "lon_gsp"
names(cmp_start)[names(cmp_start) == "lat"]       <- "lat_leg"
names(cmp_start)[names(cmp_start) == "lon"]       <- "lon_leg"
cmp_start$dist_nm <- haversine_nm(cmp_start$lat_leg, cmp_start$lon_leg,
                                   cmp_start$lat_gsp, cmp_start$lon_gsp)

cmp_end <- merge(leg_sta1,
                 gsp_sta[, c("name", "end_lat", "end_lon")],
                 by = "name")
names(cmp_end)[names(cmp_end) == "end_lat"] <- "lat_gsp"
names(cmp_end)[names(cmp_end) == "end_lon"] <- "lon_gsp"
names(cmp_end)[names(cmp_end) == "lat"]     <- "lat_leg"
names(cmp_end)[names(cmp_end) == "lon"]     <- "lon_leg"
cmp_end$dist_nm <- haversine_nm(cmp_end$lat_leg, cmp_end$lon_leg,
                                 cmp_end$lat_gsp, cmp_end$lon_gsp)

cat(sprintf("\nStations\n"))
cat(sprintf("  legacy unique names: %d   gsp: %d\n",
            length(unique(leg_sta$name)), nrow(gsp_sta)))
cat(sprintf("  matched start (side 0): %d   end (side 1): %d\n",
            nrow(cmp_start), nrow(cmp_end)))

unmatched_leg_s <- setdiff(unique(leg_sta$name), gsp_sta$name)
unmatched_gsp_s <- setdiff(gsp_sta$name, unique(leg_sta$name))
if (length(unmatched_leg_s) > 0)
  cat(sprintf("  in legacy only: %d  (e.g. %s)\n",
              length(unmatched_leg_s),
              paste(head(unmatched_leg_s, 5), collapse = ", ")))
if (length(unmatched_gsp_s) > 0)
  cat(sprintf("  in gsp only:    %d  (e.g. %s)\n",
              length(unmatched_gsp_s),
              paste(head(unmatched_gsp_s, 5), collapse = ", ")))

if (nrow(cmp_start) > 0)
  cat(sprintf("  start coord diff (nm): mean=%.5f  max=%.5f\n",
              mean(cmp_start$dist_nm), max(cmp_start$dist_nm)))
if (nrow(cmp_end) > 0)
  cat(sprintf("  end   coord diff (nm): mean=%.5f  max=%.5f\n",
              mean(cmp_end$dist_nm), max(cmp_end$dist_nm)))

report_worst(cmp_start, "start station(s)")
report_worst(cmp_end,   "end station(s)")

# ============================================================
# Distance comparison -> dat/debug_distances.csv
# ============================================================
cat(sprintf("\n=== Distance comparison ===\n"))

# -- (name, side) -> gsp location_id lookup -----------------------------------
gsp_sta_locs <- read_sql(gsp_db,
  "SELECT ext_id AS name, start_location_id, end_location_id FROM stations")
gsp_boat_locs <- read_sql(gsp_db,
  "SELECT name, location_id FROM boats")

loc_lookup <- rbind(
  data.frame(name = gsp_sta_locs$name,  side = 0L, loc_id = gsp_sta_locs$start_location_id),
  data.frame(name = gsp_sta_locs$name,  side = 1L, loc_id = gsp_sta_locs$end_location_id),
  data.frame(name = gsp_boat_locs$name, side = 0L, loc_id = gsp_boat_locs$location_id),
  data.frame(name = gsp_boat_locs$name, side = 1L, loc_id = gsp_boat_locs$location_id)
)

# -- legacy pairs: stations + boats only, no self-pairs -----------------------
leg_pairs <- read_sql(legacy_db,
  "SELECT from_name, from_side, from_type,
          to_name,   to_side,   to_type,
          distance_nm AS old_distance,
          feasible    AS old_feasible
   FROM legacy_distances
   WHERE from_type IN (1, 2)
     AND to_type   IN (1, 2)
     AND from_node <> to_node")
leg_pairs$from_name <- gsub('^"|"$', "", leg_pairs$from_name)
leg_pairs$to_name   <- gsub('^"|"$', "", leg_pairs$to_name)

cat(sprintf("  legacy station+boat pairs (excl. self): %d\n", nrow(leg_pairs)))

# -- resolve to gsp location IDs ----------------------------------------------
leg_pairs <- merge(leg_pairs, loc_lookup,
                   by.x = c("from_name", "from_side"), by.y = c("name", "side"))
names(leg_pairs)[names(leg_pairs) == "loc_id"] <- "from_location_id"

leg_pairs <- merge(leg_pairs, loc_lookup,
                   by.x = c("to_name", "to_side"),   by.y = c("name", "side"))
names(leg_pairs)[names(leg_pairs) == "loc_id"] <- "to_location_id"

cat(sprintf("  after resolving to gsp location IDs:    %d\n", nrow(leg_pairs)))

# -- join gsp distances -------------------------------------------------------
gsp_dist <- read_sql(gsp_db,
  "SELECT from_location_id, to_location_id,
          distance_nm AS new_distance,
          crosses_land, waypoint_path
   FROM distances")

result <- merge(leg_pairs, gsp_dist,
                by = c("from_location_id", "to_location_id"),
                all.x = TRUE)

cat(sprintf("  matched to gsp distance rows:           %d\n", sum(!is.na(result$new_distance))))
cat(sprintf("  no gsp distance found:                  %d\n", sum( is.na(result$new_distance))))

# -- summary stats ------------------------------------------------------------
eps   <- 0.01   # nm tolerance for "same"
INF   <- 1e5    # sentinel threshold

cmp <- result[!is.na(result$new_distance), ]
cmp$delta <- cmp$new_distance - cmp$old_distance

old_inf  <- cmp$old_distance >= INF
new_inf  <- cmp$new_distance >= INF

# bucket 1: both infeasible (no change)
both_inf   <- cmp[ old_inf  &  new_inf, ]
# bucket 2: legacy infeasible, new routable (newly reachable — good)
gained     <- cmp[ old_inf  & !new_inf, ]
# bucket 3: legacy routable, new infeasible (regression — bad)
lost       <- cmp[!old_inf  &  new_inf, ]
# bucket 4: both finite — partition into improved / same / worsened
finite     <- cmp[!old_inf  & !new_inf, ]
improved   <- finite[finite$delta < -eps, ]
same       <- finite[abs(finite$delta) <= eps, ]
worsened   <- finite[finite$delta >  eps, ]

fmt_group <- function(df, label, show_delta = TRUE) {
  if (!show_delta || nrow(df) == 0) {
    cat(sprintf("  %-28s  n=%7d\n", label, nrow(df)))
  } else {
    cat(sprintf("  %-28s  n=%7d  mean=%+9.3f nm  median=%+9.3f nm  extremum=%+9.3f nm\n",
                label, nrow(df),
                mean(df$delta), median(df$delta),
                df$delta[which.max(abs(df$delta))]))
  }
}

cat(sprintf("  total matched pairs: %d  (feasibility threshold=1e5, eps=%.3f nm)\n\n",
            nrow(cmp), eps))
cat("  [+] gsp LOWER distance = better\n\n")
fmt_group(gained,   "new       (was inf, now finite)")
fmt_group(improved, "improved  (new < old - eps)")
fmt_group(same,     "same      (|Δ| <= eps)")
fmt_group(worsened, "worsened  (new > old + eps)   <-- BAD")
cat("\n")
if (nrow(lost) > 0)
  fmt_group(lost, "REGRESSION (was finite -> now inf) <-- CRITICAL")
if (nrow(both_inf) > 0)
  cat(sprintf("  (both infeasible, unchanged:  n=%d)\n", nrow(both_inf)))

# -- write CSV ----------------------------------------------------------------
out <- result[, c("from_location_id", "to_location_id",
                  "from_name", "to_name",
                  "new_distance", "old_distance",
                  "crosses_land", "waypoint_path")]
# order w.r.t. most discrepant pairs first
out$dist_diff <- abs(out$new_distance - out$old_distance)
out <- out[order(-out$dist_diff, out$from_location_id, out$to_location_id), ]

csv_out <- "debug/distances.csv"
write.csv(out, csv_out, row.names = FALSE)
cat(sprintf("\nWrote %s  (%d rows)\n", csv_out, nrow(out)))
