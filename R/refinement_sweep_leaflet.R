#!/usr/bin/env Rscript
# Interactive Leaflet/Shiny app for exploring refinement sweeps.
# Usage:  Rscript R/refinement_sweep_leaflet.R [refinement_file]
#   or:   source("R/refinement_sweep_leaflet.R")  inside RStudio

required_packages <- c("shiny", "leaflet", "dplyr", "tibble", "jsonlite",
                       "DBI", "RSQLite", "viridisLite")

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
# Global parameters
# ---------------------------------------------------------------------------

# Segments to highlight with a thicker gold outline (1-based indices).
# Set to integer(0) to disable.
highlight_segments <- integer(0)
# highlight_segments <- c(3L, 7L)

# ---------------------------------------------------------------------------
# Single-solution JSON loader (construction.json / segment.json)
# ---------------------------------------------------------------------------

load_single_solution_from_json <- function(path) {
  if (!file.exists(path)) return(NULL)
  doc <- tryCatch(
    jsonlite::fromJSON(path),
    error = function(e) { message(sprintf("Skipping %s: %s", path, e$message)); NULL }
  )
  if (is.null(doc)) return(NULL)
  final_variant <- tryCatch({
    fn <- doc$summary$status$final
    if (is.null(fn) || !nzchar(fn)) stop("no final")
    fn
  }, error = function(e) {
    keys <- names(doc$solution)
    keys[!keys %in% c("metadata", "problem")][1]
  })
  if (is.null(final_variant) || is.na(final_variant)) return(NULL)
  sol <- doc$solution[[final_variant]]
  if (is.null(sol)) return(NULL)
  sol$tour_segments_location_ids <- ensure_segment_list(sol$tour_segments_location_ids)
  sol$tour_segments_station_ids  <- normalize_station_segments(sol$tour_segments_station_ids)
  list(solution = sol, doc = doc)
}

# ---------------------------------------------------------------------------
# Load files
# ---------------------------------------------------------------------------

args            <- commandArgs(trailingOnly = TRUE)
refinement_file <- if (length(args) >= 1) args[1] else "sol/noport/refinement_180.json"

cat(sprintf("Loading: %s\n", refinement_file))
rf       <- load_refinement_json(refinement_file)
map_data <- load_map_data()

sol_dir          <- dirname(refinement_file)
construction_obj <- load_single_solution_from_json(file.path(sol_dir, "construction.json"))
segment_obj      <- load_single_solution_from_json(file.path(sol_dir, "segment.json"))

# Build combined phase list.
# "init" from refinement is always the same route as segment.json — always
# skip it; the segment phase (added below as an extra) acts as the baseline.
rf_pass_names <- rf$pass_names[rf$pass_names != "init"]

extra_names     <- character(0)
extra_solutions <- list()
extra_distances <- list()

add_extra <- function(name, obj) {
  if (is.null(obj)) return(invisible(NULL))
  cat(sprintf("  + %s found\n", name))
  extra_names     <<- c(extra_names, name)
  extra_solutions <<- c(extra_solutions, setNames(list(obj$solution), name))
  extra_distances <<- c(extra_distances, setNames(
    list(tryCatch(extract_solution_distance(obj$solution), error = function(e) NULL)),
    name))
}
add_extra("construction", construction_obj)
add_extra("segment",      segment_obj)

all_names     <- c(extra_names,     rf_pass_names)
all_solutions <- c(extra_solutions, rf$solutions[rf_pass_names])
all_distances <- c(extra_distances, rf$distances[rf_pass_names])
n_passes      <- length(all_names)
init_transit  <- rf$init_transit          # always the refinement init baseline
boat_location_id <- as.integer(rf$doc$metadata$boat_location_id)

# ---------------------------------------------------------------------------
# Phase labels
# ---------------------------------------------------------------------------

phase_label <- function(name) {
  switch(name,
    construction = "Construction",
    segment      = "Segmentation",
    init         = "Improvement (init)",
    sprintf("Improvement %d", as.integer(sub("^pass", "", name)))
  )
}
sweep_labels <- vapply(all_names, phase_label, character(1))

