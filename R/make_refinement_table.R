#!/usr/bin/env Rscript
# make_refinement_table.R
#
# Builds a LaTeX summary table from sol/*/refinement*.json files.
# Discovers all refinement_<l2seg>.json (and refinement.json) per method.
# refinement.json without a numeric suffix is treated as L2seg = Inf
# (uncapped / solver time-limit = 0).
#
# Run from the project root:
#   Rscript R/make_refinement_table.R

load_required_packages <- function(pkgs) {
  for (pkg in pkgs) {
    if (!requireNamespace(pkg, quietly = TRUE)) {
      stop(sprintf(
        "Package '%s' is required but not installed.\nInstall with: install.packages('%s')",
        pkg, pkg
      ), call. = FALSE)
    }
    suppressPackageStartupMessages(library(pkg, character.only = TRUE))
  }
}

load_required_packages(c("jsonlite", "dplyr", "ggplot2", "cowplot", "knitr"))

script_dir <- tryCatch(
  dirname(normalizePath(sys.frame(1)$ofile)),
  error = function(e) getwd()
)
sol_dir <- normalizePath(file.path(script_dir, "..", "sol"), mustWork = FALSE)

if (!dir.exists(sol_dir)) {
  sol_dir <- "sol"
}

format_runtime <- function(seconds) {
  ifelse(
    is.na(seconds),
    "---",
    ifelse(
      seconds >= 7200,
      sprintf("%.1f h", seconds / 3600),
      sprintf("%.1f min", seconds / 60)
    )
  )
}

format_distance <- function(value) {
  if (is.na(value)) {
    return("---")
  }
  sprintf("%.2f", value)
}

format_num <- function(value, digits = 1) {
  if (is.null(value) || length(value) == 0 || is.na(value)) {
    return("---")
  }
  sprintf(paste0("%.", digits, "f"), as.numeric(value))
}

# Format an l2seg value for display: NA (uncapped) -> "$\infty$", else integer string.
format_l2seg <- function(l2seg) {
  ifelse(is.na(l2seg), "$\\infty$", as.character(l2seg))
}

read_result_json <- function(method, source = "refinement") {
  json_path <- file.path(sol_dir, method, sprintf("%s.json", source))
  if (!file.exists(json_path)) {
    warning(sprintf("Missing result JSON: %s", json_path))
    return(NULL)
  }

  tryCatch(
    jsonlite::fromJSON(json_path, simplifyVector = FALSE),
    error = function(e) {
      warning(sprintf("Failed to parse %s: %s", json_path, e$message))
      NULL
    }
  )
}

extract_solution_distance <- function(json, component = "transit") {
  if (is.null(json)) {
    return(NA_real_)
  }

  final_variant <- json$summary$status$final
  if (is.null(final_variant) || length(final_variant) == 0) {
    return(NA_real_)
  }

  value <- json$solution[[final_variant]]$distance_nm$grand_total[[component]]
  if (is.null(value) || length(value) == 0 || is.na(value)) {
    return(NA_real_)
  }

  as.numeric(value)
}

notation_rows <- tibble::tribble(
  ~method,   ~Notation, ~Variant,
  "nn",      "MH-NN",   "MH with nearest-neighbor",
  "ci",      "MH-CI",   "MH with cheapest-insertion",
  "ge",      "MH-GE",   "MH with greedy-edge",
  "noport",  "MH-OPT",  "MH with NP-based initialization"
)

# ---------------------------------------------------------------------------
# Discover all refinement JSON sources for a given method directory.
# Returns a tibble: source (file stem), l2seg (integer or NA = uncapped).
# Sorted: numbered ascending, uncapped (NA) last.
# ---------------------------------------------------------------------------
find_refinement_sources <- function(method) {
  method_dir <- file.path(sol_dir, method)
  files <- list.files(
    method_dir,
    pattern = "^refinement(_\\d+)?\\.json$",
    full.names = FALSE
  )
  if (length(files) == 0) {
    return(tibble::tibble(source = character(0), l2seg = integer(0)))
  }
  stems <- gsub("\\.json$", "", files)
  # Plain "refinement" yields NA (uncapped); "refinement_60" yields 60L
  l2seg <- suppressWarnings(
    as.integer(gsub("^refinement_(\\d+)$", "\\1", stems))
  )
  tibble::tibble(source = stems, l2seg = l2seg) %>%
    dplyr::arrange(dplyr::coalesce(l2seg, Inf))
}

