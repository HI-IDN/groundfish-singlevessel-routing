#!/usr/bin/env Rscript

suppressPackageStartupMessages({
  library(shiny)
  library(leaflet)
  library(jsonlite)
  library(DBI)
  library(RSQLite)
  library(dplyr)
  library(htmltools)
  library(tibble)
})

script_file_arg <- grep("^--file=", commandArgs(trailingOnly = FALSE), value = TRUE)
script_dir <- if (length(script_file_arg) > 0) {
  dirname(normalizePath(sub("^--file=", "", script_file_arg[1])))
} else {
  "R"
}
source(file.path(script_dir, "plot_utils.R"))

args <- commandArgs(trailingOnly = TRUE)
json_path <- if (length(args) >= 1) args[[1]] else "sol/nn/sweep_300.json"
db_path <- if (length(args) >= 2) args[[2]] else "dat/gsp.db"

if (!file.exists(json_path)) {
  stop(sprintf("Sweep JSON not found: %s", json_path), call. = FALSE)
}
if (!file.exists(db_path)) {
  stop(sprintf("Database not found: %s", db_path), call. = FALSE)
}

read_json <- function(path) {
  fromJSON(path, simplifyVector = FALSE)
}

ensure_segment_list <- function(x) {
  if (is.null(x)) return(list())
  if (is.list(x) && !is.data.frame(x)) return(x)
  if (is.data.frame(x)) return(split(x, seq_len(nrow(x))))
  list(x)
}

ordered_pass_names <- function(sweep) {
  keys <- names(sweep$solution %||% list())
  keys <- keys[grepl("^(init|pass[0-9]+)$", keys)]
  ord <- integer(length(keys))
  ord[keys == "init"] <- 0L
  ord[keys != "init"] <- as.integer(sub("^pass", "", keys[keys != "init"]))
  keys[order(ord)]
}

`%||%` <- function(x, y) {
  if (is.null(x) || length(x) == 0) y else x
}

read_db_table_quiet <- function(db_path, sql) {
  con <- DBI::dbConnect(RSQLite::SQLite(), dbname = db_path)
  on.exit(DBI::dbDisconnect(con), add = TRUE)
  tibble(DBI::dbGetQuery(con, sql))
}

build_route_df <- function(solution_block, locations_tbl) {
  segs <- ensure_segment_list(solution_block$tour_segments_location_ids)
  rows <- list()
  idx <- 1L
  for (seg_idx in seq_along(segs)) {
    seg <- as.integer(unlist(segs[[seg_idx]], use.names = FALSE))
    if (length(seg) == 0) next
    loc_rows <- locations_tbl %>%
      filter(id %in% seg) %>%
      select(id, lat, lon)
    if (nrow(loc_rows) == 0) next
    ordered <- tibble(location_id = seg, order = seq_along(seg)) %>%
      left_join(loc_rows, by = c("location_id" = "id")) %>%
      filter(!is.na(lat), !is.na(lon)) %>%
      mutate(segment = seg_idx, point_order = seq_len(n()))
    rows[[idx]] <- ordered
    idx <- idx + 1L
  }
  bind_rows(rows)
}

station_position_table <- function(db_path) {
  read_db_table_quiet(
    db_path,
    paste(
      "SELECT s.id AS station_id,",
      "ls.lat AS start_lat, ls.lon AS start_lon,",
      "le.lat AS end_lat, le.lon AS end_lon",
      "FROM stations s",
      "JOIN locations ls ON ls.id = s.start_location_id",
      "JOIN locations le ON le.id = s.end_location_id"
    )
  ) %>%
    mutate(
      lat = (start_lat + end_lat) / 2.0,
      lon = (start_lon + end_lon) / 2.0
    )
}

