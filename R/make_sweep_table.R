#!/usr/bin/env Rscript

suppressPackageStartupMessages({
  library(jsonlite)
})

`%||%` <- function(x, y) {
  if (is.null(x) || length(x) == 0) y else x
}

args <- commandArgs(trailingOnly = TRUE)

get_arg_value <- function(flag, default = NULL) {
  idx <- match(flag, args)
  if (is.na(idx) || idx >= length(args)) default else args[[idx + 1]]
}

has_flag <- function(flag) {
  any(args == flag)
}

trim_num <- function(x, digits = 3) {
  if (is.null(x) || length(x) == 0 || is.na(x)) return("NA")
  out <- format(round(as.numeric(x), digits), nsmall = digits, trim = TRUE, scientific = FALSE)
  sub("\\.?0+$", "", out)
}

fmt_one_decimal <- function(x) {
  if (is.null(x) || length(x) == 0 || is.na(x)) return("NA")
  out <- format(round(as.numeric(x), 1), nsmall = 1, trim = TRUE, scientific = FALSE)
  sub("\\.?0+$", "", out)
}

fmt_int <- function(x) {
  if (is.null(x) || length(x) == 0 || is.na(x)) return("NA")
  sprintf("%d", as.integer(round(as.numeric(x))))
}

fmt_gap <- function(x) {
  if (is.null(x) || length(x) == 0 || any(is.na(x))) return("NA")
  paste(vapply(x, fmt_int, character(1)), collapse = "/")
}

tex_escape <- function(x) {
  x <- gsub("\\\\", "\\\\textbackslash{}", x)
  x <- gsub("([#$%&_{}])", "\\\\\\1", x, perl = TRUE)
  x <- gsub("~", "\\\\textasciitilde{}", x, fixed = TRUE)
  x <- gsub("\\^", "\\\\textasciicircum{}", x)
  x
}

collect_passes <- function(doc) {
  sol <- doc$solution
  if (is.null(sol) || !is.list(sol)) return(list())
  keys <- names(sol)
  keys <- keys[grepl("^(init|pass[0-9]+)$", keys)]
  ord <- order(ifelse(keys == "init", 0L, as.integer(sub("^pass", "", keys))))
  sol[keys[ord]]
}

count_moved_from_mutations <- function(pass_entry) {
  muts <- pass_entry$tour_segments_station_mutation_ids
  if (is.null(muts) || length(muts) == 0) return(NA_integer_)
  vals <- unlist(muts, recursive = TRUE, use.names = FALSE)
  if (length(vals) == 0) return(0L)
  length(unique(abs(as.integer(vals))))
}

extract_station_move_stats <- function(doc, passes) {
  summary <- doc$summary %||% list()
  moved <- summary$stations_moved_per_pass
  if (!is.null(moved)) {
    return(list(
      mean = as.numeric(moved$mean %||% NA_real_),
      median = as.numeric(moved$median %||% NA_real_),
      max = as.integer(moved$max %||% NA_integer_)
    ))
  }

  if (length(passes) <= 1) {
    return(list(mean = 0, median = 0, max = 0))
  }

  counts <- vapply(passes[-1], count_moved_from_mutations, integer(1))
  counts <- counts[!is.na(counts)]
  if (length(counts) == 0) {
    return(list(mean = NA_real_, median = NA_real_, max = NA_integer_))
  }

  list(
    mean = mean(counts),
    median = median(counts),
    max = max(counts)
  )
}

extract_value <- function(x, name, default = NA) {
  val <- x[[name]]
  if (is.null(val) || length(val) == 0) default else val
}

load_row <- function(path) {
  doc <- fromJSON(path, simplifyVector = FALSE)
  passes <- collect_passes(doc)
  summary <- doc$summary %||% list()
  metadata <- doc$metadata %||% list()
  final_name <- extract_value(summary, "final", NULL)
  final_pass <- if (!is.null(final_name) && !is.null(doc$solution[[final_name]])) {
    doc$solution[[final_name]]
  } else if (length(passes) > 0) {
    passes[[length(passes)]]
  } else {
    NULL
  }

  move_stats <- extract_station_move_stats(doc, passes)
  gap_stats <- summary$capacity_mip_gap_percent %||% list()
  final_distance <- extract_value(summary, "final_total_distance_nm", NA_real_)
  if (is.na(final_distance) && !is.null(final_pass)) {
    final_distance <- extract_value(final_pass, "total_distance_nm", NA_real_)
  }

  pass_count <- extract_value(summary, "sweep_pass_count", NA_integer_)
  if (is.na(pass_count)) {
    pass_count <- max(length(passes) - 1L, 0L)
  }

  accepted_solves <- extract_value(summary, "accepted_capacity_solves", NA_integer_)
  if (is.na(accepted_solves)) {
    boundary_changes <- vapply(passes[-1], function(p) as.integer(extract_value(p, "boundary_changes", 0L)), integer(1))
    accepted_solves <- sum(boundary_changes)
  }

  total_solves <- extract_value(summary, "total_capacity_solves", NA_integer_)
  total_runtime_seconds <- extract_value(summary, "total_runtime_seconds", NA_real_)

  data.frame(
    path = path,
    method = as.character(extract_value(metadata, "strategy", "")),
    l2seg = as.numeric(extract_value(metadata, "l2seg", NA_real_)),
    sweeps = as.integer(pass_count),
    final_distance = as.numeric(final_distance),
    moved_mean = as.numeric(move_stats$mean),
    moved_median = as.numeric(move_stats$median),
    moved_max = as.numeric(move_stats$max),
    accepted_solves = as.integer(accepted_solves),
    total_solves = as.integer(total_solves),
    gap_min = as.numeric(extract_value(gap_stats, "min", NA_real_)),
    gap_mean = as.numeric(extract_value(gap_stats, "mean", NA_real_)),
    gap_max = as.numeric(extract_value(gap_stats, "max", NA_real_)),
    gap_std = as.numeric(extract_value(gap_stats, "std", NA_real_)),
    runtime_min = as.numeric(total_runtime_seconds) / 60.0,
    stringsAsFactors = FALSE
  )
}