# Helper: normalise l2seg from JSON metadata
# mip_time_limit_seconds == 0 means uncapped -> treat as NA
l2seg_from_json <- function(json) {
  raw <- as.integer(json$metadata$mip_time_limit_seconds)
  if (!is.na(raw) && raw == 0L) NA_integer_ else raw
}

# ---------------------------------------------------------------------------
trajectory_rows <- function(method, source = "refinement") {
  json <- read_result_json(method, source)
  if (is.null(json)) {
    return(NULL)
  }

  # Build ordered pass keys: init, pass1, pass2, ...
  n_passes  <- as.integer(json$summary$sweep$sweep_pass_count)
  pass_keys <- c("init", if (n_passes > 0L) paste0("pass", seq_len(n_passes)))

  # Read transit distance directly from solution.<key>.distance_nm.grand_total.transit
  transit <- vapply(pass_keys, function(k) {
    val <- json$solution[[k]]$distance_nm$grand_total$transit
    if (is.null(val) || length(val) == 0L) NA_real_ else as.numeric(val)
  }, numeric(1))

  initial_transit <- transit[[1L]]   # solution.init.distance_nm.grand_total.transit

  moved <- vapply(seq_along(pass_keys), function(i) {
    if (i == 1L) return(NA_integer_)
    value <- json$solution[[pass_keys[i]]]$stations_moved
    if (is.null(value) || length(value) == 0L || is.na(value)) return(NA_integer_)
    as.integer(value)
  }, integer(1))
  accepted <- vapply(seq_along(pass_keys), function(i) {
    if (i == 1L) return(NA_integer_)
    value <- json$solution[[pass_keys[i]]]$accepted_capacity_solves
    if (is.null(value) || length(value) == 0L || is.na(value)) return(NA_integer_)
    as.integer(value)
  }, integer(1))
  capacity_solves <- vapply(seq_along(pass_keys), function(i) {
    if (i == 1L) return(NA_integer_)
    value <- json$solution[[pass_keys[i]]]$total_capacity_solves
    if (is.null(value) || length(value) == 0L || is.na(value)) return(NA_integer_)
    as.integer(value)
  }, integer(1))
  mip_solves <- vapply(seq_along(pass_keys), function(i) {
    if (i == 1L) return(NA_integer_)
    value <- json$solution[[pass_keys[i]]]$mip_solves
    if (is.null(value) || length(value) == 0L || is.na(value)) return(NA_integer_)
    as.integer(value)
  }, integer(1))

  tibble::tibble(
    method = method,
    source = source,
    l2seg = l2seg_from_json(json),
    sweep = seq_along(pass_keys) - 1L,
    transit_nm = transit,
    relative_improvement_percent = 100 * (initial_transit - transit) / initial_transit,
    stations_moved = moved,
    accepted_capacity_solves = accepted,
    total_capacity_solves = capacity_solves,
    mip_solves = mip_solves,
    point_label = ifelse(
      is.na(moved),
      NA_character_,
      sprintf("moved %d\naccepted %d/%d", moved, accepted, capacity_solves)
    )
  )
}

refinement_row <- function(method, source = "refinement") {
  json <- read_result_json(method, source)
  if (is.null(json)) {
    return(NULL)
  }

  moved <- json$summary$sweep$stations_moved_per_pass
  accepted <- json$summary$sweep$accepted_capacity_solves
  capacity_solves <- json$summary$sweep$total_capacity_solves

  # Read transit directly — never use total or haul
  initial_transit <- as.numeric(json$solution$init$distance_nm$grand_total$transit)
  final_transit   <- as.numeric(json$solution[[json$summary$status$final]]$distance_nm$grand_total$transit)
  improvement         <- initial_transit - final_transit
  relative_improvement <- 100 * improvement / initial_transit

  tibble::tibble(
    method = method,
    source = source,
    l2seg = l2seg_from_json(json),
    Sweeps = json$summary$sweep$sweep_pass_count,
    `Final transit (nm)` = format_distance(final_transit),
    `Transit improvement (nm)` = format_distance(improvement),
    `Relative improvement (%)` = format_num(relative_improvement, 1),
    `Stations moved` = sprintf(
      "%s/%s/%s",
      format_num(moved$mean, 1),
      format_num(moved$median, 1),
      ifelse(is.null(moved$max) || is.na(moved$max), "---", as.character(moved$max))
    ),
    `Accepted/capacity solves` = sprintf("%s/%s", accepted, capacity_solves),
    `Boundary changes` = json$summary$sweep$total_boundary_changes,
    `MIP solves` = json$summary$sweep$total_mip_solves,
    Runtime = format_runtime(as.numeric(json$summary$runtime_seconds$grandtotal))
  )
}

