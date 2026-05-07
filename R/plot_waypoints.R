#!/usr/bin/env Rscript

source("R/gsp_db.R")
source("R/gsp_plot_utils.R")

parse_args <- function(args) {
  out <- list(
    gsp_db = "dat/gsp.db",
    solution_db = "dat/solution.db",
    output = "dat/waypoints.png",
    ports = "all",
    title = "Coastline, Waypoints, and Ports"
  )
  i <- 1L
  while (i <= length(args)) {
    key <- args[[i]]
    val <- if (i < length(args)) args[[i + 1L]] else NULL
    if (!startsWith(key, "--") || is.null(val)) {
      stop(sprintf("Invalid argument near: %s", key), call. = FALSE)
    }
    name <- sub("^--", "", key)
    if (!name %in% names(out)) stop(sprintf("Unknown option: %s", key), call. = FALSE)
    out[[name]] <- val
    i <- i + 2L
  }
  out
}

parse_id_list <- function(x) {
  if (is.null(x) || x %in% c("all", "ALL", "*")) return(NULL)
  if (x %in% c("none", "NONE", "-")) return(integer())
  as.integer(strsplit(x, ",", fixed = TRUE)[[1]])
}

main <- function() {
  load_gsp_plot_packages()
  opt <- parse_args(commandArgs(trailingOnly = TRUE))

  con <- connect_gsp_db(opt$gsp_db, opt$solution_db)
  on.exit(DBI::dbDisconnect(con), add = TRUE)

  country <- read_country_layers(con)
  waypoints <- read_waypoints(con)
  ports <- read_ports(con, parse_id_list(opt$ports))

  plot <- plot_survey_waypoints(
    country = country,
    waypoints = waypoints,
    ports = ports,
    title = opt$title,
    show_degree_axes = TRUE
  )

  dir.create(dirname(opt$output), recursive = TRUE, showWarnings = FALSE)
  ggplot2::ggsave(opt$output, plot, width = 8.2, height = 6.0, dpi = 300, bg = "white")
  message("Wrote ", opt$output)
}

main()
