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