# ---------------------------------------------------------------------------
# Expand notation_rows with all discovered refinement sources per method
# ---------------------------------------------------------------------------
all_runs <- lapply(seq_len(nrow(notation_rows)), function(i) {
  method <- notation_rows$method[i]
  srcs <- find_refinement_sources(method)
  if (nrow(srcs) == 0) {
    warning(sprintf("No refinement JSON files found for method: %s", method))
    return(NULL)
  }
  cbind(notation_rows[i, ], srcs)
}) %>%
  dplyr::bind_rows()

# Label: append "(Xs)" when a method has >1 run; use Inf symbol for uncapped
run_counts <- table(all_runs$method)
all_runs <- all_runs %>%
  dplyr::mutate(
    Label = ifelse(
      run_counts[method] > 1,
      sprintf("%s (%ss)", Notation, format_l2seg(l2seg)),
      Notation
    )
  )

# ---------------------------------------------------------------------------
# Build summary table
# ---------------------------------------------------------------------------
rows <- lapply(seq_len(nrow(all_runs)), function(i) {
  refinement_row(all_runs$method[i], all_runs$source[i])
})
refinement_tbl <- dplyr::bind_rows(rows) %>%
  left_join(all_runs, by = c("method", "source", "l2seg")) %>%
  dplyr::mutate(`$L_{2\\text{seg}}$ (s)` = format_l2seg(l2seg)) %>%
  select(
    Notation,
    `$L_{2\\text{seg}}$ (s)`,
    Variant,
    Sweeps,
    `Final transit (nm)`,
    `Transit improvement (nm)`,
    `Relative improvement (%)`,
    `Stations moved`,
    `Accepted/capacity solves`,
    `Boundary changes`,
    `MIP solves`,
    Runtime
  )

# ---------------------------------------------------------------------------
# Build trajectory table for plots
# ---------------------------------------------------------------------------
trajectory_tbl <- lapply(seq_len(nrow(all_runs)), function(i) {
  trajectory_rows(all_runs$method[i], all_runs$source[i])
}) %>%
  dplyr::bind_rows() %>%
  left_join(all_runs, by = c("method", "source", "l2seg"))

trajectory_tbl$Label <- factor(
  trajectory_tbl$Label,
  levels = unique(all_runs$Label)
)

# ---------------------------------------------------------------------------
# Whether to annotate the right end of each line with improvement values
show_end_labels <- TRUE

