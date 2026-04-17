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
output_file <- if (length(args) >= 2) args[2] else file.path(sol_dir, "sweep_summary_panels.png")

methods <- c("nn", "ci", "ge", "noport")
method_labels <- c(nn = "NN", ci = "CI", ge = "GE", noport = "NOPORT")
method_colors <- c(nn = "#355070", ci = "#6D597A", ge = "#B56576", noport = "#E56B6F")

read_json_safe <- function(path) {
  tryCatch(jsonlite::fromJSON(path, simplifyVector = FALSE), error = function(e) NULL)
}

extract_station_rows <- function(method, path) {
  doc <- read_json_safe(path)
  if (is.null(doc) || is.null(doc$summary$pass_count) || is.null(doc$solution)) {
    return(tibble())
  }

  pass_count <- as.integer(doc$summary$pass_count)
  if (is.na(pass_count) || pass_count <= 1) {
    return(tibble())
  }

  rows <- lapply(seq.int(1, pass_count - 1), function(pass_idx) {
    pass_obj <- doc$solution[[sprintf("pass%d", pass_idx)]]
    if (is.null(pass_obj) || is.null(pass_obj$changed) || !isTRUE(pass_obj$changed)) {
      return(NULL)
    }
    tibble(
      method = method,
      pass = pass_idx,
      stations_moved = if (!is.null(pass_obj$stations_moved)) as.numeric(pass_obj$stations_moved) else NA_real_
    )
  })

  bind_rows(rows)
}

extract_distance_rows <- function(method, path) {
  doc <- read_json_safe(path)
  if (is.null(doc) || is.null(doc$summary$total_distance_nm)) {
    return(tibble())
  }

  tibble(
    method = method,
    sweep = seq_along(doc$summary$total_distance_nm) - 1,
    total_distance_nm = as.numeric(unlist(doc$summary$total_distance_nm))
  )
}

extract_mip_rows <- function(method, path) {
  doc <- read_json_safe(path)
  if (is.null(doc) || is.null(doc$summary$pass_count) || is.null(doc$solution)) {
    return(list(runtime = tibble(), gap = tibble(), solves = tibble()))
  }

  pass_count <- as.integer(doc$summary$pass_count)
  if (is.na(pass_count) || pass_count <= 1) {
    return(list(runtime = tibble(), gap = tibble(), solves = tibble()))
  }

  runtime_rows <- list()
  gap_rows <- list()
  solve_rows <- list()

  for (pass_idx in seq.int(1, pass_count - 1)) {
    pass_obj <- doc$solution[[sprintf("pass%d", pass_idx)]]
    if (is.null(pass_obj)) next

    runtime_vals <- if (!is.null(pass_obj$capacity_mip_runtime_seconds_values)) {
      as.numeric(unlist(pass_obj$capacity_mip_runtime_seconds_values))
    } else {
      numeric(0)
    }
    gap_vals <- if (!is.null(pass_obj$capacity_mip_gap_percent_values)) {
      as.numeric(unlist(pass_obj$capacity_mip_gap_percent_values))
    } else {
      numeric(0)
    }

    if (length(runtime_vals) > 0) {
      runtime_rows[[length(runtime_rows) + 1]] <- tibble(
        method = method,
        pass = pass_idx,
        mip_runtime_seconds = runtime_vals
      )
    }

    if (length(gap_vals) > 0) {
      gap_rows[[length(gap_rows) + 1]] <- tibble(
        method = method,
        pass = pass_idx,
        mip_gap_percent = gap_vals
      )
    }

    solve_rows[[length(solve_rows) + 1]] <- tibble(
      method = method,
      pass = pass_idx,
      total_capacity_solves = if (!is.null(pass_obj$total_capacity_solves)) as.numeric(pass_obj$total_capacity_solves) else NA_real_,
      accepted_capacity_solves = if (!is.null(pass_obj$accepted_capacity_solves)) as.numeric(pass_obj$accepted_capacity_solves) else NA_real_
    )
  }

  list(
    runtime = bind_rows(runtime_rows),
    gap = bind_rows(gap_rows),
    solves = bind_rows(solve_rows)
  )
}

station_rows <- bind_rows(lapply(methods, function(method) {
  extract_station_rows(method, file.path(sol_dir, method, "refinement.json"))
})) %>%
  mutate(method = factor(method, levels = methods, labels = unname(method_labels[methods])))