# ---------------------------------------------------------------------------
# Colour palette (fixed size = max segments across all phases)
# ---------------------------------------------------------------------------

n_segments <- max(vapply(all_solutions, function(s)
  length(s$tour_segments_location_ids), integer(1)))
seg_colors <- viridisLite::viridis(n_segments, option = "turbo")

seg_color <- function(seg) seg_colors[min(as.integer(seg), n_segments)]

# ---------------------------------------------------------------------------
# Precompute ALL drawing data at startup — zero work at slider-move time
# ---------------------------------------------------------------------------

precompute_phase <- function(solution) {
  route         <- build_regular_route_path(solution, map_data$locations, boat_location_id)
  station_lines <- build_station_line_segments(solution$tour_segments_station_ids,
                                               map_data$station_endpoints)
  ports <- route |> dplyr::filter(point_type == "PORT") |>
           dplyr::distinct(lat, lon, .keep_all = TRUE)

  seg_ids <- sort(unique(route$segment))
  n_segs  <- length(seg_ids)

  # ---- Route: list-of-vectors (one per segment) for single addPolylines call
  route_lats    <- vector("list", n_segs)
  route_lons    <- vector("list", n_segs)
  route_colors  <- character(n_segs)
  route_weights <- numeric(n_segs)
  route_opacities <- numeric(n_segs)
  route_labels  <- character(n_segs)

  for (k in seq_len(n_segs)) {
    seg <- seg_ids[k]
    df  <- route[route$segment == seg, ]
    route_lats[[k]]    <- df$lat
    route_lons[[k]]    <- df$lon
    is_hl              <- seg %in% highlight_segments
    route_colors[k]    <- if (is_hl) "#FFD700" else seg_color(seg)
    route_weights[k]   <- if (is_hl) 5 else 1.5
    route_opacities[k] <- if (is_hl) 1.0 else 0.75
    route_labels[k]    <- sprintf("Segment %d%s", seg, if (is_hl) " \u2605" else "")
  }

  # ---- Station lines: list-of-vectors (one NA-interleaved set per segment)
  sl_ids    <- sort(unique(station_lines$segment))
  n_sl_segs <- length(sl_ids)
  sl_lats   <- vector("list", n_sl_segs)
  sl_lons   <- vector("list", n_sl_segs)
  sl_colors <- character(n_sl_segs)

  for (k in seq_len(n_sl_segs)) {
    seg <- sl_ids[k]
    sl  <- station_lines[station_lines$segment == seg, ]
    sl_lats[[k]] <- c(rbind(sl$lat,   sl$lat_end,  NA_real_))
    sl_lons[[k]] <- c(rbind(sl$lon,   sl$lon_end,  NA_real_))
    sl_colors[k] <- seg_color(seg)
  }

  # ---- Segment midpoints for number labels
  seg_mids <- route |>
    dplyr::group_by(segment) |>
    dplyr::summarise(
      lat = lat[ceiling(dplyr::n() / 2)],
      lon = lon[ceiling(dplyr::n() / 2)],
      .groups = "drop"
    ) |>
    dplyr::mutate(color = vapply(segment, seg_color, character(1)))

  list(
    seg_ids        = seg_ids,
    route_lats     = route_lats,    route_lons      = route_lons,
    route_colors   = route_colors,  route_weights   = route_weights,
    route_opacities = route_opacities, route_labels  = route_labels,
    sl_lats        = sl_lats,       sl_lons         = sl_lons,
    sl_colors      = sl_colors,
    seg_mids       = seg_mids,
    ports          = ports
  )
}

cat("Precomputing drawing data for all phases...\n")
all_draw <- lapply(all_solutions, precompute_phase)
names(all_draw) <- all_names
cat(sprintf("  Done (%d phases)\n", n_passes))

# Bounding box
all_routes <- dplyr::bind_rows(lapply(all_solutions, build_route_path, locations = map_data$locations))
bounds     <- compute_map_bounds(all_routes)

