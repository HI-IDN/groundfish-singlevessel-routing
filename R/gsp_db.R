#!/usr/bin/env Rscript

load_gsp_db_packages <- function() {
  for (pkg in c("DBI", "RSQLite")) {
    if (!requireNamespace(pkg, quietly = TRUE)) {
      stop(sprintf("Missing required R package: %s", pkg), call. = FALSE)
    }
  }
}

connect_gsp_db <- function(gsp_db = "dat/gsp.db", solution_db = "dat/solution.db") {
  load_gsp_db_packages()
  if (!file.exists(gsp_db)) stop(sprintf("Missing routing DB: %s", gsp_db), call. = FALSE)
  if (!file.exists(solution_db)) stop(sprintf("Missing solution DB: %s", solution_db), call. = FALSE)

  con <- DBI::dbConnect(RSQLite::SQLite(), dbname = gsp_db)
  DBI::dbExecute(
    con,
    sprintf("ATTACH DATABASE %s AS solution", DBI::dbQuoteString(con, solution_db))
  )
  con
}

db_read <- function(con, sql, params = NULL) {
  if (is.null(params)) {
    DBI::dbGetQuery(con, sql)
  } else {
    DBI::dbGetQuery(con, sql, params = params)
  }
}

read_country_layers <- function(con) {
  list(
    coastline = db_read(con, "
      SELECT lon, lat
      FROM coastline
      ORDER BY id
    "),
    stations = db_read(con, "
      WITH station_xy AS (
        SELECT
          s.id AS station_id,
          s.amount AS catch_amount,
          ls.lon AS start_lon,
          ls.lat AS start_lat,
          le.lon AS end_lon,
          le.lat AS end_lat
        FROM stations s
        JOIN locations ls ON ls.id = s.start_location_id
        JOIN locations le ON le.id = s.end_location_id
      )
      SELECT
        station_id,
        catch_amount,
        log10(NULLIF(catch_amount, 0)) AS log10_catch,
        start_lon,
        start_lat,
        end_lon,
        end_lat
      FROM station_xy
    ")
  )
}

read_ports <- function(con, port_ids = NULL) {
  ports <- db_read(con, "
    WITH selected_ports AS (
      SELECT id, name, location_id
      FROM ports
    )
    SELECT
      p.id,
      p.name,
      p.location_id,
      l.lon,
      l.lat
    FROM selected_ports p
    JOIN locations l ON l.id = p.location_id
    ORDER BY p.id
  ")

  if (!is.null(port_ids) && length(port_ids) > 0L) {
    ports <- ports[ports$id %in% port_ids | ports$location_id %in% port_ids, , drop = FALSE]
  }
  ports
}

read_vessels <- function(con, boat_ids = NULL) {
  vessels <- db_read(con, "
    SELECT
      b.id AS boat_id,
      b.name,
      b.capacity,
      b.location_id,
      l.lon,
      l.lat
    FROM boats b
    JOIN locations l ON l.id = b.location_id
    ORDER BY b.id
  ")

  if (!is.null(boat_ids) && length(boat_ids) > 0L) {
    vessels <- vessels[vessels$boat_id %in% boat_ids, , drop = FALSE]
  }
  vessels
}

read_waypoints <- function(con, granularity = NULL) {
  waypoints <- db_read(con, "
    SELECT
      w.id,
      w.location_id,
      w.granularity,
      l.lon,
      l.lat
    FROM waypoints w
    JOIN locations l ON l.id = w.location_id
    ORDER BY w.granularity, w.id
  ")

  if (!is.null(granularity) && length(granularity) > 0L) {
    waypoints <- waypoints[waypoints$granularity %in% granularity, , drop = FALSE]
  }
  waypoints
}

read_solution_runs <- function(con) {
  db_read(con, "
    SELECT
      run_id,
      method,
      phase,
      solution_key,
      parent_run_id,
      is_final,
      n_segments,
      feasible,
      boat_id,
      boat_name,
      NULL AS boat_capacity,
      runtime_seconds,
      source_json
    FROM solution.runs
    ORDER BY method, phase, run_id
  ")
}

read_route_run <- function(con, run_id) {
  run <- db_read(con, "
    SELECT
      runs.run_id,
      runs.method,
      runs.phase,
      runs.solution_key,
      runs.parent_run_id,
      runs.is_final,
      runs.n_segments,
      runs.feasible,
      runs.boat_id,
      runs.boat_name,
      boats.capacity AS boat_capacity,
      runs.runtime_seconds,
      runs.source_json
    FROM solution.runs
    LEFT JOIN boats ON boats.id = runs.boat_id
    WHERE runs.run_id = ?
  ", list(run_id))

  if (nrow(run) != 1L) {
    stop(sprintf("Expected one run for run_id='%s', found %d", run_id, nrow(run)), call. = FALSE)
  }

  route_path <- db_read(con, "
    WITH route AS (
      SELECT
        run_id,
        segment,
        sequence,
        location_id,
        point_type
      FROM solution.location_segments
      WHERE run_id = ?
    )
    SELECT
      route.segment,
      route.sequence,
      route.location_id,
      route.point_type,
      locations.lon,
      locations.lat
    FROM route
    JOIN locations ON locations.id = route.location_id
    ORDER BY route.segment, route.sequence
  ", list(run_id))

  station_lines <- db_read(con, "
    WITH segment_stations AS (
      SELECT
        run_id,
        segment,
        sequence,
        signed_station_id,
        station_id
      FROM solution.station_segments
      WHERE run_id = ?
    ),
    station_xy AS (
      SELECT
        ss.segment,
        ss.sequence,
        ss.signed_station_id,
        ss.station_id,
        stations.amount AS catch_amount,
        CASE WHEN ss.signed_station_id < 0 THEN end_loc.lon ELSE start_loc.lon END AS start_lon,
        CASE WHEN ss.signed_station_id < 0 THEN end_loc.lat ELSE start_loc.lat END AS start_lat,
        CASE WHEN ss.signed_station_id < 0 THEN start_loc.lon ELSE end_loc.lon END AS end_lon,
        CASE WHEN ss.signed_station_id < 0 THEN start_loc.lat ELSE end_loc.lat END AS end_lat
      FROM segment_stations ss
      JOIN stations ON stations.id = ss.station_id
      JOIN locations start_loc ON start_loc.id = stations.start_location_id
      JOIN locations end_loc ON end_loc.id = stations.end_location_id
    )
    SELECT *
    FROM station_xy
    ORDER BY segment, sequence
  ", list(run_id))

  distances <- db_read(con, "
    SELECT
      segment,
      transit_nm,
      total_nm
    FROM solution.distance
    WHERE run_id = ?
    ORDER BY segment IS NULL, segment
  ", list(run_id))

  list(
    run = run,
    route_path = route_path,
    station_lines = station_lines,
    distances = distances
  )
}

read_final_run_id <- function(con, method, phase) {
  rows <- db_read(con, "
    SELECT run_id
    FROM solution.runs
    WHERE method = ?
      AND phase = ?
      AND is_final = 1
    ORDER BY run_id
  ", list(method, phase))

  if (nrow(rows) != 1L) {
    stop(
      sprintf(
        "Expected one final run for method='%s', phase='%s', found %d",
        method, phase, nrow(rows)
      ),
      call. = FALSE
    )
  }
  rows$run_id[[1]]
}

read_refinement_passes <- function(con, refinement_run_id = NULL) {
  sql <- "
    SELECT
      run_id,
      solution_run_id,
      pass_number,
      changed,
      stations_moved,
      boundary_attempts,
      boundary_changes,
      mip_solves,
      runtime_seconds
    FROM solution.refinement_passes
  "
  if (is.null(refinement_run_id)) {
    db_read(con, paste(sql, "ORDER BY run_id, pass_number"))
  } else {
    db_read(con, paste(sql, "WHERE run_id = ? ORDER BY pass_number"), list(refinement_run_id))
  }
}

read_refinement_station_mutations <- function(con, refinement_run_id, pass_number = NULL) {
  sql <- "
    SELECT
      run_id,
      pass_number,
      segment,
      sequence,
      signed_station_id,
      station_id
    FROM solution.refinement_station_mutations
    WHERE run_id = ?
  "
  if (is.null(pass_number)) {
    db_read(con, paste(sql, "ORDER BY pass_number, segment, sequence"), list(refinement_run_id))
  } else {
    db_read(con, paste(sql, "AND pass_number = ? ORDER BY segment, sequence"), list(refinement_run_id, pass_number))
  }
}

read_mip_solves <- function(con) {
  db_read(con, "
    SELECT
      phase_code,
      segment_model,
      station_count,
      node_count,
      model_variable_count,
      model_constraint_count,
      runtime_seconds,
      gap_percent
    FROM solution.mip_solves
  ")
}
