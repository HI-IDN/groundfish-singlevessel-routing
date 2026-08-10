#!/usr/bin/env Rscript

source("R/gsp_db.R")
source("R/gsp_plot_utils.R")

if (!requireNamespace("cowplot", quietly = TRUE)) {
  stop("Missing required R package: cowplot", call. = FALSE)
}

parse_args <- function(args) {
  out <- list(
    gsp_db = "dat/gsp.db",
    solution_db = "dat/solution.db",
    method = "all",
    output = NULL,
    ports = "all",
    table_corner_a = "upper_right",
    table_corner_b = "upper_right",
    skip_existing = "true"
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

valid_methods <- function() {
  c("nn", "ge", "ci", "noport", "fixedport", "lkh")
}

parse_bool <- function(x) {
  if (is.logical(x)) return(isTRUE(x))
  value <- tolower(trimws(x))
  if (value %in% c("1", "true", "t", "yes", "y")) return(TRUE)
  if (value %in% c("0", "false", "f", "no", "n")) return(FALSE)
  stop(sprintf("Invalid boolean value: %s", x), call. = FALSE)
}

parse_methods <- function(x) {
  if (is.null(x) || !nzchar(x) || x %in% c("all", "ALL", "*")) {
    return(valid_methods())
  }
  methods <- trimws(strsplit(x, ",", fixed = TRUE)[[1]])
  methods <- methods[nzchar(methods)]
  unknown <- setdiff(methods, valid_methods())
  if (length(unknown) > 0L) {
    stop(
      sprintf(
        "Unknown method(s): %s. Use one or more of: %s, or all.",
        paste(unknown, collapse = ", "),
        paste(valid_methods(), collapse = ", ")
      ),
      call. = FALSE
    )
  }
  methods
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
    lkh = "LKH (Auckland)",
    method
  )
}

method_code_label <- function(method) {
  switch(method,
    nn = "MH-NN",
    ge = "MH-GE",
    ci = "MH-CI",
    noport = "MH-OPT",
    fixedport = "C-MIP",
    lkh = "LKH",
    method
  )
}

method_initialisation_label <- function(method) {
  switch(method,
    nn = "nearest neighbor initialisation",
    ge = "greedy edge initialisation",
    ci = "cheapest insertion initialisation",
    noport = "no-port initialisation",
    fixedport = "fixed-port initialisation",
    lkh = "LKH construction (Adams & Walker)",
    method_label(method)
  )
}

grand_transit <- function(route) {
  total <- route$distances[is.na(route$distances$segment), , drop = FALSE]
  if (nrow(total) > 0L) return(total$transit_nm[[1]])
  sum(route$distances$transit_nm, na.rm = TRUE)
}

format_runtime <- function(seconds) {
  if (is.null(seconds) || length(seconds) == 0L || is.na(seconds)) return("n/a")
  units <- c(
    "week" = 7 * 24 * 3600,
    "day" = 24 * 3600,
    "hr" = 3600,
    "min" = 60,
    "s" = 1
  )
  for (unit in names(units)) {
    value <- seconds / units[[unit]]
    if (value >= 1 || identical(unit, "s")) {
      label <- if (unit %in% c("week", "day") && round(value, 1) != 1) paste0(unit, "s") else unit
      return(sprintf("%.1f %s", value, label))
    }
  }
}

route_status <- function(route) {
  feasible <- route$run$feasible[[1]]
  if (!is.null(feasible) && length(feasible) > 0L && !is.na(feasible)) {
    return(if (as.integer(feasible) == 1L) "feasible" else "infeasible")
  }
  "status unknown"
}

segment_count_label <- function(route) {
  n <- route$run$n_segments[[1]]
  if (is.null(n) || length(n) == 0L || is.na(n)) {
    n <- length(unique(route$route_path$segment))
  }
  sprintf("%d segment%s", n, if (n == 1L) "" else "s")
}

visited_ports <- function(ports, ...) {
  routes <- list(...)
  location_ids <- unique(unlist(lapply(routes, function(route) route$route_path$location_id)))
  ports[ports$location_id %in% location_ids, , drop = FALSE]
}

output_path_for_method <- function(method, output, method_count) {
  if (is.null(output) || !nzchar(output)) {
    return(file.path("sol", method, "mh_phase0.png"))
  }
  if (method_count == 1L) return(output)
  file.path(output, method, "mh_phase0.png")
}

plot_method <- function(con, opt, method, output) {
  if (parse_bool(opt$skip_existing) && file.exists(output)) {
    message("Skipping ", method, " (file exists): ", output)
    return(invisible(FALSE))
  }

  construction_run_id <- read_final_run_id(con, method, "construction")
  segmentation_run_id <- read_final_run_id(con, method, "segmentation")

  country <- read_country_layers(con)
  construction <- read_route_run(con, construction_run_id)
  segmentation <- read_route_run(con, segmentation_run_id)
  all_ports <- read_ports(con, parse_id_list(opt$ports))
  construction_ports <- visited_ports(all_ports, construction)
  segmentation_ports <- visited_ports(all_ports, segmentation)
  vessels <- read_vessels(con, construction$run$boat_id)

  all_route_points <- rbind(
    construction$route_path[, c("lon", "lat"), drop = FALSE],
    segmentation$route_path[, c("lon", "lat"), drop = FALSE]
  )
  bounds <- plot_bounds(country$coastline, all_route_points)

  caption_text <- sprintf(
    "%s phase 0: %s",
    method_code_label(method),
    method_initialisation_label(method)
  )
  p_a <- plot_country_or_route(
    country = country,
    route = construction,
    ports = construction_ports,
    vessels = vessels,
    table_corner = opt$table_corner_a,
    title = sprintf("A — Construction (%s)", route_status(construction)),
    subtitle = sprintf(
      "Transit %.0f nm | %s | Runtime %s",
      grand_transit(construction),
      segment_count_label(construction),
      format_runtime(construction$run$runtime_seconds[[1]])
    ),
    legend_position = "none",
    bounds_override = bounds
  )

  p_b <- plot_country_or_route(
    country = country,
    route = segmentation,
    ports = segmentation_ports,
    vessels = vessels,
    table_corner = opt$table_corner_b,
    title = sprintf("B — Segmentation (%s)", route_status(segmentation)),
    subtitle = sprintf(
      "Transit %.0f nm | %s | Runtime %s",
      grand_transit(segmentation),
      segment_count_label(segmentation),
      format_runtime(segmentation$run$runtime_seconds[[1]])
    ),
    legend_position = "none",
    bounds_override = bounds
  )

  dir.create(dirname(output), recursive = TRUE, showWarnings = FALSE)

  panel_row <- cowplot::plot_grid(p_a, p_b, ncol = 2, align = "hv", axis = "tblr")
  title <- cowplot::ggdraw() +
    cowplot::draw_label(caption_text, fontface = "bold", size = 16, x = 0.5, hjust = 0.5)
  combined <- cowplot::plot_grid(title, panel_row, ncol = 1, rel_heights = c(0.08, 1))
  ggplot2::ggsave(output, combined, width = 11.5, height = 4, dpi = 300, bg = "white")
  message("Wrote ", output)
  invisible(TRUE)
}

main <- function() {
  load_gsp_plot_packages()
  opt <- parse_args(commandArgs(trailingOnly = TRUE))

  methods <- parse_methods(opt$method)

  con <- connect_gsp_db(opt$gsp_db, opt$solution_db)
  on.exit(DBI::dbDisconnect(con), add = TRUE)

  for (method in methods) {
    output <- output_path_for_method(method, opt$output, length(methods))
    tryCatch(
      plot_method(con, opt, method, output),
      error = function(e) {
        message("Skipping ", method, " (unable to plot): ", conditionMessage(e))
      }
    )
  }
}

main()
