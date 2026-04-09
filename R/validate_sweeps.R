
#!/usr/bin/env Rscript

required_packages <- c("jsonlite", "tidyverse")

script_file_arg <- grep("^--file=", commandArgs(trailingOnly = FALSE), value = TRUE)
script_dir <- if (length(script_file_arg) > 0) {
  dirname(normalizePath(sub("^--file=", "", script_file_arg[1])))
} else {
  "R"
}
source(file.path(script_dir, "plot_utils.R"))
load_required_packages(required_packages)

args <- commandArgs(trailingOnly = TRUE)
sol_dir <- if (length(args) >= 1) args[1] else "sol"
methods <- c("nn", "ci", "ge", "opt")

read_json_safe <- function(path) {
  tryCatch(jsonlite::fromJSON(path, simplifyVector = FALSE), error = function(e) NULL)
}

ensure_list <- function(x) {
  if (is.null(x)) return(list())
  if (is.list(x) && !is.data.frame(x)) return(x)
  if (is.data.frame(x)) return(split(x, seq_len(nrow(x))))
  list(x)
}

resolve_final_solution <- function(doc) {
  if (is.null(doc) || is.null(doc$solution) || is.null(doc$summary$final)) return(NULL)
  doc$solution[[doc$summary$final]]
}

station_signature <- function(solution) {
  if (is.null(solution) || is.null(solution$tour_segments_station_ids)) return(NA_character_)
  paste(
    vapply(
      ensure_list(solution$tour_segments_station_ids),
      function(seg) paste(unlist(seg), collapse = ","),
      character(1)
    ),
    collapse = "|"
  )
}

cat("=== Sweep Validation ===\n\n")
cat(sprintf("Solution directory: %s\n\n", sol_dir))

rows <- list()

for (method in methods) {
  sweep_paths <- Sys.glob(file.path(sol_dir, method, "sweep_*.json"))
  if (length(sweep_paths) == 0) next

  sweep_tbl <- lapply(sort(sweep_paths), function(path) {
    doc <- read_json_safe(path)
    solution <- resolve_final_solution(doc)
    tibble(
      method = method,
      file = basename(path),
      status = if (!is.null(doc$summary$status)) as.character(doc$summary$status) else NA_character_,
      distance_nm = if (!is.null(doc$summary$final_total_distance_nm)) as.numeric(doc$summary$final_total_distance_nm) else NA_real_,
      station_signature = station_signature(solution)
    )
  }) %>% bind_rows()

  baseline_distance <- sweep_tbl$distance_nm[[1]]
  baseline_signature <- sweep_tbl$station_signature[[1]]

  rows[[length(rows) + 1]] <- tibble(
    method = method,
    sweep_files = nrow(sweep_tbl),
    statuses_ok = all(sweep_tbl$status == "sweep_complete"),
    distance_same = all(dplyr::near(sweep_tbl$distance_nm, baseline_distance)),
    station_order_same = all(sweep_tbl$station_signature == baseline_signature),
    reference_distance_nm = baseline_distance
  )
}

result <- bind_rows(rows) %>%
  mutate(reference_distance_nm = round(reference_distance_nm, 2))

cat("| method | sweep_files | statuses_ok | distance_same | station_order_same | reference_distance_nm |\n")
cat("|---|---:|---|---|---|---:|\n")
for (i in seq_len(nrow(result))) {
  cat(sprintf(
    "| %s | %d | %s | %s | %s | %.2f |\n",
    result$method[[i]],
    result$sweep_files[[i]],
    ifelse(result$statuses_ok[[i]], "true", "false"),
    ifelse(result$distance_same[[i]], "true", "false"),
    ifelse(result$station_order_same[[i]], "true", "false"),
    result$reference_distance_nm[[i]]
  ))
}
cat("\n")