# ---------------------------------------------------------------------------
# Function: build and save one per-l2seg line plot
# ---------------------------------------------------------------------------
make_sweep_lineplot <- function(plot_l2seg, traj_tbl, show_end_labels = TRUE) {
  tbl <- traj_tbl %>%
    dplyr::filter(
      if (is.na(plot_l2seg)) is.na(l2seg) else (!is.na(l2seg) & l2seg == plot_l2seg)
    )

  if (nrow(tbl) == 0) {
    warning(sprintf("No trajectory data for l2seg = %s", plot_l2seg))
    return(invisible(NULL))
  }

  subtitle <- if (is.na(plot_l2seg)) {
    bquote(L[2*seg] == infinity ~ "(uncapped)")
  } else {
    bquote(L[2*seg] == .(plot_l2seg) ~ s)
  }

  subtitle_txt <- if (is.na(plot_l2seg)) {
    "L\u2082seg = \u221e (uncapped)"
  } else {
    sprintf("L\u2082seg = %d s", plot_l2seg)
  }

  x_max <- max(tbl$sweep)

  end_tbl <- tbl %>%
    dplyr::group_by(Label) %>%
    dplyr::filter(sweep == max(sweep)) %>%
    dplyr::ungroup() %>%
    dplyr::left_join(
      tbl %>%
        dplyr::filter(sweep == 0) %>%
        dplyr::select(Label, initial_nm = transit_nm),
      by = "Label"
    ) %>%
    dplyr::mutate(
      end_label_a = sprintf("%+.0f nm", transit_nm - initial_nm),
      end_label_b = sprintf("%+.1f%%", relative_improvement_percent)
    )

  pa <- ggplot(tbl, aes(x = sweep, y = transit_nm, color = Label, group = Label)) +
    geom_line(linewidth = 0.8) +
    geom_point(size = 2) +
    geom_text(
      aes(label = point_label),
      nudge_y = 95, size = 2.5, lineheight = 0.9,
      color = "black", show.legend = FALSE, na.rm = TRUE
    ) +
    { if (show_end_labels) geom_text(
        data = end_tbl,
        aes(x = sweep, y = transit_nm, label = end_label_a, color = Label),
        hjust = -0.15, size = 2.8, fontface = "bold", show.legend = FALSE
      ) } +
    expand_limits(
      y = max(tbl$transit_nm, na.rm = TRUE) + 250,
      x = x_max + if (show_end_labels) 0.6 else 0
    ) +
    scale_x_continuous(breaks = sort(unique(tbl$sweep))) +
    labs(
      title = "A: Transit Distance", subtitle = subtitle,
      x = "Sweep", y = "Transit distance (nm)", color = "Variant"
    ) +
    theme_bw(base_size = 12) +
    theme(
      legend.position = "bottom",
      plot.title = element_text(face = "bold"),
      plot.subtitle = element_text(size = 9, color = "grey40"),
      panel.grid.minor = element_blank()
    )

  pb <- ggplot(tbl, aes(x = sweep, y = relative_improvement_percent, color = Label, group = Label)) +
    geom_hline(yintercept = 0, color = "grey65", linewidth = 0.4) +
    geom_line(linewidth = 0.8) +
    geom_point(size = 2) +
    { if (show_end_labels) geom_text(
        data = end_tbl,
        aes(x = sweep, y = relative_improvement_percent, label = end_label_b, color = Label),
        hjust = -0.15, size = 2.8, fontface = "bold", show.legend = FALSE
      ) } +
    expand_limits(x = x_max + if (show_end_labels) 0.6 else 0) +
    scale_x_continuous(breaks = sort(unique(tbl$sweep))) +
    labs(
      title = "B: Relative Improvement", subtitle = subtitle,
      x = "Sweep", y = "Improvement from initial transit (%)", color = "Variant"
    ) +
    theme_bw(base_size = 12) +
    theme(
      legend.position = "bottom",
      plot.title = element_text(face = "bold"),
      plot.subtitle = element_text(size = 9, color = "grey40"),
      panel.grid.minor = element_blank()
    )

  leg <- cowplot::get_legend(pa)

  footnote_grob <- cowplot::ggdraw() +
    cowplot::draw_label(
      paste0(
        "All variants use ", subtitle_txt, ". ",
        "End labels: (A) absolute transit improvement from sweep 0; ",
        "(B) relative improvement from initial transit."
      ),
      x = 0.02, y = 0.5, hjust = 0, vjust = 0.5, size = 7.5, color = "grey30"
    )

  cowplot::plot_grid(
    cowplot::plot_grid(
      pa + theme(legend.position = "none"),
      pb + theme(legend.position = "none"),
      ncol = 2, rel_widths = c(1, 1)
    ),
    leg,
    footnote_grob,
    ncol = 1,
    rel_heights = c(1, 0.10, 0.07)
  )
}

# ---------------------------------------------------------------------------
# Generate one line plot per distinct l2seg value
# ---------------------------------------------------------------------------
all_l2seg_plot_vals <- c(
  sort(unique(trajectory_tbl$l2seg[!is.na(trajectory_tbl$l2seg)])),
  if (any(is.na(trajectory_tbl$l2seg))) NA_integer_
)

