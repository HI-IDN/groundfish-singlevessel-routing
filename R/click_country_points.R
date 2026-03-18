# click_country_points.R
#
# Single-file R script:
# - reads coastline / waypoints / ports / boats from SQLite
# - opens an interactive Shiny + Plotly map
# - clicking on the map appends:
#       WAYP <EASTING> <NORTHING>
#   into a text box below the plot
#
# Adjust the SETTINGS section if needed.

# ----------------------------
# SETTINGS
# ----------------------------
db_path <- "dat/gsp_data.db"
app_height <- "850px"
grid_nx <- 140           # clickable grid density in x
grid_ny <- 140           # clickable grid density in y

# ----------------------------
# PACKAGES
# ----------------------------
required_packages <- c(
  "ggplot2", "dplyr", "purrr", "DBI", "RSQLite",
  "plotly", "sf", "shiny"
)

missing_packages <- required_packages[
  !vapply(required_packages, requireNamespace, logical(1), quietly = TRUE)
]

if (length(missing_packages) > 0) {
  stop(
    sprintf(
      "Missing packages: %s\nInstall them first, e.g. install.packages(c(%s))",
      paste(missing_packages, collapse = ", "),
      paste(sprintf('"%s"', missing_packages), collapse = ", ")
    ),
    call. = FALSE
  )
}

invisible(lapply(required_packages, library, character.only = TRUE))

# ----------------------------
# HELPERS
# ----------------------------
read_db_table <- function(db_path, query) {
  con <- DBI::dbConnect(RSQLite::SQLite(), db_path)
  on.exit(DBI::dbDisconnect(con), add = TRUE)
  DBI::dbGetQuery(con, query)
}

fmt_hover <- function(title, fields) {
  paste(c(sprintf("<b>%s</b>", title), fields), collapse = "<br>")
}

close_ring <- function(df) {
  if (nrow(df) == 0) return(df)
  dplyr::bind_rows(df, df[1, , drop = FALSE])
}

make_coastline_polygon <- function(coastline_df) {
  ring <- close_ring(coastline_df)
  coords <- as.matrix(ring[, c("lon", "lat")])
  sf::st_sfc(sf::st_polygon(list(coords)), crs = 4326)
}

decimal_deg_to_degmin_int <- function(deg) {
  abs_deg <- abs(deg)
  whole_deg <- floor(abs_deg)
  minutes <- (abs_deg - whole_deg) * 60.0
  as.integer(round((whole_deg * 100.0 + minutes) * 100.0))
}

decimal_lon_to_degmin_storage <- function(lon_deg) {
  decimal_deg_to_degmin_int(abs(lon_deg))
}

lonlat_to_grid <- function(lon, lat) {
  list(
    easting = decimal_deg_to_degmin_int(lat),
    northing = decimal_lon_to_degmin_storage(lon)
  )
}

make_click_grid <- function(lon_range, lat_range, nx = grid_nx, ny = grid_ny) {
  grid <- expand.grid(
    lon = seq(lon_range[1], lon_range[2], length.out = nx),
    lat = seq(lat_range[1], lat_range[2], length.out = ny)
  )
  grid$easting <- vapply(grid$lat, decimal_deg_to_degmin_int, integer(1))
  grid$northing <- vapply(grid$lon, decimal_lon_to_degmin_storage, integer(1))
  grid$wayp <- sprintf("WAYP %d %d", grid$easting, grid$northing)
  grid
}

filter_click_grid_to_water <- function(grid_df, coastline_polygon) {
  pts <- sf::st_as_sf(grid_df, coords = c("lon", "lat"), crs = 4326)
  on_land <- as.vector(sf::st_within(pts, coastline_polygon, sparse = FALSE))
  dplyr::filter(grid_df, !on_land)
}

# Fixed aspect ratio based on latitude
coord_fixed_for_lat <- function(lat_values, fallback_lat = 65.0) {
  mean_lat <- mean(lat_values, na.rm = TRUE)
  if (!is.finite(mean_lat)) mean_lat <- fallback_lat
  ggplot2::coord_fixed(ratio = 1 / cos(mean_lat * pi / 180))
}

# Simple degree-format axes
apply_degree_axes <- function(p) {
  p +
    ggplot2::scale_x_continuous(labels = function(x) sprintf("%.1f°", x)) +
    ggplot2::scale_y_continuous(labels = function(y) sprintf("%.1f°", y))
}

# Simple common theme
gsp_common_theme <- function(legend_position = "right", legend_direction = "vertical") {
  ggplot2::theme_minimal(base_size = 12) +
    ggplot2::theme(
      panel.grid.minor = ggplot2::element_blank(),
      legend.position = legend_position,
      legend.direction = legend_direction,
      plot.title = ggplot2::element_text(face = "bold"),
      axis.title = ggplot2::element_blank()
    )
}

