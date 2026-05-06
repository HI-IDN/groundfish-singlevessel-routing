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
# Data loading
# ---------------------------------------------------------------------------

args            <- commandArgs(trailingOnly = TRUE)
refinement_file <- if (length(args) >= 1) args[1] else "sol/noport/refinement_180.json"

cat(sprintf("Loading: %s\n", refinement_file))
rf       <- load_refinement_json(refinement_file)
map_data <- load_map_data()

n_passes         <- length(rf$pass_names)
boat_location_id <- as.integer(rf$doc$metadata$boat_location_id)

# Segment colour palette (viridis turbo, sized to max segments across all sweeps)
n_segments <- max(vapply(rf$solutions, function(s) {
  length(s$tour_segments_location_ids)
}, integer(1)))
seg_colors <- viridisLite::viridis(n_segments, option = "turbo")

# Sweep label choices for the slider
sweep_labels <- vapply(rf$pass_names, function(n) {
  sw <- pass_sweep(n)
  if (sw == 0L) "Initial (0)" else sprintf("Sweep %d", sw)
}, character(1))

# Bounding box for initial map view
all_routes <- dplyr::bind_rows(lapply(rf$solutions, build_route_path, locations = map_data$locations))
bounds     <- compute_map_bounds(all_routes)

# ---------------------------------------------------------------------------
# Helper: build leaflet data for a single sweep
# ---------------------------------------------------------------------------

leaflet_route_data <- function(solution) {
  route         <- build_regular_route_path(solution, map_data$locations, boat_location_id)
  station_lines <- build_station_line_segments(solution$tour_segments_station_ids,
                                               map_data$station_endpoints)
  disp          <- route_display_points(route)
  list(route = route, station_lines = station_lines, ports = disp$ports, waypoints = disp$waypoints)
}

# Pre-compute data for every sweep so UI feels instant
all_leaflet_data <- lapply(rf$solutions, leaflet_route_data)
names(all_leaflet_data) <- rf$pass_names

# ---------------------------------------------------------------------------
# UI
# ---------------------------------------------------------------------------