for (lv in all_l2seg_plot_vals) {
  lv_str <- if (is.na(lv)) "inf" else as.character(lv)
  plt    <- make_sweep_lineplot(lv, trajectory_tbl, show_end_labels)
  if (!is.null(plt)) {
    out <- file.path(sol_dir, sprintf("refinement_transit_sweeps_%s.png", lv_str))
    ggsave(out, plot = plt, width = 11, height = 5.5, dpi = 150)
    cat(sprintf("Plot saved to: %s\n", out))
  }
}
cat("\n")

# ---------------------------------------------------------------------------
# Summary boxplot across ALL l2seg values
# Per sweep: one box per method coloured by Notation; the distribution within
# each box spans the available l2seg runs for that method.
# A line connects per-method means across sweeps.
# ---------------------------------------------------------------------------
all_l2seg_vals <- sort(unique(trajectory_tbl$l2seg[!is.na(trajectory_tbl$l2seg)]))
has_uncapped   <- any(is.na(trajectory_tbl$l2seg))
l2seg_list_str <- paste(
  c(as.character(all_l2seg_vals), if (has_uncapped) "\u221e"),
  collapse = ", "
)
box_subtitle <- bquote(
  "All " * L[2*seg] * " values: " * .(l2seg_list_str) * " s"
)

box_sweeps <- sort(unique(trajectory_tbl$sweep))
box_x_max  <- max(box_sweeps)

# Ensure Notation is a factor in trajectory_tbl (inherits level order from all_runs)
trajectory_tbl$Notation <- factor(
  trajectory_tbl$Notation,
  levels = notation_rows$Notation
)

# Map l2seg values to linetypes for background lines
l2seg_lt_levels <- c(
  as.character(sort(unique(trajectory_tbl$l2seg[!is.na(trajectory_tbl$l2seg)]))),
  if (any(is.na(trajectory_tbl$l2seg))) "Inf"
)
lt_palette <- c("solid", "dashed", "dotted", "dotdash", "longdash", "twodash")
lt_values  <- setNames(
  lt_palette[seq_along(l2seg_lt_levels)],
  l2seg_lt_levels
)
trajectory_tbl <- trajectory_tbl %>%
  dplyr::mutate(
    l2seg_fct = factor(
      ifelse(is.na(l2seg), "Inf", as.character(l2seg)),
      levels = l2seg_lt_levels
    )
  )

# Per-run final improvement (each run's own last sweep, not global max sweep)
final_per_run <- trajectory_tbl %>%
  dplyr::group_by(Notation, source) %>%
  dplyr::filter(sweep == max(sweep)) %>%
  dplyr::ungroup() %>%
  dplyr::left_join(
    trajectory_tbl %>%
      dplyr::filter(sweep == 0) %>%
      dplyr::select(Notation, source, initial_nm = transit_nm),
    by = c("Notation", "source")
  ) %>%
  dplyr::mutate(
    abs_improv = initial_nm - transit_nm,
    rel_improv = 100 * abs_improv / initial_nm
  )

# Summary per method: mean ± SD of improvement across l2seg runs
improv_summary <- final_per_run %>%
  dplyr::group_by(Notation) %>%
  dplyr::summarise(
    mean_abs = mean(abs_improv, na.rm = TRUE),
    sd_abs   = sd(abs_improv, na.rm = TRUE),
    mean_pct = mean(rel_improv, na.rm = TRUE),
    sd_pct   = sd(rel_improv, na.rm = TRUE),
    mean_final_nm  = mean(transit_nm, na.rm = TRUE),
    mean_final_pct = mean(rel_improv, na.rm = TRUE),
    .groups = "drop"
  ) %>%
  dplyr::mutate(
    end_label_a = ifelse(
      is.na(sd_abs),
      sprintf("-%d nm\n%+.1f%%", round(mean_abs), mean_pct),
      sprintf("-%d \u00b1 %d nm\n%+.1f \u00b1 %.1f%%",
              round(mean_abs), round(sd_abs), mean_pct, sd_pct)
    ),
    end_label_b = ifelse(
      is.na(sd_pct),
      sprintf("%+.1f%%", mean_pct),
      sprintf("%+.1f \u00b1 %.1f%%", mean_pct, sd_pct)
    )
  )