render_tex <- function(rows, l2seg_label) {
  cat("%\\begin{table}[ht]\n")
  cat("%\\centering\n")
  cat("%\\caption{Populate caption manually if needed.}\n")
  cat("%\\label{tab:sweep-results}\n")
  cat("\\begin{tabular}{r r r r r r r}\n")
  cat("\\hline\n")
  cat(sprintf("%s & num. & final & stations moved & accepted/ & MIP gap (\\\\%%) & run time \\\\\n",
              tex_escape(l2seg_label)))
  cat("    & sweeps & distance & mean/med/max & solves & min/mean/max/std & (min) \\\\\n")
  cat("\\hline\n")
  for (i in seq_len(nrow(rows))) {
    cat(sprintf(
      "%s & %s & %s & %s/%s/%s & %s/%s & %s & %s \\\\\n",
      fmt_int(rows$l2seg[[i]]),
      fmt_int(rows$sweeps[[i]]),
      trim_num(rows$final_distance[[i]], 3),
      fmt_one_decimal(rows$moved_mean[[i]]),
      fmt_one_decimal(rows$moved_median[[i]]),
      fmt_int(rows$moved_max[[i]]),
      fmt_int(rows$accepted_solves[[i]]),
      fmt_int(rows$total_solves[[i]]),
      fmt_gap(c(rows$gap_min[[i]], rows$gap_mean[[i]], rows$gap_max[[i]], rows$gap_std[[i]])),
      fmt_int(rows$runtime_min[[i]])
    ))
  }
  cat("\\hline\n")
  cat("\\end{tabular}\n")
  cat("%\\end{table}\n")
}

render_markdown <- function(rows, l2seg_label) {
  cat(sprintf("| %s | sweeps | final distance | stations moved mean/med/max | accepted/total solves | MIP gap min/mean/max/std (%%) | run time (min) |\n",
              l2seg_label))
  cat("| ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n")
  for (i in seq_len(nrow(rows))) {
    cat(sprintf(
      "| %s | %s | %s | %s/%s/%s | %s/%s | %s | %s |\n",
      fmt_int(rows$l2seg[[i]]),
      fmt_int(rows$sweeps[[i]]),
      trim_num(rows$final_distance[[i]], 3),
      fmt_one_decimal(rows$moved_mean[[i]]),
      fmt_one_decimal(rows$moved_median[[i]]),
      fmt_int(rows$moved_max[[i]]),
      fmt_int(rows$accepted_solves[[i]]),
      fmt_int(rows$total_solves[[i]]),
      fmt_gap(c(rows$gap_min[[i]], rows$gap_mean[[i]], rows$gap_max[[i]], rows$gap_std[[i]])),
      fmt_int(rows$runtime_min[[i]])
    ))
  }
}

input_dir <- get_arg_value("--input-dir", "sol/nn")
pattern <- get_arg_value("--pattern", "^sweep_[0-9]+\\.json$")
format_name <- tolower(get_arg_value("--format", "tex"))
output_path <- get_arg_value("--output", "")
l2seg_label <- get_arg_value("--l2seg-label", "$L_{\\text{2seg}}$")
header_unit <- tolower(get_arg_value("--l2seg-unit", "stations"))

if (header_unit == "minutes") {
  l2seg_label <- paste0(l2seg_label, " (min)")
} else if (header_unit == "seconds") {
  l2seg_label <- paste0(l2seg_label, " (s)")
} else if (header_unit == "stations") {
  l2seg_label <- l2seg_label
}

paths <- list.files(input_dir, pattern = pattern, full.names = TRUE)
if (length(paths) == 0) {
  stop(sprintf("No sweep JSON files matched %s under %s", pattern, input_dir))
}

rows <- do.call(rbind, lapply(paths, load_row))
rows <- rows[order(rows$l2seg), , drop = FALSE]

missing_cols <- c()
if (any(is.na(rows$total_solves))) missing_cols <- c(missing_cols, "total_capacity_solves")
if (any(is.na(rows$gap_min) | is.na(rows$gap_mean) | is.na(rows$gap_max) | is.na(rows$gap_std))) {
  missing_cols <- c(missing_cols, "capacity_mip_gap_percent")
}

render_output <- function() {
  if (format_name == "tex") render_tex(rows, l2seg_label)
  else if (format_name %in% c("md", "markdown")) render_markdown(rows, l2seg_label)
  else stop(sprintf("Unsupported format: %s", format_name))
}

if (nzchar(output_path)) {
  con <- file(output_path, open = "wt")
  on.exit(close(con), add = TRUE)
  sink(con)
  on.exit(sink(), add = TRUE)
  render_output()
} else {
  render_output()
}

if (length(missing_cols) > 0 && has_flag("--warn-missing")) {
  warning(sprintf("Missing fields in some JSON files: %s", paste(unique(missing_cols), collapse = ", ")))
}