ui <- shiny::fluidPage(
  shiny::tags$head(
    shiny::tags$title("GSP Refinement Sweeps"),
    shiny::tags$style(shiny::HTML("
      html, body { height: 100%; margin: 0; padding: 0; background: #f5f5f5; }
      #ctrl-panel {
        padding: 10px 16px 6px 16px;
        background: white;
        box-shadow: 0 2px 6px rgba(0,0,0,.15);
        position: relative; z-index: 500;
      }
      #map { height: calc(100vh - 108px); }
      .info-box {
        font-size: 13px; line-height: 1.5;
        padding: 6px 10px;
        background: white;
        border-radius: 4px;
        box-shadow: 0 1px 4px rgba(0,0,0,.2);
      }
      label { font-weight: 600 !important; }
    "))
  ),

  shiny::div(
    id = "ctrl-panel",
    shiny::fluidRow(
      # Sweep slider + animate button
      shiny::column(5,
        shiny::sliderInput(
          "sweep_idx",
          label   = "Sweep",
          min     = 1L,
          max     = n_passes,
          value   = 1L,
          step    = 1L,
          width   = "100%",
          ticks   = TRUE,
          animate = shiny::animationOptions(interval = 1600, loop = FALSE)
        )
      ),
      # Checkboxes
      shiny::column(3,
        shiny::br(),
        shiny::checkboxInput("show_stations", "Show station lines", value = TRUE),
        shiny::checkboxInput("show_init",     "Overlay initial route", value = FALSE)
      ),
      # Stats panel
      shiny::column(4,
        shiny::br(),
        shiny::htmlOutput("sweep_stats")
      )
    )
  ),

  leaflet::leafletOutput("map", width = "100%", height = "calc(100vh - 108px)")
)

# ---------------------------------------------------------------------------
# Server
# ---------------------------------------------------------------------------

server <- function(input, output, session) {

  # ---- Sweep stats panel ---------------------------------------------------

  output$sweep_stats <- shiny::renderUI({
    idx  <- input$sweep_idx
    name <- rf$pass_names[idx]
    dist <- rf$distances[[name]]
    imp  <- rf$init_transit - dist$grand_transit
    imp_pct <- if (rf$init_transit == 0) 0 else imp / rf$init_transit * 100

    n_segs    <- length(rf$solutions[[name]]$tour_segments_location_ids)
    seg_trans <- dist$segment_transit
    best_seg  <- which.min(seg_trans)
    worst_seg <- which.max(seg_trans)

    shiny::div(
      class = "info-box",
      shiny::HTML(sprintf(
        "<b>%s</b> &nbsp;&mdash;&nbsp; %d segments<br>
         Transit <b>%.0f nm</b> &nbsp;|&nbsp; Δ init <b>%+.0f nm</b> (%.1f%%)<br>
         Best seg #%d (%.0f nm) &nbsp;|&nbsp; Worst seg #%d (%.0f nm)",
        sweep_labels[idx], n_segs,
        dist$grand_transit, -imp, imp_pct,
        best_seg,  seg_trans[best_seg],
        worst_seg, seg_trans[worst_seg]
      ))
    )
  })

  # ---- Base map (rendered once) -------------------------------------------

  output$map <- leaflet::renderLeaflet({
    leaflet::leaflet() |>
      leaflet::addProviderTiles(
        leaflet::providers$CartoDB.Positron,
        options = leaflet::providerTileOptions(opacity = 0.85)
      ) |>
      leaflet::fitBounds(
        lng1 = bounds$lon_range[1], lat1 = bounds$lat_range[1],
        lng2 = bounds$lon_range[2], lat2 = bounds$lat_range[2]
      ) |>
      leaflet::addScaleBar(position = "bottomleft") |>
      leaflet::addLayersControl(
        overlayGroups = c("Route", "Stations", "Ports / waypoints", "Initial route"),
        options = leaflet::layersControlOptions(collapsed = FALSE)
      ) |>
      leaflet::hideGroup("Initial route")
  })

  # ---- Reactive route drawing ----------------------------------------------

  shiny::observe({
    idx      <- input$sweep_idx
    name     <- rf$pass_names[idx]
    data     <- all_leaflet_data[[name]]
    proxy    <- leaflet::leafletProxy("map", session)

    # Clear previous layers
    proxy |>
      leaflet::clearGroup("Route") |>
      leaflet::clearGroup("Stations") |>
      leaflet::clearGroup("Ports / waypoints") |>
      leaflet::clearGroup("Initial route")

    # --- Draw route segments (one addPolylines call per segment) ----------
    for (seg in sort(unique(data$route$segment))) {
      seg_df <- data$route |> dplyr::filter(segment == seg)
      col    <- seg_colors[min(seg, n_segments)]
      proxy  <- proxy |>
        leaflet::addPolylines(
          data    = seg_df,
          lng     = ~lon,
          lat     = ~lat,
          color   = col,
          weight  = 3,
          opacity = 0.88,
          group   = "Route",
          label   = sprintf("Segment %d", seg),
          highlightOptions = leaflet::highlightOptions(weight = 5, bringToFront = TRUE)
        )
    }

    # --- Station lines (per-segment batch using NA-separated vectors) ------
    if (isTRUE(input$show_stations) && nrow(data$station_lines) > 0) {
      for (seg in sort(unique(data$station_lines$segment))) {
        sl  <- data$station_lines |> dplyr::filter(segment == seg)
        col <- seg_colors[min(seg, n_segments)]
        # Interleave start/end/NA so addPolylines draws many 2-point lines
        lng_vec <- c(rbind(sl$lon, sl$lon_end, NA_real_))
        lat_vec <- c(rbind(sl$lat, sl$lat_end, NA_real_))
        proxy   <- proxy |>
          leaflet::addPolylines(
            lng     = lng_vec,
            lat     = lat_vec,
            color   = col,
            weight  = 2.5,
            opacity = 0.70,
            group   = "Stations"
          )
      }
    }

    # --- Port / waypoint markers ------------------------------------------
    if (nrow(data$ports) > 0) {
      proxy <- proxy |>
        leaflet::addCircleMarkers(
          data        = data$ports,
          lng         = ~lon,
          lat         = ~lat,
          radius      = 7,
          color       = "#333",
          weight      = 2,
          fillColor   = "white",
          fillOpacity = 0.95,
          group       = "Ports / waypoints",
          label       = "Port"
        )
    }
    if (nrow(data$waypoints) > 0) {
      proxy <- proxy |>
        leaflet::addCircleMarkers(
          data        = data$waypoints,
          lng         = ~lon,
          lat         = ~lat,
          radius      = 3,
          color       = "#555",
          weight      = 1.5,
          fillColor   = "#ccc",
          fillOpacity = 0.80,
          group       = "Ports / waypoints",
          label       = "Waypoint"
        )
    }

    # --- Overlay initial route (dashed grey) if toggled -------------------
    if (isTRUE(input$show_init) && idx > 1L) {
      init_data <- all_leaflet_data[[rf$pass_names[1]]]
      for (seg in sort(unique(init_data$route$segment))) {
        seg_df <- init_data$route |> dplyr::filter(segment == seg)
        proxy  <- proxy |>
          leaflet::addPolylines(
            data      = seg_df,
            lng       = ~lon,
            lat       = ~lat,
            color     = "#888888",
            weight    = 1.8,
            opacity   = 0.38,
            dashArray = "6,5",
            group     = "Initial route",
            label     = sprintf("Init seg %d", seg)
          )
      }
    }
  })

  # Sync show_stations checkbox with layer control
  shiny::observeEvent(input$show_stations, {
    proxy <- leaflet::leafletProxy("map", session)
    if (isTRUE(input$show_stations)) {
      proxy |> leaflet::showGroup("Stations")
    } else {
      proxy |> leaflet::hideGroup("Stations")
    }
  })

  shiny::observeEvent(input$show_init, {
    proxy <- leaflet::leafletProxy("map", session)
    if (isTRUE(input$show_init)) {
      proxy |> leaflet::showGroup("Initial route")
    } else {
      proxy |> leaflet::hideGroup("Initial route")
    }
  })
}

# ---------------------------------------------------------------------------
# Launch
# ---------------------------------------------------------------------------

shiny::shinyApp(ui, server)

