#!/usr/bin/env Rscript
# Shared plotting/database utilities for GSP R scripts.

load_required_packages <- function(required_packages) {
  for (pkg in required_packages) {
    if (!requireNamespace(pkg, quietly = TRUE)) {
      stop(sprintf(
        "Package '%s' is required but not installed.\nInstall with: install.packages('%s')",
        pkg,
        pkg
      ), call. = FALSE)
    }
    suppressPackageStartupMessages(library(pkg, character.only = TRUE))
  }
}

read_db_table <- function(db_path, sql, params = NULL) {
  if (!file.exists(db_path)) {
    stop(sprintf("Database file not found: %s", db_path), call. = FALSE)
  }

  con <- DBI::dbConnect(RSQLite::SQLite(), dbname = db_path)
  on.exit(DBI::dbDisconnect(con), add = TRUE)

  data <- tryCatch({
    if (is.null(params)) {
      DBI::dbGetQuery(con, sql)
    } else {
      DBI::dbGetQuery(con, sql, params = params)
    }
  }, error = function(e) {
    stop(sprintf("SQL query failed: %s\nQuery: %s", e$message, sql), call. = FALSE)
  })

  cat(sprintf(
    "Loaded %d rows with columns: %s\n",
    nrow(data),
    paste(colnames(data), collapse = ", ")
  ))

  tibble::tibble(data)
}

log_path_for_output <- function(output_file) {
  sub("\\.[Pp][Nn][Gg]$", ".log", output_file)
}

write_log_lines <- function(log_path, lines) {
  dir.create(dirname(log_path), recursive = TRUE, showWarnings = FALSE)
  writeLines(lines, log_path, useBytes = TRUE)
  cat(sprintf("Log saved to: %s\n", normalizePath(log_path, winslash = "/", mustWork = FALSE)))
}

ensure_segment_list <- function(x) {
  if (is.null(x)) return(list())
  if (is.list(x) && !is.data.frame(x)) return(x)
  if (is.data.frame(x)) return(split(x, seq_len(nrow(x))))
  list(x)
}

parse_integer_segment <- function(x) {
  if (is.null(x)) return(integer())
  if (is.character(x)) {
    x <- trimws(paste(x, collapse = " "))
    if (!nzchar(x)) return(integer())
    return(as.integer(strsplit(x, "\\s+")[[1]]))
  }
  as.integer(unlist(x, use.names = FALSE))
}

normalize_station_segments <- function(x) {
  lapply(ensure_segment_list(x), parse_integer_segment)
}

resolve_summary_final_variant <- function(doc) {
  final_name <- doc$summary$status$final
  if (is.null(final_name) || !nzchar(final_name)) {
    final_name <- doc$summary$final
  }
  final_name
}

extract_solution_distance <- function(solution) {
  distance_block <- solution$distance_nm
  if (is.null(distance_block)) {
    stop("solution$distance_nm is missing from this JSON.", call. = FALSE)
  }

  segment_transit <- distance_block$segment$transit
  segment_total <- distance_block$segment$total
  grand_transit <- distance_block$grand_total$transit
  grand_total <- distance_block$grand_total$total

  if (is.null(segment_transit)) {
    stop("solution$distance_nm$segment$transit is missing from this JSON.", call. = FALSE)
  }
  if (is.null(segment_total)) {
    stop("solution$distance_nm$segment$total is missing from this JSON.", call. = FALSE)
  }
  if (is.null(grand_transit) || length(grand_transit) == 0) {
    stop("solution$distance_nm$grand_total$transit is missing from this JSON.", call. = FALSE)
  }
  if (is.null(grand_total) || length(grand_total) == 0) {
    stop("solution$distance_nm$grand_total$total is missing from this JSON.", call. = FALSE)
  }

  list(
    segment_transit = as.numeric(segment_transit),
    segment_total = as.numeric(segment_total),
    grand_transit = as.numeric(grand_transit[[1]]),
    grand_total = as.numeric(grand_total[[1]])
  )
}