distance_rows <- bind_rows(lapply(methods, function(method) {
  extract_distance_rows(method, file.path(sol_dir, method, "refinement.json"))
})) %>%
  group_by(method) %>%
  mutate(
    initial_distance_nm = first(total_distance_nm),
    relative_improvement_pct = ifelse(
      is.na(initial_distance_nm) | initial_distance_nm == 0,
      NA_real_,
      100 * (initial_distance_nm - total_distance_nm) / initial_distance_nm
    )
  ) %>%
  ungroup() %>%
  mutate(method = factor(method, levels = methods, labels = unname(method_labels[methods])))

mip_lists <- lapply(methods, function(method) {
  extract_mip_rows(method, file.path(sol_dir, method, "refinement.json"))
})

mip_runtime_rows <- bind_rows(lapply(mip_lists, `[[`, "runtime")) %>%
  mutate(method = factor(method, levels = methods, labels = unname(method_labels[methods])))

mip_gap_rows <- bind_rows(lapply(mip_lists, `[[`, "gap")) %>%
  mutate(method = factor(method, levels = methods, labels = unname(method_labels[methods])))

solve_rows <- bind_rows(lapply(mip_lists, `[[`, "solves")) %>%
  mutate(method = factor(method, levels = methods, labels = unname(method_labels[methods])))

if (nrow(station_rows) == 0 || nrow(distance_rows) == 0) {
  stop("Missing refinement.json data needed for combined sweep summary figure.", call. = FALSE)
}

station_summary <- station_rows %>%
  group_by(method) %>%
  summarize(
    sweeps = n(),
    mean_moved = mean(stations_moved, na.rm = TRUE),
    .groups = "drop"
  )

boxplot_subtitle <- paste(
  sprintf("%s: %d changed sweeps, mean %.1f", station_summary$method, station_summary$sweeps, station_summary$mean_moved),
  collapse = " | "
)

panel_a <- ggplot(station_rows, aes(x = method, y = stations_moved, fill = method)) +
  geom_boxplot(width = 0.62, alpha = 0.82, outlier.shape = NA, color = "#2F2F2F") +
  scale_fill_manual(values = unname(method_colors[methods])) +
  labs(
    title = "Stations Moved",
    subtitle = boxplot_subtitle,
    x = NULL,
    y = "Stations moved"
  ) +
  gsp_common_theme(legend_position = "none") +
  theme(
    plot.title = element_text(hjust = 0.5, face = "bold"),
    plot.subtitle = element_text(hjust = 0.5, size = 10)
  )

panel_b <- ggplot(distance_rows, aes(x = sweep, y = total_distance_nm, color = method)) +
  geom_line(linewidth = 1.1) +
  geom_point(size = 2.3) +
  scale_color_manual(values = unname(method_colors[methods])) +
  scale_x_continuous(breaks = scales::pretty_breaks()) +
  labs(
    title = "Distance By Sweep",
    x = "Sweep pass",
    y = "Total distance (nm)",
    color = "Method"
  ) +
  gsp_common_theme(legend_position = "top", legend_direction = "horizontal") +
  theme(
    plot.title = element_text(hjust = 0.5, face = "bold"),
    legend.title = element_text(face = "bold")
  )

panel_c <- ggplot(distance_rows, aes(x = sweep, y = relative_improvement_pct, color = method)) +
  geom_line(linewidth = 1.1) +
  geom_point(size = 2.3) +
  scale_color_manual(values = unname(method_colors[methods])) +
  scale_x_continuous(breaks = scales::pretty_breaks()) +
  labs(
    title = "Relative Improvement By Sweep",
    x = "Sweep pass",
    y = "Improvement (%)",
    color = "Method"
  ) +
  gsp_common_theme(legend_position = "none") +
  theme(
    plot.title = element_text(hjust = 0.5, face = "bold"),
    legend.title = element_text(face = "bold")
  )

panel_d <- ggplot(mip_runtime_rows, aes(x = method, y = mip_runtime_seconds, fill = method)) +
  geom_boxplot(width = 0.62, alpha = 0.82, outlier.shape = NA, color = "#2F2F2F") +
  scale_fill_manual(values = unname(method_colors[methods])) +
  labs(
    title = "MIP Runtime Distribution",
    x = NULL,
    y = "MIP runtime (s)"
  ) +
  gsp_common_theme(legend_position = "none") +
  theme(
    plot.title = element_text(hjust = 0.5, face = "bold")
  )

