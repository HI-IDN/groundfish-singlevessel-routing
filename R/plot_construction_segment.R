#!/usr/bin/env Rscript

source("R/gsp_db.R")
source("R/gsp_plot_utils.R")

parse_args <- function(args) {
  out <- list(
    gsp_db = "dat/gsp.db",
    solution_db = "dat/solution.db",
    method = "ci",
    output = NULL,
    ports = "all",
    table_corner_a = "upper_right",
    table_corner_b = "upper_right"
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

method_label <- function(method) {
  switch(method,
    nn = "Nearest Neighbor",
    ge = "Greedy Edge",
    ci = "Cheapest Insertion",
    noport = "No-Port",
    fixedport = "Fixed-Port",
    method
  )
}

main <- function() {
  load_gsp_plot_packages()
  opt <- parse_args(commandArgs(trailingOnly = TRUE))

  con <- connect_gsp_db(opt$gsp_db, opt$solution_db)
  on.exit(DBI::dbDisconnect(con), add = TRUE)

  construction_run_id <- read_final_run_id(con, opt$method, "construction")
  segmentation_run_id <- read_final_run_id(con, opt$method, "segmentation")

  country <- read_country_layers(con)
  construction <- read_route_run(con, construction_run_id)
  segmentation <- read_route_run(con, segmentation_run_id)
  ports <- read_ports(con, parse_id_list(opt$ports))
  vessels <- read_vessels(con, construction$run$boat_id)

  all_route_points <- rbind(
    construction$route_path[, c("lon", "lat"), drop = FALSE],
    segmentation$route_path[, c("lon", "lat"), drop = FALSE]
  )
  bounds <- plot_bounds(country$coastline, all_route_points)

  label <- method_label(opt$method)
  p_a <- plot_country_or_route(
    country = country,
    route = construction,
    ports = ports,
    vessels = vessels,
    table_corner = opt$table_corner_a,
    title = NULL,
    legend_position = "none",
    bounds_override = bounds
  )

  p_b <- plot_country_or_route(
    country = country,
    route = segmentation,
    ports = ports,
    vessels = vessels,
    table_corner = opt$table_corner_b,
    title = NULL,
    legend_position = "none",
    bounds_override = bounds
  )

  output <- opt$output
  if (is.null(output) || !nzchar(output)) {
    output <- file.path("sol", opt$method, "mh_phase0.png")
  }
  dir.create(dirname(output), recursive = TRUE, showWarnings = FALSE)

  combined <- gridExtra::arrangeGrob(
    p_a,
    p_b,
    ncol = 2,
    top = grid::grobTree(
      grid::textGrob(
        label,
        y = 0.78,
        gp = grid::gpar(fontsize = 16, fontface = "bold")
      ),
      grid::textGrob(
        "A: Construction    B: Segmentation",
        y = 0.28,
        gp = grid::gpar(fontsize = 11)
      )
    )
  )
  ggplot2::ggsave(output, combined, width = 11.5, height = 5.2, dpi = 300, bg = "white")
  message("Wrote ", output)
}

main()
