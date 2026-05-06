#!/usr/bin/env Rscript
# Build pass-by-pass refinement frames and an optional GIF.
# Usage: Rscript R/refinement_sweep_gif.R sol/noport/refinement_180.json [output_dir] [output.gif] [comparison.png]

required_packages <- c("tidyverse", "DBI", "RSQLite", "jsonlite", "grid", "ggpp", "gridExtra")

script_file_arg <- grep("^--file=", commandArgs(trailingOnly = FALSE), value = TRUE)
script_dir <- if (length(script_file_arg) > 0) {
  dirname(normalizePath(sub("^--file=", "", script_file_arg[1])))
} else {
  "R"
}
source(file.path(script_dir, "plot_utils.R"))
source(file.path(script_dir, "refinement_sweep_utils.R"))
load_required_packages(required_packages)

cat("=== GSP Refinement Sweep GIF Builder ===\n\n")

args            <- commandArgs(trailingOnly = TRUE)
refinement_file <- if (length(args) >= 1) args[1] else "sol/noport/refinement_180.json"
frame_dir       <- if (length(args) >= 2) args[2] else sub("\\.[Jj][Ss][Oo][Nn]$", "_sweep_frames", refinement_file)
gif_file        <- if (length(args) >= 3) args[3] else sub("\\.[Jj][Ss][Oo][Nn]$", "_sweep.gif",    refinement_file)
comparison_file <- if (length(args) >= 4) args[4] else sub("\\.[Jj][Ss][Oo][Nn]$", "_sweep.png",    refinement_file)

rf       <- load_refinement_json(refinement_file)
map_data <- load_map_data()

all_route_paths <- dplyr::bind_rows(lapply(rf$solutions, build_route_path, locations = map_data$locations))
bounds          <- compute_map_bounds(all_route_paths)

# Build a context list consumed by the plotting helpers in refinement_sweep_utils.R
ctx <- c(rf, map_data, list(
  fixed_map_coord = function() make_fixed_map_coord(bounds)
))

# ---------------------------------------------------------------------------
# Frame output
# ---------------------------------------------------------------------------

dir.create(frame_dir, recursive = TRUE, showWarnings = FALSE)
unlink(file.path(frame_dir, "*.png"))

frame_paths <- character()
frame_idx   <- 1L

add_frame <- function(plot, label) {
  path <- file.path(frame_dir, sprintf("%02d_%s.png", frame_idx, label))
  save_plot(plot, path)
  frame_paths <<- c(frame_paths, path)
  frame_idx   <<- frame_idx + 1L
  cat(sprintf("Saved frame: %s\n", normalizePath(path, winslash = "/", mustWork = FALSE)))
}

# Initial sweep frame
add_frame(
  regular_route_plot(rf$solutions[[1]], rf$distances[[1]], rf$pass_names[1], ctx),
  rf$pass_names[1]
)

# Per-pass transition frames
for (i in seq_len(length(rf$pass_names) - 1L)) {
  prev_name <- rf$pass_names[i]
  curr_name <- rf$pass_names[i + 1L]

  add_frame(transition_plot(prev_name, curr_name, "leaving",  ctx), sprintf("%s_to_%s_leaving",  prev_name, curr_name))
  add_frame(transition_plot(prev_name, curr_name, "arriving", ctx), sprintf("%s_to_%s_arriving", prev_name, curr_name))
  add_frame(regular_route_plot(rf$solutions[[curr_name]], rf$distances[[curr_name]], curr_name, ctx), curr_name)
}

# Summary comparison frame
comparison_plot <- init_to_final_plot(ctx)
save_plot(comparison_plot, comparison_file)
cat(sprintf("Saved comparison: %s\n", normalizePath(comparison_file, winslash = "/", mustWork = FALSE)))
add_frame(comparison_plot, "init_to_final")

# ---------------------------------------------------------------------------
# GIF assembly
# ---------------------------------------------------------------------------

if (requireNamespace("magick", quietly = TRUE)) {
  cat(sprintf("\nBuilding GIF with 3s frame delay and 10s final frame: %s\n", gif_file))
  images       <- magick::image_read(frame_paths)
  frame_delays <- c(rep(300, length(frame_paths) - 1L), 1000)
  animation    <- magick::image_animate(images, delay = frame_delays)
  magick::image_write(animation, path = gif_file)
  cat(sprintf("OK GIF saved to: %s\n", normalizePath(gif_file, winslash = "/", mustWork = FALSE)))
} else if (nzchar(Sys.which("py"))) {
  cat(sprintf("\nBuilding GIF with Python/Pillow fallback: %s\n", gif_file))
  python_code <- paste(
    "from PIL import Image",
    "import sys",
    "out = sys.argv[1]",
    "paths = sys.argv[2:]",
    "frames = [Image.open(p).convert('RGB').copy() for p in paths]",
    "durations = [3000] * (len(frames) - 1) + [10000]",
    "[frame.info.update({'duration': duration}) for frame, duration in zip(frames, durations)]",
    "frames[0].save(out, save_all=True, append_images=frames[1:], duration=durations, loop=0, disposal=[2]*len(frames))",
    "[frame.close() for frame in frames]",
    sep = "; "
  )
  gif_status <- system2(
    Sys.which("py"),
    args   = c("-c", shQuote(python_code, type = "cmd"),
               normalizePath(gif_file, winslash = "/", mustWork = FALSE),
               normalizePath(frame_paths, winslash = "/")),
    stdout = TRUE,
    stderr = TRUE
  )
  if (!is.null(attr(gif_status, "status")) && attr(gif_status, "status") != 0) {
    warning(paste(gif_status, collapse = "\n"), call. = FALSE)
  } else {
    cat(sprintf("OK GIF saved to: %s\n", normalizePath(gif_file, winslash = "/", mustWork = FALSE)))
  }
} else {
  warning(
    "Package 'magick' is not installed and Python/Pillow fallback is unavailable; ",
    "PNG frames were created, but GIF was skipped.",
    call. = FALSE
  )
}

cat(sprintf("\nOK Frames saved in: %s\n", normalizePath(frame_dir, winslash = "/", mustWork = FALSE)))