# Base coastline plot
base_coastline_plot <- function(coastline) {
  ggplot2::ggplot() +
    ggplot2::geom_polygon(
      data = close_ring(coastline),
      ggplot2::aes(x = lon, y = lat),
      fill = "white",
      color = NA,
      inherit.aes = FALSE
    ) +
    ggplot2::geom_path(
      data = coastline,
      ggplot2::aes(x = lon, y = lat),
      linewidth = 0.5,
      color = "grey35",
      alpha = 1.0,
      inherit.aes = FALSE
    )
}

# ----------------------------
# CHECK INPUT
# ----------------------------
if (!file.exists(db_path)) {
  stop(sprintf("Database not found: %s", db_path), call. = FALSE)
}

# ----------------------------
# READ DATA
# ----------------------------
coastline <- read_db_table(db_path, "SELECT lat, lon FROM coastline ORDER BY id")
if (nrow(coastline) == 0) stop("No coastline rows found in database.", call. = FALSE)

waypoints <- read_db_table(
  db_path,
  "SELECT w.id AS waypoint_order, w.granularity,
          l.id AS location_id, l.easting, l.northing, l.lat, l.lon
   FROM waypoints w
   JOIN locations l ON l.id = w.location_id
   ORDER BY w.granularity, w.id"
)
if (nrow(waypoints) == 0) stop("No waypoint rows found in database.", call. = FALSE)

ports <- read_db_table(
  db_path,
  "SELECT p.id AS port_id, p.name,
          l.id AS location_id, l.easting, l.northing, l.lat, l.lon
   FROM ports p
   JOIN locations l ON l.id = p.location_id
   ORDER BY p.id"
)

boats <- read_db_table(
  db_path,
  "SELECT b.id AS boat_id,
          COALESCE(b.name, '') AS name,
          l.id AS location_id,
          l.easting AS easting,
          l.northing AS northing,
          l.lat AS lat,
          l.lon AS lon
   FROM boats b
   JOIN locations l ON l.id = b.location_id
   ORDER BY b.id"
)

# ----------------------------
# PREP DATA
# ----------------------------
granularity_levels <- c("0", "1", "2")
granularity_labels <- c("small", "medium", "fine")
granularity_colours <- c(
  "small"  = "#E69F00",
  "medium" = "#0072B2",
  "fine"   = "#009E73"
)

waypoints <- waypoints |>
  dplyr::mutate(
    gran_fct = factor(
      as.character(granularity),
      levels = granularity_levels,
      labels = granularity_labels
    ),
    hover_text = purrr::pmap_chr(
      list(waypoint_order, granularity, gran_fct, location_id, easting, northing, lat, lon),
      \(waypoint_order, granularity, gran_fct, location_id, easting, northing, lat, lon) {
        fmt_hover(
          sprintf("Waypoint %s", waypoint_order),
          c(
            sprintf("granularity_code: %s", granularity),
            sprintf("granularity: %s", gran_fct),
            sprintf("location_id: %s", location_id),
            sprintf("easting: %s", easting),
            sprintf("northing: %s", northing),
            sprintf("lat: %.6f", lat),
            sprintf("lon: %.6f", lon)
          )
        )
      }
    )
  )

ports <- ports |>
  dplyr::mutate(
    hover_text = purrr::pmap_chr(
      list(port_id, name, location_id, easting, northing, lat, lon),
      \(port_id, name, location_id, easting, northing, lat, lon) {
        fmt_hover(
          sprintf("Port %s", port_id),
          c(
            sprintf("name: %s", name),
            sprintf("location_id: %s", location_id),
            sprintf("easting: %s", easting),
            sprintf("northing: %s", northing),
            sprintf("lat: %.6f", lat),
            sprintf("lon: %.6f", lon)
          )
        )
      }
    )
  )

boat_points <- boats |>
  dplyr::mutate(
    hover_text = purrr::pmap_chr(
      list(boat_id, name, location_id, easting, northing, lat, lon),
      \(boat_id, name, location_id, easting, northing, lat, lon) {
        fmt_hover(
          sprintf("Boat %s docked", boat_id),
          c(
            sprintf("name: %s", name),
            sprintf("location_id: %s", location_id),
            sprintf("easting: %s", easting),
            sprintf("northing: %s", northing),
            sprintf("lat: %.6f", lat),
            sprintf("lon: %.6f", lon)
          )
        )
      }
    )
  )

waypoints_ring <- waypoints |>
  dplyr::group_by(gran_fct) |>
  dplyr::group_modify(~ close_ring(.x)) |>
  dplyr::ungroup()

counts_by_gran <- waypoints |>
  dplyr::count(gran_fct, name = "n_pts") |>
  dplyr::mutate(label = sprintf("%s: %d pts", gran_fct, n_pts))

subtitle_text <- paste(
  sprintf("Coastline: %d pts", nrow(coastline)),
  sprintf("Ports: %d", nrow(ports)),
  sprintf("Boats: %d", nrow(boats)),
  sprintf("Waypoints - %s", paste(counts_by_gran$label, collapse = " | ")),
  sep = " | "
)

