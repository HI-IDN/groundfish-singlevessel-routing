#!/usr/bin/env Rscript
# make_refinement_table.R
#
# Builds a LaTeX summary table from configured refinement JSON files.
# Uses config/gsp_solver.yaml sweep.l2seg_variants and omits missing results.
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

load_required_packages(c("jsonlite", "dplyr", "ggplot2", "cowplot", "knitr", "scales"))

script_dir <- tryCatch(
  dirname(normalizePath(sys.frame(1)$ofile)),
  error = function(e) getwd()
)
sol_dir <- normalizePath(file.path(script_dir, "..", "sol"), mustWork = FALSE)
config_file <- normalizePath(file.path(script_dir, "..", "config", "gsp_solver.yaml"), mustWork = FALSE)

if (!dir.exists(sol_dir)) {
  sol_dir <- "sol"
}
if (!file.exists(config_file)) {
  config_file <- file.path("config", "gsp_solver.yaml")
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
# Read configured refinement methods and l2seg values.
# This intentionally avoids a YAML dependency because the needed config values
# are simple lists.
# ---------------------------------------------------------------------------
read_yaml_list <- function(path, key) {
  if (!file.exists(path)) return(character(0))
  lines <- readLines(path, warn = FALSE)
  line <- lines[grepl(sprintf("^\\s*%s\\s*:", key), lines)]
  if (length(line) == 0) return(character(0))
  value <- sub("^[^:]+:\\s*", "", line[1])
  value <- sub("\\s*#.*$", "", value)
  value <- gsub("^\\[|\\]$", "", trimws(value))
  if (!nzchar(value)) return(character(0))
  trimws(strsplit(value, ",", fixed = TRUE)[[1]])
}

configured_l2seg_variants <- function() {
  vals <- suppressWarnings(as.integer(read_yaml_list(config_file, "l2seg_variants")))
  vals <- vals[!is.na(vals)]
  if (!length(vals)) {
    warning("No sweep.l2seg_variants found in config; falling back to existing refinement JSON files.", call. = FALSE)
  }
  vals
}

configured_methods <- function() {
  if (!file.exists(config_file)) return(notation_rows$method)
  lines <- readLines(config_file, warn = FALSE)
  strategies_start <- grep("^\\s*strategies\\s*:", lines)
  if (!length(strategies_start)) return(notation_rows$method)

  tail_lines <- lines[(strategies_start[1] + 1L):length(lines)]
  strategy_lines <- tail_lines[grepl("^\\s*-\\s+", tail_lines)]
  if (!length(strategy_lines)) return(notation_rows$method)

  methods <- trimws(sub("^\\s*-\\s*", "", strategy_lines))
  methods <- sub("\\s*#.*$", "", methods)
  methods[methods %in% notation_rows$method]
}

# ---------------------------------------------------------------------------
# Discover configured refinement JSON sources for a given method directory.
# Returns a tibble: source (file stem), l2seg (integer or NA = uncapped).
# Sorted: numbered ascending, uncapped (NA) last.
# ---------------------------------------------------------------------------
find_refinement_sources <- function(method) {
  method_dir <- file.path(sol_dir, method)
  configured_l2seg <- configured_l2seg_variants()

  if (length(configured_l2seg)) {
    candidates <- tibble::tibble(
      source = sprintf("refinement_%d", configured_l2seg),
      l2seg = configured_l2seg,
      path = file.path(method_dir, sprintf("refinement_%d.json", configured_l2seg))
    )

    existing <- candidates %>% dplyr::filter(file.exists(path))
    missing <- candidates %>% dplyr::filter(!file.exists(path))
    if (nrow(existing) > 0 && nrow(missing) > 0) {
      warning(
        sprintf(
          "Omitting missing configured refinement results for %s: %s",
          method,
          paste(sprintf("%s.json", missing$source), collapse = ", ")
        ),
        call. = FALSE
      )
    }

    return(existing %>%
      dplyr::select(source, l2seg) %>%
      dplyr::arrange(l2seg))
  }

  files <- list.files(method_dir, pattern = "^refinement(_\\d+)?\\.json$", full.names = FALSE)
  if (length(files) == 0) return(tibble::tibble(source = character(0), l2seg = integer(0)))

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
    accepted_solve_ratio = ifelse(
      is.na(accepted) | is.na(capacity_solves) | capacity_solves <= 0,
      0,
      pmin(1, pmax(0, accepted / capacity_solves))
    ),
    mip_solves = mip_solves,
    moved_label = ifelse(is.na(moved), NA_character_, as.character(moved)),
    accepted_label = ifelse(
      is.na(moved),
      NA_character_,
      sprintf("a: %d/%d", accepted, capacity_solves)
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
  if (!(method %in% configured_methods())) {
    return(NULL)
  }
  srcs <- find_refinement_sources(method)
  if (nrow(srcs) == 0) {
    return(NULL)
  }
  cbind(notation_rows[i, ], srcs)
}) %>%
  dplyr::bind_rows()

if (nrow(all_runs) == 0) {
  message("No refinement JSON files found; skipping refinement table and plots.")
  quit(save = "no", status = 0)
}

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
# Shared sweep plot helpers
# ---------------------------------------------------------------------------

trajectory_tbl$Notation <- factor(
  trajectory_tbl$Notation,
  levels = notation_rows$Notation
)

l2seg_lt_levels <- c(
  as.character(sort(unique(trajectory_tbl$l2seg[!is.na(trajectory_tbl$l2seg)]))),
  if (any(is.na(trajectory_tbl$l2seg))) "Inf"
)

lt_palette <- c("solid", "dashed", "dotted", "dotdash", "longdash", "twodash")

lt_values <- setNames(
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

sweep_segment_rows <- function(tbl, y_col, group_cols) {
  x_span <- max(tbl$sweep, na.rm = TRUE) - min(tbl$sweep, na.rm = TRUE)
  y_vals <- tbl[[y_col]]
  y_span <- max(y_vals, na.rm = TRUE) - min(y_vals, na.rm = TRUE)

  if (!is.finite(x_span) || x_span == 0) x_span <- 1
  if (!is.finite(y_span) || y_span == 0) y_span <- 1

  tbl %>%
    dplyr::group_by(dplyr::across(dplyr::all_of(group_cols))) %>%
    dplyr::arrange(sweep, .by_group = TRUE) %>%
    dplyr::mutate(
      prev_sweep = dplyr::lag(sweep),
      prev_y = dplyr::lag(.data[[y_col]]),
      current_y = .data[[y_col]],
      label_x = (prev_sweep + sweep) / 2,
      label_y = (prev_y + current_y) / 2 + 0.025 * y_span,
      label_angle = atan2(
        (current_y - prev_y) / y_span,
        (sweep - prev_sweep) / x_span
      ) * 180 / pi
    ) %>%
    dplyr::ungroup() %>%
    dplyr::filter(
      sweep > 0,
      !is.na(prev_y),
      !is.na(current_y)
    )
}

sweep_station_size_scale <- function(range = c(2, 7), name = "Stations moved") {
  scale_size_continuous(
    name = name,
    range = range,
    breaks = scales::breaks_pretty(n = 4)
  )
}

sweep_linewidth_scale <- function() {
  scale_linewidth_continuous(
    name = "Accepted solve ratio",
    range = c(0.25, 2.2),
    limits = c(0, 1),
    breaks = c(0, 0.5, 1),
    labels = scales::label_percent(accuracy = 1)
  )
}

sweep_guides <- function(include_linetype = FALSE) {
  guide_list <- list(
    fill = "none",
    color = guide_legend(
      order = 1,
      override.aes = list(linetype = "solid", linewidth = 0.9, alpha = 1)
    ),
    size = guide_legend(order = 3),
    linewidth = guide_legend(order = 4)
  )

  if (include_linetype) {
    guide_list$linetype <- guide_legend(
      order = 2,
      override.aes = list(shape = NA, linewidth = 0.8, alpha = 1, color = "grey30")
    )
  }

  do.call(guides, guide_list)
}

sweep_legends <- function(plot) {
  variant_legend <- cowplot::get_legend(
    plot +
      guides(size = "none", linewidth = "none") +
      theme(legend.position = "bottom", legend.direction = "horizontal")
  )

  diagnostic_legend <- cowplot::get_legend(
    plot +
      guides(color = "none", fill = "none", linetype = "none") +
      theme(legend.position = "bottom", legend.direction = "horizontal")
  )

  list(variant = variant_legend, diagnostic = diagnostic_legend)
}

sweep_footnote_grob <- function(label, fontsize = 7.2, x = 0.02, hjust = 0) {
  cowplot::ggdraw() +
    cowplot::draw_grob(
      grid::textGrob(
        label,
        x = grid::unit(x, "npc"),
        y = grid::unit(0.5, "npc"),
        hjust = hjust,
        vjust = 0.5,
        gp = grid::gpar(fontsize = fontsize, col = "grey30")
      )
    )
}

make_end_labels <- function(tbl, combined = FALSE) {
  group_cols <- if (combined) c("Notation", "source") else c("Label")

  final_per_run <- tbl %>%
    dplyr::group_by(dplyr::across(dplyr::all_of(group_cols))) %>%
    dplyr::filter(sweep == max(sweep, na.rm = TRUE)) %>%
    dplyr::ungroup() %>%
    dplyr::left_join(
      tbl %>%
        dplyr::filter(sweep == 0) %>%
        dplyr::select(dplyr::all_of(group_cols), initial_nm = transit_nm),
      by = group_cols
    ) %>%
    dplyr::mutate(
      abs_improv = initial_nm - transit_nm,
      rel_improv = 100 * abs_improv / initial_nm
    )

  if (!combined) {
    return(
      final_per_run %>%
        dplyr::mutate(
          end_y_a = transit_nm,
          end_y_b = rel_improv,
          end_label_a = sprintf("-%d nm", round(abs_improv)),
          end_label_b = sprintf("%+.1f%%", rel_improv)
        )
    )
  }

  final_per_run %>%
    dplyr::group_by(Notation) %>%
    dplyr::summarise(
      end_y_a = mean(transit_nm, na.rm = TRUE),
      end_y_b = mean(rel_improv, na.rm = TRUE),
      mean_abs = mean(abs_improv, na.rm = TRUE),
      sd_abs = sd(abs_improv, na.rm = TRUE),
      mean_pct = mean(rel_improv, na.rm = TRUE),
      sd_pct = sd(rel_improv, na.rm = TRUE),
      .groups = "drop"
    ) %>%
    dplyr::mutate(
      end_label_a = ifelse(
        is.na(sd_abs),
        sprintf("-%d nm", round(mean_abs)),
        sprintf("-%d ± %d nm", round(mean_abs), round(sd_abs))
      ),
      end_label_b = ifelse(
        is.na(sd_pct),
        sprintf("%+.1f%%", mean_pct),
        sprintf("%+.1f ± %.1f%%", mean_pct, sd_pct)
      )
    )
}

make_sweep_panel <- function(
  tbl,
  segment_tbl,
  y_col,
  title,
  y_label,
  color_col,
  subtitle = NULL,
  include_hline = FALSE,
  include_linetype = FALSE,
  include_sweep_annotations = FALSE,
  end_tbl = NULL,
  end_y_col = NULL,
  end_label_col = NULL,
  size_range = c(2, 7),
  point_alpha = 0.75,
  line_alpha = 0.9,
  y_expand = NULL,
  right_margin = 90
) {
  p <- ggplot(
    tbl,
    aes(
      x = sweep,
      y = .data[[y_col]],
      fill = .data[[color_col]],
      color = .data[[color_col]]
    )
  )

  if (include_hline) {
    p <- p + geom_hline(yintercept = 0, color = "grey65", linewidth = 0.4)
  }

  if (include_linetype) {
    p <- p +
      geom_segment(
        data = segment_tbl,
        aes(
          x = prev_sweep,
          xend = sweep,
          y = prev_y,
          yend = current_y,
          color = .data[[color_col]],
          linetype = l2seg_fct,
          linewidth = accepted_solve_ratio
        ),
        inherit.aes = FALSE,
        alpha = line_alpha,
        lineend = "round",
        na.rm = TRUE,
        show.legend = c(color = TRUE, linetype = TRUE, linewidth = TRUE)
      ) +
      scale_linetype_manual(
        values = lt_values,
        name = bquote(L[2*seg] ~ "(s)")
      )
  } else {
    p <- p +
      geom_segment(
        data = segment_tbl,
        aes(
          x = prev_sweep,
          xend = sweep,
          y = prev_y,
          yend = current_y,
          color = .data[[color_col]],
          linewidth = accepted_solve_ratio
        ),
        inherit.aes = FALSE,
        alpha = line_alpha,
        lineend = "round",
        na.rm = TRUE
      )
  }

  p <- p +
    geom_point(
      aes(size = stations_moved, group = interaction(.data[[color_col]], source)),
      alpha = point_alpha,
      na.rm = TRUE,
      show.legend = c(color = FALSE, size = TRUE)
    )

  if (include_sweep_annotations) {
    p <- p +
      geom_text(
        aes(label = moved_label),
        size = 2.3,
        color = "black",
        show.legend = FALSE,
        na.rm = TRUE
      ) +
      geom_text(
        data = segment_tbl,
        aes(
          x = label_x,
          y = label_y,
          label = accepted_label,
          angle = label_angle
        ),
        inherit.aes = FALSE,
        size = 2.3,
        lineheight = 0.9,
        color = "black",
        show.legend = FALSE,
        check_overlap = TRUE,
        na.rm = TRUE
      )
  }

  if (!is.null(end_tbl)) {
    p <- p +
      geom_text(
        data = end_tbl,
        aes(
          x = Inf,
          y = .data[[end_y_col]],
          label = .data[[end_label_col]],
          color = .data[[color_col]]
        ),
        hjust = 0,
        vjust = 0.5,
        size = 2.6,
        fontface = "bold",
        inherit.aes = FALSE,
        show.legend = FALSE
      )
  }

  p <- p +
    coord_cartesian(clip = "off") +
    scale_x_continuous(breaks = sort(unique(tbl$sweep))) +
    sweep_station_size_scale(range = size_range) +
    sweep_linewidth_scale() +
    labs(
      title = title,
      subtitle = subtitle,
      x = "Sweep",
      y = y_label,
      fill = "Variant",
      color = "Variant"
    ) +
    sweep_guides(include_linetype = include_linetype) +
    theme_bw(base_size = 12) +
    theme(
      legend.position = "bottom",
      plot.title = element_text(face = "bold"),
      plot.subtitle = element_text(size = 9, color = "grey40"),
      panel.grid.minor = element_blank(),
      plot.margin = margin(t = 5, r = right_margin, b = 5, l = 5, unit = "pt")
    )

  if (!is.null(y_expand)) {
    p <- p + expand_limits(y = y_expand)
  }

  p
}

make_refinement_sweep_plot <- function(
  traj_tbl,
  l2seg_filter = NULL,
  annotate_sweeps = FALSE
) {
  tbl <- traj_tbl

  if (!is.null(l2seg_filter)) {
    tbl <- tbl %>%
      dplyr::filter(
        if (is.na(l2seg_filter)) is.na(l2seg) else !is.na(l2seg) & l2seg == l2seg_filter
      )
  }

  if (nrow(tbl) == 0) {
    warning("No trajectory rows for requested plot.")
    return(NULL)
  }

  combined <- is.null(l2seg_filter)

  segment_group_cols <- if (combined) c("Notation", "source") else c("Label")

  segment_a <- sweep_segment_rows(tbl, "transit_nm", segment_group_cols)
  segment_b <- sweep_segment_rows(tbl, "relative_improvement_percent", segment_group_cols)

  end_tbl <- make_end_labels(tbl, combined = combined)

  color_col <- if (combined) "Notation" else "Label"

  p_a <- make_sweep_panel(
    tbl = tbl,
    segment_tbl = segment_a,
    y_col = "transit_nm",
    title = "A: Transit Distance",
    y_label = "Transit distance (nm)",
    color_col = color_col,
    include_linetype = TRUE,
    include_sweep_annotations = annotate_sweeps,
    end_tbl = end_tbl,
    end_y_col = "end_y_a",
    end_label_col = "end_label_a",
    size_range = if (combined) c(0.5, 4) else c(2, 7),
    point_alpha = 0.75,
    line_alpha = 0.9
  )

  p_b <- make_sweep_panel(
    tbl = tbl,
    segment_tbl = segment_b,
    y_col = "relative_improvement_percent",
    title = "B: Relative Improvement",
    y_label = "Improvement from initial transit (%)",
    color_col = color_col,
    subtitle = NULL,
    include_hline = TRUE,
    include_linetype = TRUE,
    include_sweep_annotations = annotate_sweeps,
    end_tbl = end_tbl,
    end_y_col = "end_y_b",
    end_label_col = "end_label_b",
    size_range = if (combined) c(0.5, 4) else c(2, 7),
    point_alpha = 0.75,
    line_alpha = 0.9
  )

  legends <- sweep_legends(p_a)

  footnote <- if (combined) {
    sweep_footnote_grob(
      paste0(
        "Lines show individual trajectories for each time-limit value; ",
        "line width and point size are proportional to the accepted/total ratio and the number of stations moved in that sweep, respectively.\n",
        "Right-margin labels show mean \u00b1 SD improvement across time-limit runs: absolute for A, relative for B."
      )
    )
  } else {
    sweep_footnote_grob(
      paste0(
        "Line width and point size are proportional to the accepted/total ratio and the number of stations moved in that sweep, respectively. ",
        "The ratio is explicitly given by the 'a: accepted/total' labels for two-segment boundary change attempts.\n",
        "End labels show total transit reduction from sweep 0: absolute for A, relative for B."
      )
    )
  }

  cowplot::plot_grid(
    cowplot::plot_grid(
      p_a + theme(legend.position = "none"),
      p_b + theme(legend.position = "none"),
      ncol = 2,
      rel_widths = c(1, 1)
    ),
    legends$variant,
    legends$diagnostic,
    footnote,
    ncol = 1,
    rel_heights = c(1, 0.08, 0.10, 0.09)
  )
}

# ---------------------------------------------------------------------------
# Generate plots
# ---------------------------------------------------------------------------

all_l2seg_plot_vals <- c(
  sort(unique(trajectory_tbl$l2seg[!is.na(trajectory_tbl$l2seg)])),
  if (any(is.na(trajectory_tbl$l2seg))) NA_integer_
)

combined_plot <- make_refinement_sweep_plot(
  trajectory_tbl,
  l2seg_filter = NULL,
  annotate_sweeps = FALSE
)

if (!is.null(combined_plot)) {
  out <- file.path(sol_dir, "refinement_sweeps.png")
  ggsave(out, plot = combined_plot, width = 11, height = 5.5, dpi = 150, bg = "white")
  cat(sprintf("Plot saved to: %s\n", out))
}

invisible(lapply(all_l2seg_plot_vals, function(lv) {
  lv_str <- if (is.na(lv)) "inf" else as.character(lv)

  p <- make_refinement_sweep_plot(
    trajectory_tbl,
    l2seg_filter = lv,
    annotate_sweeps = TRUE
  )

  if (!is.null(p)) {
    out <- file.path(sol_dir, sprintf("refinement_sweeps_%s.png", lv_str))
    ggsave(out, plot = p, width = 11, height = 5.5, dpi = 150, bg = "white")
    cat(sprintf("Plot saved to: %s\n", out))
  }
}))

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