end_mean_a <- improv_summary %>% dplyr::mutate(mean_val = mean_final_nm)
end_mean_b <- improv_summary %>% dplyr::mutate(mean_val = mean_final_pct,
                                               end_label = end_label_b)

end_mean_b <- improv_summary %>%
  dplyr::mutate(mean_val = mean_final_pct, end_label = end_label_b)

box_dodge <- position_dodge(width = 0.6)

bplot_a <- ggplot(
  trajectory_tbl,
  aes(x = sweep, y = transit_nm, fill = Notation, color = Notation)
) +
  # Background: one thin line per (method × l2seg), linetype by l2seg
  geom_line(
    aes(group = interaction(Notation, source), linetype = l2seg_fct),
    linewidth = 0.4, alpha = 0.35, inherit.aes = TRUE, show.legend = TRUE
  ) +
  geom_point(
    aes(group = interaction(Notation, source)),
    size = 0.9, alpha = 0.35, inherit.aes = TRUE, show.legend = FALSE
  ) +
  # Foreground: boxplot distribution across l2seg values per method
  geom_boxplot(
    aes(group = interaction(sweep, Notation)),
    position = box_dodge,
    width = 0.5,
    outlier.size = 1.2,
    alpha = 0.55,
    linewidth = 0.5
  ) +
  { if (show_end_labels) geom_text(
      data = end_mean_a,
      aes(x = Inf, y = mean_val, label = end_label_a, color = Notation),
      hjust = 0, vjust = 0.5,
      size = 2.4, lineheight = 0.85,
      inherit.aes = FALSE,
      show.legend = FALSE
    ) } +
  coord_cartesian(clip = "off") +
  scale_x_continuous(breaks = box_sweeps) +
  scale_linetype_manual(
    values = lt_values,
    name   = bquote(L[2*seg] ~ "(s)")
  ) +
  labs(
    title = "A: Transit Distance",
    subtitle = box_subtitle,
    x = "Sweep",
    y = "Transit distance (nm)",
    fill = "Variant", color = "Variant"
  ) +
  theme_bw(base_size = 12) +
  theme(
    legend.position = "bottom",
    plot.title    = element_text(face = "bold"),
    plot.subtitle = element_text(size = 9, color = "grey40"),
    panel.grid.minor = element_blank(),
    plot.margin = margin(t = 5, r = 90, b = 5, l = 5, unit = "pt")
  )

bplot_b <- ggplot(
  trajectory_tbl,
  aes(x = sweep, y = relative_improvement_percent, fill = Notation, color = Notation)
) +
  geom_hline(yintercept = 0, color = "grey65", linewidth = 0.4) +
  # Background: one thin line per (method × l2seg), linetype by l2seg
  geom_line(
    aes(group = interaction(Notation, source), linetype = l2seg_fct),
    linewidth = 0.4, alpha = 0.35, inherit.aes = TRUE, show.legend = TRUE
  ) +
  geom_point(
    aes(group = interaction(Notation, source)),
    size = 0.9, alpha = 0.35, inherit.aes = TRUE, show.legend = FALSE
  ) +
  # Foreground: boxplot distribution across l2seg values per method
  geom_boxplot(
    aes(group = interaction(sweep, Notation)),
    position = box_dodge,
    width = 0.5,
    outlier.size = 1.2,
    alpha = 0.55,
    linewidth = 0.5
  ) +
  { if (show_end_labels) geom_text(
      data = end_mean_b,
      aes(x = Inf, y = mean_val, label = end_label, color = Notation),
      hjust = 0, vjust = 0.5,
      size = 2.4, lineheight = 0.85,
      inherit.aes = FALSE,
      show.legend = FALSE
    ) } +
  coord_cartesian(clip = "off") +
  scale_x_continuous(breaks = box_sweeps) +
  scale_linetype_manual(
    values = lt_values,
    name   = bquote(L[2*seg] ~ "(s)")
  ) +
  labs(
    title = "B: Relative Improvement",
    subtitle = box_subtitle,
    x = "Sweep",
    y = "Improvement from initial transit (%)",
    fill = "Variant", color = "Variant"
  ) +
  theme_bw(base_size = 12) +
  theme(
    legend.position = "bottom",
    plot.title    = element_text(face = "bold"),
    plot.subtitle = element_text(size = 9, color = "grey40"),
    panel.grid.minor = element_blank(),
    plot.margin = margin(t = 5, r = 90, b = 5, l = 5, unit = "pt")
  )