mutation_points <- function(solution_block, station_tbl) {
  muts <- ensure_segment_list(solution_block$tour_segments_station_mutation_ids)
  if (length(muts) == 0) return(tibble())
  vals <- unlist(muts, recursive = TRUE, use.names = FALSE)
  if (length(vals) == 0) return(tibble())
  tibble(raw_station_id = as.integer(vals)) %>%
    mutate(
      station_id = abs(raw_station_id),
      direction = ifelse(raw_station_id < 0, "left", "entered")
    ) %>%
    left_join(station_tbl, by = "station_id") %>%
    filter(!is.na(lat), !is.na(lon))
}

transition_rows <- function(sweep) {
  pass_names <- ordered_pass_names(sweep)
  final_name <- sweep$summary$final %||% tail(pass_names, 1)
  rows <- list(
    tibble(
      key = "init_to_final",
      label = sprintf("init -> %s", final_name),
      prev = "init",
      curr = final_name
    )
  )
  if (length(pass_names) > 1) {
    for (i in 2:length(pass_names)) {
      rows[[length(rows) + 1L]] <- tibble(
        key = sprintf("%s_to_%s", pass_names[[i - 1]], pass_names[[i]]),
        label = sprintf("%s -> %s", pass_names[[i - 1]], pass_names[[i]]),
        prev = pass_names[[i - 1]],
        curr = pass_names[[i]]
      )
    }
  }
  bind_rows(rows)
}

segment_color <- colorFactor(hcl.colors(32, "Dynamic"), domain = 1:32)

sweep <- read_json(json_path)
coastline <- read_db_table_quiet(db_path, "SELECT lat, lon FROM coastline")
locations_tbl <- read_db_table_quiet(db_path, "SELECT id, lat, lon FROM locations")
station_tbl <- station_position_table(db_path)
transitions <- transition_rows(sweep)

ui <- fluidPage(
  titlePanel(sprintf("Sweep Viewer: %s", basename(json_path))),
  fluidRow(
    column(
      width = 3,
      selectInput("transition", "View", choices = setNames(transitions$key, transitions$label)),
      fluidRow(
        column(6, actionButton("prev_btn", "Previous")),
        column(6, actionButton("next_btn", "Next"))
      ),
      tags$hr(),
      uiOutput("summary_ui"),
      tags$hr(),
      tableOutput("gain_table"),
      tags$hr(),
      tableOutput("mutation_table")
    ),
    column(
      width = 9,
      leafletOutput("map", height = "86vh")
    )
  )
)