# Index of "init" overlay reference (refinement improvement baseline)
init_ref_name <- if ("init" %in% all_names) "init" else
                 if ("segment" %in% all_names) "segment" else all_names[1]

# ---------------------------------------------------------------------------
# UI
# ---------------------------------------------------------------------------

ui <- shiny::fluidPage(
  shiny::tags$head(
    shiny::tags$title("GSP Refinement Sweeps"),
    shiny::tags$style(shiny::HTML("
      html, body { height: 100%; margin: 0; padding: 0; background: #f5f5f5; }
      #ctrl-panel {
        padding: 10px 16px 6px 16px; background: white;
        box-shadow: 0 2px 6px rgba(0,0,0,.15);
        position: relative; z-index: 500;
      }
      #map { height: calc(100vh - 108px); }
      .info-box {
        font-size: 13px; line-height: 1.5; padding: 6px 10px;
        background: white; border-radius: 4px;
        box-shadow: 0 1px 4px rgba(0,0,0,.2);
      }
      label { font-weight: 600 !important; }
    "))
  ),
  shiny::div(
    id = "ctrl-panel",
    shiny::fluidRow(
      shiny::column(5,
        shiny::sliderInput("phase_idx", label = "Phase",
          min = 1L, max = n_passes, value = 1L, step = 1L, width = "100%",
          ticks = TRUE,
          animate = shiny::animationOptions(interval = 1400, loop = FALSE))
      ),
      shiny::column(3,
        shiny::br(),
        shiny::checkboxInput("show_stations",   "Station lines",     value = TRUE),
        shiny::checkboxInput("show_seg_labels", "Segment numbers",   value = FALSE),
        shiny::checkboxInput("show_init",       "Overlay baseline",  value = FALSE)
      ),
      shiny::column(4, shiny::br(), shiny::htmlOutput("sweep_stats"))
    )
  ),
  leaflet::leafletOutput("map", width = "100%", height = "calc(100vh - 108px)")
)

# ---------------------------------------------------------------------------
# Server
# ---------------------------------------------------------------------------

server <- function(input, output, session) {

  output$sweep_stats <- shiny::renderUI({
    idx   <- input$phase_idx
    name  <- all_names[idx]
    dist  <- all_distances[[name]]
    label <- sweep_labels[idx]

    if (is.null(dist))
      return(shiny::div(class = "info-box", shiny::HTML(sprintf("<b>%s</b>", label))))

    n_segs    <- length(all_solutions[[name]]$tour_segments_location_ids)
    seg_trans <- dist$segment_transit
    best_seg  <- which.min(seg_trans)
    worst_seg <- which.max(seg_trans)

    delta_html <- if (name %in% c("construction", "segment")) "" else {
      imp     <- init_transit - dist$grand_transit
      imp_pct <- if (init_transit == 0) 0 else imp / init_transit * 100
      sprintf(" &nbsp;|&nbsp; \u0394 init <b>%+.0f nm</b> (%.1f%%)", -imp, imp_pct)
    }

    shiny::div(class = "info-box", shiny::HTML(sprintf(
      "<b>%s</b> &nbsp;&mdash;&nbsp; %d segments<br>
       Transit <b>%.0f nm</b>%s<br>
       Best #%d (%.0f nm) &nbsp;|&nbsp; Worst #%d (%.0f nm)",
      label, n_segs, dist$grand_transit, delta_html,
      best_seg, seg_trans[best_seg], worst_seg, seg_trans[worst_seg]
    )))
  })

  output$map <- leaflet::renderLeaflet({
    leaflet::leaflet() |>
      leaflet::addProviderTiles(leaflet::providers$CartoDB.Positron,
        options = leaflet::providerTileOptions(opacity = 0.85)) |>
      leaflet::fitBounds(lng1 = bounds$lon_range[1], lat1 = bounds$lat_range[1],
                         lng2 = bounds$lon_range[2], lat2 = bounds$lat_range[2]) |>
      leaflet::addScaleBar(position = "bottomleft") |>
      leaflet::addLayersControl(
        overlayGroups = c("Route", "Stations", "Segment labels", "Baseline"),
        options = leaflet::layersControlOptions(collapsed = FALSE)) |>
      leaflet::hideGroup("Segment labels") |>
      leaflet::hideGroup("Baseline")
  })

  # Slider → redraw.  All data is precomputed; each group = 1 proxy call.
  shiny::observe({
    idx   <- input$phase_idx
    name  <- all_names[idx]
    d     <- all_draw[[name]]
    proxy <- leaflet::leafletProxy("map", session)

    proxy |>
      leaflet::clearGroup("Route") |>
      leaflet::clearGroup("Stations") |>
      leaflet::clearGroup("Segment labels") |>
      leaflet::clearGroup("Ports") |>
      leaflet::clearGroup("Baseline")

    # ---- Route (all segments, 1 call) --------------------------------------
    proxy <- proxy |>
      leaflet::addPolylines(
        lat     = d$route_lats,
        lng     = d$route_lons,
        color   = d$route_colors,
        weight  = d$route_weights,
        opacity = d$route_opacities,
        label   = d$route_labels,
        group   = "Route",
        highlightOptions = leaflet::highlightOptions(weight = 4, bringToFront = TRUE)
      )

    # ---- Station lines (all segments, 1 call) ------------------------------
    if (isTRUE(input$show_stations) && length(d$sl_lats) > 0) {
      proxy <- proxy |>
        leaflet::addPolylines(
          lat     = d$sl_lats,
          lng     = d$sl_lons,
          color   = d$sl_colors,
          weight  = 2,
          opacity = 0.50,
          group   = "Stations"
        )
    }

    # ---- Segment number labels (1 circle marker per segment) ---------------
    if (isTRUE(input$show_seg_labels) && nrow(d$seg_mids) > 0) {
      proxy <- proxy |>
        leaflet::addCircleMarkers(
          data        = d$seg_mids,
          lng         = ~lon,
          lat         = ~lat,
          radius      = 11,
          color       = ~color,
          weight      = 2,
          fillColor   = "white",
          fillOpacity = 0.90,
          label       = ~as.character(segment),
          labelOptions = leaflet::labelOptions(
            noHide = TRUE, direction = "center", textOnly = TRUE,
            style  = list("font-weight" = "bold", "font-size" = "11px")
          ),
          group = "Segment labels"
        )
    }

    # ---- Ports (always visible) --------------------------------------------
    if (nrow(d$ports) > 0) {
      proxy <- proxy |>
        leaflet::addCircleMarkers(
          data = d$ports, lng = ~lon, lat = ~lat,
          radius = 7, color = "#333", weight = 2,
          fillColor = "white", fillOpacity = 0.95,
          group = "Ports", label = "Port"
        )
    }

    # ---- Baseline overlay (Segmentation / Improvement 0) -------------------
    if (isTRUE(input$show_init) && name != init_ref_name) {
      bd <- all_draw[[init_ref_name]]
      proxy <- proxy |>
        leaflet::addPolylines(
          lat       = bd$route_lats,
          lng       = bd$route_lons,
          color     = "#888888",
          weight    = 1.5,
          opacity   = 0.40,
          dashArray = "6,5",
          group     = "Baseline"
        )
    }
  })

  shiny::observeEvent(input$show_stations, {
    p <- leaflet::leafletProxy("map", session)
    if (isTRUE(input$show_stations)) p |> leaflet::showGroup("Stations")
    else                             p |> leaflet::hideGroup("Stations")
  })
  shiny::observeEvent(input$show_seg_labels, {
    p <- leaflet::leafletProxy("map", session)
    if (isTRUE(input$show_seg_labels)) p |> leaflet::showGroup("Segment labels")
    else                               p |> leaflet::hideGroup("Segment labels")
  })
  shiny::observeEvent(input$show_init, {
    p <- leaflet::leafletProxy("map", session)
    if (isTRUE(input$show_init)) p |> leaflet::showGroup("Baseline")
    else                         p |> leaflet::hideGroup("Baseline")
  })
}

shiny::shinyApp(ui, server)