box_legend <- cowplot::get_legend(bplot_a)

box_footnote_text <- paste0(
  "Faint lines show individual trajectories for each L\u2082seg value per variant. ",
  "Boxes summarise the distribution across L\u2082seg values at each sweep; whiskers are 1.5\u00d7IQR. ",
  if (show_end_labels) paste0(
    "End labels: expected improvement = mean absolute change \u00f7 mean initial transit. ",
    "(A) relative (\u0025) and absolute (nm); (B) relative (\u0025)."
  )
)
box_footnote_grob <- cowplot::ggdraw() +
  cowplot::draw_label(
    box_footnote_text,
    x = 0.02, y = 0.5,
    hjust = 0, vjust = 0.5,
    size = 7.5,
    color = "grey30"
  )

box_plot <- cowplot::plot_grid(
  cowplot::plot_grid(
    bplot_a + theme(legend.position = "none"),
    bplot_b + theme(legend.position = "none"),
    ncol = 2, rel_widths = c(1, 1)
  ),
  box_legend,
  box_footnote_grob,
  ncol = 1,
  rel_heights = c(1, 0.10, 0.08)
)

box_plot_file <- file.path(sol_dir, "refinement_transit_sweeps.png")
ggsave(box_plot_file, plot = box_plot, width = 11, height = 5.5, dpi = 150)
cat(sprintf("Boxplot saved to: %s\n\n", box_plot_file))

# ---------------------------------------------------------------------------
# Metadata summary
# ---------------------------------------------------------------------------
metadata_tbl <- lapply(seq_len(nrow(all_runs)), function(i) {
  json <- read_result_json(all_runs$method[i], all_runs$source[i])
  if (is.null(json)) return(NULL)
  tibble::tibble(
    method = all_runs$method[i],
    source = all_runs$source[i],
    mip_time_limit_seconds = json$metadata$mip_time_limit_seconds,
    max_iterations = json$metadata$max_iterations,
    global_time_limit_seconds = json$metadata$global_time_limit_seconds
  )
}) %>%
  dplyr::bind_rows()

latex_table <- knitr::kable(
  refinement_tbl,
  format = "latex",
  booktabs = TRUE,
  escape = FALSE,
  linesep = "",
  label = "refinement-summary",
  caption = "Refinement outcomes by initialization strategy. Distances report transit distance only; the common station towing distance is excluded. Improvement is relative to the initial segmented baseline for each strategy. Stations moved are reported as mean/median/max per sweep pass, and MIP solves are local two-segment C-MIP boundary solves."
)

cat(latex_table)
cat("\n\n")

if (nrow(metadata_tbl) > 0) {
  unique_limits <- unique(metadata_tbl$mip_time_limit_seconds)
  unique_max_iterations <- unique(metadata_tbl$max_iterations)
  unique_global_limits <- unique(metadata_tbl$global_time_limit_seconds)

  # Treat 0 as uncapped (Inf) in the prose
  fmt_limits <- ifelse(unique_limits == 0, "$\\infty$", as.character(unique_limits))

  cat("\\noindent ")
  cat(sprintf(
    "All refinement runs use $L_{2\\text{seg}}=%s$ seconds, a maximum of %s sweep iterations, and a global workflow limit of %s hours.",
    paste(fmt_limits, collapse = "/"),
    paste(unique_max_iterations, collapse = "/"),
    paste(format(round(unique_global_limits / 3600, 1), trim = TRUE), collapse = "/")
  ))
  cat("\n")
}

cat("\\noindent In Figure~\\ref{fig:refinement-transit-sweeps}, point labels report the sweep-level diagnostics: moved is the number of stations reassigned across segment boundaries, and accepted is the number of accepted boundary-improvement attempts divided by all boundary attempts in that sweep.\n")