# ----------------------------
# PLOT DATA
# ----------------------------
lon_range <- range(coastline$lon, na.rm = TRUE)
lat_range <- range(coastline$lat, na.rm = TRUE)
coastline_polygon <- make_coastline_polygon(coastline)
click_grid <- make_click_grid(lon_range, lat_range)
click_grid <- filter_click_grid_to_water(click_grid, coastline_polygon)

p <- base_coastline_plot(coastline) +
  ggplot2::geom_point(
    data = click_grid,
    ggplot2::aes(x = lon, y = lat),
    size = 0.35,
    alpha = 0.35,
    color = "#808080",
    inherit.aes = FALSE
  ) +
  ggplot2::geom_path(
    data = waypoints_ring,
    ggplot2::aes(x = lon, y = lat, color = gran_fct, group = gran_fct),
    linewidth = 0.7,
    alpha = 0.9,
    inherit.aes = FALSE
  ) +
  ggplot2::geom_point(
    data = waypoints,
    ggplot2::aes(x = lon, y = lat, color = gran_fct, text = hover_text),
    size = 1.6,
    alpha = 0.9,
    inherit.aes = FALSE
  ) +
  ggplot2::geom_point(
    data = ports,
    ggplot2::aes(x = lon, y = lat, text = hover_text),
    shape = 21,
    stroke = 0.35,
    size = 2.2,
    color = "#1B9E77",
    fill = "#E6FFF2",
    alpha = 0.95,
    inherit.aes = FALSE
  ) +
  ggplot2::geom_point(
    data = boat_points,
    ggplot2::aes(x = lon, y = lat, text = hover_text),
    shape = 24,
    size = 2.1,
    color = "#CC79A7",
    fill = "#FBE6F4",
    stroke = 0.35,
    alpha = 0.95,
    inherit.aes = FALSE
  ) +
  ggplot2::scale_color_manual(
    name = "Waypoint granularity",
    values = granularity_colours,
    drop = TRUE
  ) +
  ggplot2::labs(
    title = "Coastline, Inferred Waypoints, and Ports",
    subtitle = subtitle_text,
    x = NULL,
    y = NULL
  ) +
  coord_fixed_for_lat(coastline$lat, fallback_lat = 65.0)

p <- apply_degree_axes(p)
p <- p + gsp_common_theme(
  legend_position = "right",
  legend_direction = "vertical"
)

# ----------------------------
# INTERACTIVE PLOT
# ----------------------------
p_interactive <- plotly::ggplotly(p, tooltip = "text", source = "map")

# Visible clickable helper points over water only
p_interactive <- plotly::add_markers(
  p_interactive,
  data = click_grid,
  x = ~lon,
  y = ~lat,
  customdata = ~wayp,
  text = ~wayp,
  hovertemplate = "%{text}<extra></extra>",
  inherit = FALSE,
  name = "helper points",
  marker = list(
    size = 7,
    opacity = 0.45,
    color = "#666666"
  ),
  showlegend = FALSE
)

p_interactive <- plotly::event_register(p_interactive, "plotly_click")
p_interactive <- plotly::layout(
  p_interactive,
  hoverlabel = list(align = "left")
)

# ----------------------------
# SHINY APP
# ----------------------------
ui <- shiny::fluidPage(
  shiny::tags$h4("Interactive country plot"),
  shiny::tags$p("Click on the map to generate waypoint lines."),
  plotly::plotlyOutput("map", height = app_height),
  shiny::tags$br(),
  shiny::actionButton("clear_wayps", "Clear"),
  shiny::tags$br(),
  shiny::tags$br(),
  shiny::textAreaInput(
    "wayp_text",
    "Clicked waypoints",
    value = "",
    width = "100%",
    height = "180px",
    resize = "vertical"
  )
)

server <- function(input, output, session) {
  output$map <- plotly::renderPlotly({
    p_interactive
  })

  wayp_lines <- shiny::reactiveVal(character())

  shiny::observe({
    click <- plotly::event_data("plotly_click", source = "map")
    shiny::req(!is.null(click))

    wayp_line <- NULL

    if (!is.null(click$customdata) && length(click$customdata) > 0) {
      wayp_line <- as.character(click$customdata[[1]])
    } else if (!is.null(click$x) && !is.null(click$y)) {
      xy <- lonlat_to_grid(click$x, click$y)
      wayp_line <- sprintf("WAYP %d %d", xy$easting, xy$northing)
    }

    shiny::req(!is.null(wayp_line), nzchar(wayp_line))

    new_lines <- c(wayp_lines(), wayp_line)
    wayp_lines(new_lines)

    shiny::updateTextAreaInput(
      session,
      "wayp_text",
      value = paste(new_lines, collapse = "\n")
    )
  })

  shiny::observeEvent(input$clear_wayps, {
    wayp_lines(character())
    shiny::updateTextAreaInput(session, "wayp_text", value = "")
  })
}

shiny::runApp(
  shiny::shinyApp(ui = ui, server = server),
  launch.browser = TRUE
)