panel_e <- ggplot(mip_gap_rows, aes(x = method, y = mip_gap_percent, fill = method)) +
  geom_boxplot(width = 0.62, alpha = 0.82, outlier.shape = NA, color = "#2F2F2F") +
  scale_fill_manual(values = unname(method_colors[methods])) +
  labs(
    title = "MIP Gap Distribution",
    x = NULL,
    y = "MIP gap (%)"
  ) +
  gsp_common_theme(legend_position = "none") +
  theme(
    plot.title = element_text(hjust = 0.5, face = "bold")
  )

panel_f <- ggplot(solve_rows, aes(x = method, y = total_capacity_solves, fill = method)) +
  geom_boxplot(width = 0.62, alpha = 0.82, outlier.shape = NA, color = "#2F2F2F") +
  scale_fill_manual(values = unname(method_colors[methods])) +
  labs(
    title = "Capacity Solves Per Pass",
    x = NULL,
    y = "Total capacity solves"
  ) +
  gsp_common_theme(legend_position = "none") +
  theme(
    plot.title = element_text(hjust = 0.5, face = "bold")
  )

dir.create(dirname(output_file), recursive = TRUE, showWarnings = FALSE)

png(filename = output_file, width = 15.0, height = 9.5, units = "in", res = 320, bg = "white")
grid::grid.newpage()
grid::pushViewport(grid::viewport(layout = grid::grid.layout(
  nrow = 2, ncol = 3, widths = grid::unit(c(1, 1, 1), "null"), heights = grid::unit(c(1, 1), "null")
)))

panel_a_grob <- ggplotGrob(panel_a)
panel_b_grob <- ggplotGrob(panel_b)
panel_c_grob <- ggplotGrob(panel_c)
panel_d_grob <- ggplotGrob(panel_d)
panel_e_grob <- ggplotGrob(panel_e)
panel_f_grob <- ggplotGrob(panel_f)

grid::pushViewport(grid::viewport(layout.pos.row = 1, layout.pos.col = 1))
grid::grid.draw(panel_a_grob)
grid::grid.text("A", x = grid::unit(0.02, "npc"), y = grid::unit(0.98, "npc"),
                just = c("left", "top"), gp = grid::gpar(fontsize = 16, fontface = "bold"))
grid::popViewport()

grid::pushViewport(grid::viewport(layout.pos.row = 1, layout.pos.col = 2))
grid::grid.draw(panel_b_grob)
grid::grid.text("B", x = grid::unit(0.02, "npc"), y = grid::unit(0.98, "npc"),
                just = c("left", "top"), gp = grid::gpar(fontsize = 16, fontface = "bold"))
grid::popViewport()

grid::pushViewport(grid::viewport(layout.pos.row = 1, layout.pos.col = 3))
grid::grid.draw(panel_c_grob)
grid::grid.text("C", x = grid::unit(0.02, "npc"), y = grid::unit(0.98, "npc"),
                just = c("left", "top"), gp = grid::gpar(fontsize = 16, fontface = "bold"))
grid::popViewport()

grid::pushViewport(grid::viewport(layout.pos.row = 2, layout.pos.col = 1))
grid::grid.draw(panel_d_grob)
grid::grid.text("D", x = grid::unit(0.02, "npc"), y = grid::unit(0.98, "npc"),
                just = c("left", "top"), gp = grid::gpar(fontsize = 16, fontface = "bold"))
grid::popViewport()

grid::pushViewport(grid::viewport(layout.pos.row = 2, layout.pos.col = 2))
grid::grid.draw(panel_e_grob)
grid::grid.text("E", x = grid::unit(0.02, "npc"), y = grid::unit(0.98, "npc"),
                just = c("left", "top"), gp = grid::gpar(fontsize = 16, fontface = "bold"))
grid::popViewport()

grid::pushViewport(grid::viewport(layout.pos.row = 2, layout.pos.col = 3))
grid::grid.draw(panel_f_grob)
grid::grid.text("F", x = grid::unit(0.02, "npc"), y = grid::unit(0.98, "npc"),
                just = c("left", "top"), gp = grid::gpar(fontsize = 16, fontface = "bold"))
grid::popViewport(2)

dev.off()

cat(sprintf("Saved plot to %s\n", output_file))