load_station_endpoints <- function(db_path) {
  read_db_table(
    db_path,
    "SELECT s.id AS station_id,
            s.start_location_id,
            s.end_location_id,
            ls.lat AS start_lat,
            ls.lon AS start_lon,
            le.lat AS end_lat,
            le.lon AS end_lon
     FROM stations s
     JOIN locations ls ON ls.id = s.start_location_id
     JOIN locations le ON le.id = s.end_location_id"
  )
}

build_station_line_segments <- function(station_segments, station_endpoints) {
  station_segments <- normalize_station_segments(station_segments)
  station_lines <- tibble::tibble()
  station_sequence <- 1

  for (s in seq_along(station_segments)) {
    for (signed_station_id in station_segments[[s]]) {
      station_id <- abs(as.integer(signed_station_id))
      station_row <- station_endpoints[station_endpoints$station_id == station_id, ]
      if (nrow(station_row) > 0) {
        reverse_station <- signed_station_id < 0
        station_lines <- dplyr::bind_rows(
          station_lines,
          tibble::tibble(
            segment = s,
            station_sequence = station_sequence,
            station_id = station_id,
            lat = if (reverse_station) station_row$end_lat[1] else station_row$start_lat[1],
            lon = if (reverse_station) station_row$end_lon[1] else station_row$start_lon[1],
            lat_end = if (reverse_station) station_row$start_lat[1] else station_row$end_lat[1],
            lon_end = if (reverse_station) station_row$start_lon[1] else station_row$end_lon[1]
          )
        )
        station_sequence <- station_sequence + 1
      }
    }
  }

  station_lines
}

base_coastline_plot <- function(coastline_df) {
  ggplot2::ggplot(coastline_df, ggplot2::aes(x = lon, y = lat)) +
    ggplot2::geom_path(color = "gray30", linewidth = 0.3, alpha = 0.6)
}

apply_degree_axes <- function(plot_obj) {
  plot_obj +
    ggplot2::scale_x_continuous(labels = function(x) paste0(x, "°")) +
    ggplot2::scale_y_continuous(labels = function(y) paste0(y, "°"))
}

coord_fixed_for_lat <- function(lat_values, fallback_lat = 65.0) {
  mean_lat <- mean(lat_values, na.rm = TRUE)
  if (is.na(mean_lat)) {
    mean_lat <- fallback_lat
  }
  ratio <- 1.0 / cos(mean_lat * pi / 180.0)
  ggplot2::coord_fixed(ratio = ratio)
}

compute_interior_label_position <- function(x, y, ref_x, ref_y, x_frac = 0.07, y_frac = 0.05) {
  x_range <- range(ref_x, na.rm = TRUE)
  y_range <- range(ref_y, na.rm = TRUE)
  x_span <- diff(x_range)
  y_span <- diff(y_range)
  x_mid <- mean(x_range)
  y_mid <- mean(y_range)

  tibble::tibble(
    label_lon = x + ifelse(x <= x_mid, 1, -1) * x_span * x_frac,
    label_lat = y + ifelse(y <= y_mid, 1, -1) * y_span * y_frac
  )
}

gsp_common_theme <- function(legend_position = "right", legend_direction = "vertical") {
  ggplot2::theme_minimal() +
    ggplot2::theme(
      legend.position = legend_position,
      legend.direction = legend_direction,
      plot.title = ggplot2::element_text(hjust = 0.5, size = 16, face = "bold"),
      plot.subtitle = ggplot2::element_text(hjust = 0.5, size = 12),
      axis.text.x = ggplot2::element_text(size = 10),
      axis.text.y = ggplot2::element_text(size = 10),
      panel.grid.major = ggplot2::element_blank(),
      panel.grid.minor = ggplot2::element_blank(),
      panel.background = ggplot2::element_blank(),
      plot.background = ggplot2::element_rect(fill = "white", color = NA)
    )
}