server <- function(input, output, session) {
  observeEvent(input$prev_btn, {
    idx <- match(input$transition, transitions$key)
    idx <- max(1L, idx - 1L)
    updateSelectInput(session, "transition", selected = transitions$key[[idx]])
  })

  observeEvent(input$next_btn, {
    idx <- match(input$transition, transitions$key)
    idx <- min(nrow(transitions), idx + 1L)
    updateSelectInput(session, "transition", selected = transitions$key[[idx]])
  })

  selected_transition <- reactive({
    transitions %>% filter(key == input$transition) %>% slice(1)
  })

  selected_blocks <- reactive({
    tr <- selected_transition()
    list(
      prev_name = tr$prev[[1]],
      curr_name = tr$curr[[1]],
      prev = sweep$solution[[tr$prev[[1]]]],
      curr = sweep$solution[[tr$curr[[1]]]]
    )
  })

  output$summary_ui <- renderUI({
    blocks <- selected_blocks()
    prev_dist <- as.numeric(blocks$prev$total_distance_nm %||% NA_real_)
    curr_dist <- as.numeric(blocks$curr$total_distance_nm %||% NA_real_)
    delta <- curr_dist - prev_dist
    tags$div(
      tags$strong(sprintf("%s -> %s", blocks$prev_name, blocks$curr_name)),
      tags$p(sprintf("Distance: %.2f -> %.2f nm", prev_dist, curr_dist)),
      tags$p(sprintf("Delta: %.2f nm", delta)),
      tags$p(sprintf("Changed: %s", blocks$curr$changed %||% NA)),
      tags$p(sprintf("Boundary changes: %s", blocks$curr$boundary_changes %||% NA)),
      tags$p(sprintf("Accepted/total solves: %s / %s",
                     blocks$curr$accepted_capacity_solves %||% NA,
                     blocks$curr$total_capacity_solves %||% NA)),
      tags$p(sprintf("MIP calls: %s", blocks$curr$capacity_mip_solves %||% NA))
    )
  })

  output$gain_table <- renderTable({
    curr <- selected_blocks()$curr
    gains <- as.numeric(unlist(curr$boundary_improvement_gain_nm %||% list(), use.names = FALSE))
    ports <- as.integer(unlist(curr$boundary_port_ids %||% list(), use.names = FALSE))
    if (length(gains) == 0) return(NULL)
    tibble(
      boundary = seq_along(gains),
      port_id = ports,
      gain_nm = round(gains, 3)
    ) %>% filter(gain_nm > 0)
  })

  output$mutation_table <- renderTable({
    curr <- selected_blocks()$curr
    muts <- mutation_points(curr, station_tbl)
    if (nrow(muts) == 0) return(NULL)
    muts %>%
      distinct(station_id, direction) %>%
      arrange(direction, station_id)
  })

  output$map <- renderLeaflet({
    blocks <- selected_blocks()
    prev_route <- build_route_df(blocks$prev, locations_tbl)
    curr_route <- build_route_df(blocks$curr, locations_tbl)
    mut_points <- mutation_points(blocks$curr, station_tbl)
    boat_loc <- sweep$metadata$boat_docked_location

    m <- leaflet(options = leafletOptions(preferCanvas = TRUE)) %>%
      addProviderTiles("CartoDB.Positron") %>%
      addPolylines(
        data = coastline,
        lng = ~lon,
        lat = ~lat,
        color = "#444444",
        weight = 1,
        opacity = 0.7,
        group = "Coastline"
      )

    if (nrow(prev_route) > 0) {
      for (seg_idx in sort(unique(prev_route$segment))) {
        seg_df <- prev_route %>% filter(segment == seg_idx)
        m <- m %>%
          addPolylines(
            data = seg_df,
            lng = ~lon,
            lat = ~lat,
            color = "#7a7a7a",
            weight = 3,
            opacity = 0.55,
            dashArray = "6,6",
            label = sprintf("Previous seg %d", seg_idx),
            group = "Previous"
          )
      }
    }

    if (nrow(curr_route) > 0) {
      for (seg_idx in sort(unique(curr_route$segment))) {
        seg_df <- curr_route %>% filter(segment == seg_idx)
        m <- m %>%
          addPolylines(
            data = seg_df,
            lng = ~lon,
            lat = ~lat,
            color = segment_color(seg_idx),
            weight = 4,
            opacity = 0.9,
            label = sprintf("Current seg %d", seg_idx),
            group = "Current"
          )
      }
    }

    if (nrow(mut_points) > 0) {
      m <- m %>%
        addCircleMarkers(
          data = mut_points,
          lng = ~lon,
          lat = ~lat,
          radius = 5,
          stroke = TRUE,
          weight = 1,
          color = ~ifelse(direction == "entered", "#1b9e77", "#d95f02"),
          fillOpacity = 0.8,
          popup = ~sprintf("Station %d (%s)", station_id, direction),
          group = "Mutations"
        )
    }

    m <- m %>%
      addCircleMarkers(
        lng = boat_loc$lon,
        lat = boat_loc$lat,
        radius = 6,
        color = "#000000",
        fillColor = "#000000",
        fillOpacity = 1,
        popup = "Boat dock",
        group = "Boat"
      ) %>%
      addLayersControl(
        overlayGroups = c("Previous", "Current", "Mutations", "Boat"),
        options = layersControlOptions(collapsed = FALSE)
      )

    if (nrow(curr_route) > 0) {
      m <- fitBounds(
        m,
        lng1 = min(curr_route$lon, na.rm = TRUE),
        lat1 = min(curr_route$lat, na.rm = TRUE),
        lng2 = max(curr_route$lon, na.rm = TRUE),
        lat2 = max(curr_route$lat, na.rm = TRUE)
      )
    }

    m
  })
}

runApp(shinyApp(ui, server), launch.browser = TRUE)
