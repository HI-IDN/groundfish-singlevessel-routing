#!/usr/bin/env Rscript
# plot_mip_runtime.R
#
# Reads construction.json, segment.json and all refinement_<l2seg>.json for
# each method, extracts every individual MIP solve's runtime and gap, and
# produces plots of solve-time distributions annotated by optimality gap.
#
# Output: sol/mip_runtime_vs_size.png
#
# Run from the project root:
#   Rscript R/plot_mip_runtime.R

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

load_required_packages(c("jsonlite", "dplyr", "ggplot2", "cowplot", "scales"))

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
script_dir <- tryCatch(
  dirname(normalizePath(sys.frame(1)$ofile)),
  error = function(e) getwd()
)
sol_dir <- normalizePath(file.path(script_dir, "..", "sol"), mustWork = FALSE)
if (!dir.exists(sol_dir)) sol_dir <- "sol"
cat(sprintf("sol_dir: %s\n", sol_dir))

# ---------------------------------------------------------------------------
# Methods to process
# ---------------------------------------------------------------------------
methods <- c("nn", "ci", "ge", "noport", "fixedport")

# ---------------------------------------------------------------------------
# Discover all refinement_<l2seg>.json files for a method directory.
# Returns a data.frame with columns: path, l2seg (integer or NA = uncapped).
# ---------------------------------------------------------------------------
find_refinement_jsons <- function(method_dir) {
  files <- list.files(
    method_dir,
    pattern = "^refinement(_\\d+)?\\.json$",
    full.names = TRUE
  )
  if (length(files) == 0) return(data.frame(path = character(0), l2seg = integer(0)))
  stems <- gsub("\\.json$", "", basename(files))
  l2seg <- suppressWarnings(as.integer(gsub("^refinement_(\\d+)$", "\\1", stems)))
  data.frame(path = files, l2seg = l2seg, stringsAsFactors = FALSE)
}

# ---------------------------------------------------------------------------
# Extract MIP solves from a single JSON file.
# Returns a data.frame with one row per solve, including gap_percent.
# ---------------------------------------------------------------------------
extract_mip_solves <- function(json_path, method, source_label, l2seg = NA_integer_) {
  if (!file.exists(json_path)) return(NULL)

  json <- tryCatch(
    jsonlite::fromJSON(json_path, simplifyVector = FALSE),
    error = function(e) {
      warning(sprintf("Failed to parse %s: %s", json_path, e$message))
      NULL
    }
  )
  if (is.null(json) || is.null(json$mip)) return(NULL)

  mip         <- json$mip
  tuple_names <- unlist(mip$solve_detail_tuple)
  solves      <- mip$solves
  if (length(solves) == 0) return(NULL)

  # Column indices differ by phase
  if (source_label %in% c("construction", "segment")) {
    size_col <- which(tuple_names == "size")
  } else {
    size_col <- which(tuple_names == "station_count")
  }
  rt_col  <- which(tuple_names == "runtime_seconds")
  gap_col <- which(tuple_names == "gap_percent")

  missing_cols <- c(
    if (length(size_col) == 0) "size/station_count",
    if (length(rt_col)   == 0) "runtime_seconds",
    if (length(gap_col)  == 0) "gap_percent"
  )
  if (length(missing_cols) > 0) {
    warning(sprintf("Missing columns in %s: %s", json_path, paste(missing_cols, collapse = ", ")))
    return(NULL)
  }

  df <- data.frame(
    method          = method,
    source          = source_label,
    l2seg           = l2seg,
    problem_size    = vapply(solves, function(s) as.numeric(s[[size_col]]),  numeric(1)),
    runtime_seconds = vapply(solves, function(s) as.numeric(s[[rt_col]]),    numeric(1)),
    gap_percent     = vapply(solves, function(s) as.numeric(s[[gap_col]]),   numeric(1)),
    stringsAsFactors = FALSE
  )
  df$gap_zero <- df$gap_percent == 0
  df
}

# ---------------------------------------------------------------------------
# Collect data for all methods
# ---------------------------------------------------------------------------
all_data <- list()

for (method in methods) {
  method_dir <- file.path(sol_dir, method)

  # Construction + segment (single file each)
  for (src in c("construction", "segment")) {
    fname <- sprintf("%s.json", src)
    d <- extract_mip_solves(file.path(method_dir, fname), method, src)
    if (!is.null(d)) {
      cat(sprintf("  [%-10s] %-20s: %d solves\n", method, src, nrow(d)))
      all_data <- c(all_data, list(d))
    }
  }

  # All refinement_<l2seg>.json files
  ref_jsons <- find_refinement_jsons(method_dir)
  for (i in seq_len(nrow(ref_jsons))) {
    lv    <- ref_jsons$l2seg[i]
    label <- if (is.na(lv)) "refinement (Inf)" else sprintf("refinement (%ds)", lv)
    d <- extract_mip_solves(ref_jsons$path[i], method, "refinement", lv)
    if (!is.null(d)) {
      cat(sprintf("  [%-10s] %-20s: %d solves\n", method, label, nrow(d)))
      all_data <- c(all_data, list(d))
    }
  }
}

if (length(all_data) == 0) {
  stop("No MIP data found in any of the method directories.")
}

df <- dplyr::bind_rows(all_data)
df$method <- factor(df$method, levels = methods)
df$source <- factor(df$source, levels = c("construction", "segment", "refinement"))
# Ordered refinement label for legends (finite l2seg ascending, then Inf)
df <- df %>%
  dplyr::mutate(
    l2seg_str = dplyr::case_when(
      source != "refinement" ~ as.character(source),
      is.na(l2seg)           ~ "refinement (\u221e s)",
      TRUE                   ~ sprintf("refinement (%d s)", l2seg)
    )
  )

cat(sprintf("\nTotal MIP solves collected: %d\n", nrow(df)))
cat(sprintf("  Gap = 0 (optimal):    %d (%.1f%%)\n",
            sum(df$gap_zero, na.rm = TRUE),
            100 * mean(df$gap_zero, na.rm = TRUE)))
cat(sprintf("  Gap > 0 (timed-out):  %d (%.1f%%)\n",
            sum(!df$gap_zero, na.rm = TRUE),
            100 * mean(!df$gap_zero, na.rm = TRUE)))

# ---------------------------------------------------------------------------
# Summary table: count / mean / max runtime per source × l2seg × gap
# ---------------------------------------------------------------------------
summary_tbl <- df %>%
  dplyr::mutate(
    phase = dplyr::case_when(
      source != "refinement"  ~ as.character(source),
      is.na(l2seg)            ~ "refinement (\u221e s)",
      TRUE                    ~ sprintf("refinement (%d s)", l2seg)
    ),
    gap_group = ifelse(gap_zero, "gap=0", "gap>0")
  ) %>%
  dplyr::group_by(phase, gap_group) %>%
  dplyr::summarise(
    n           = dplyr::n(),
    mean_rt_s   = mean(runtime_seconds, na.rm = TRUE),
    max_rt_s    = max(runtime_seconds,  na.rm = TRUE),
    mean_gap    = mean(gap_percent,     na.rm = TRUE),
    max_gap     = max(gap_percent,      na.rm = TRUE),
    .groups     = "drop"
  )

summary_overall <- df %>%
  dplyr::mutate(
    phase = dplyr::case_when(
      source != "refinement"  ~ as.character(source),
      is.na(l2seg)            ~ "refinement (\u221e s)",
      TRUE                    ~ sprintf("refinement (%d s)", l2seg)
    )
  ) %>%
  dplyr::group_by(phase) %>%
  dplyr::summarise(
    gap_group = "overall",
    n         = dplyr::n(),
    mean_rt_s = mean(runtime_seconds, na.rm = TRUE),
    max_rt_s  = max(runtime_seconds,  na.rm = TRUE),
    mean_gap  = mean(gap_percent,     na.rm = TRUE),
    max_gap   = max(gap_percent,      na.rm = TRUE),
    .groups   = "drop"
  )

# Phase ordering: construction first, segment second, then refinement variants sorted
phase_order <- c(
  "construction",
  "segment",
  sort(unique(df$l2seg_str[df$source == "refinement"]))
)
gap_order <- c("overall", "gap=0", "gap>0")

summary_full <- dplyr::bind_rows(summary_overall, summary_tbl) %>%
  dplyr::mutate(
    phase     = factor(phase,     levels = phase_order),
    gap_group = factor(gap_group, levels = gap_order)
  ) %>%
  dplyr::arrange(phase, gap_group)

cat("\n--- MIP solve summary (count / mean / max runtime and gap) ---\n")
cat(sprintf("%-30s  %-8s  %6s  %9s  %9s  %9s  %9s\n",
            "Phase", "Gap", "n", "mean_rt", "max_rt", "mean_gap%", "max_gap%"))
cat(strrep("-", 90), "\n")
for (i in seq_len(nrow(summary_full))) {
  r <- summary_full[i, ]
  cat(sprintf("%-30s  %-8s  %6d  %9.1f  %9.1f  %9.1f  %9.1f\n",
              as.character(r$phase), as.character(r$gap_group),
              r$n, r$mean_rt_s, r$max_rt_s, r$mean_gap, r$max_gap))
}
cat("\n")

# ---------------------------------------------------------------------------
# Colour / shape helpers
# ---------------------------------------------------------------------------
phase_colors <- c(
  "segment"    = "#1b9e77",
  "refinement" = "#d95f02"
)

gap_colors <- c(
  "TRUE"  = "#2166ac",   # optimal  (gap = 0)
  "FALSE" = "#d73027"    # timed-out (gap > 0)
)
gap_labels <- c("TRUE" = "Gap = 0 (optimal)", "FALSE" = "Gap > 0 (timed-out)")
gap_shapes <- c("TRUE" = 16L, "FALSE" = 4L)

df_fit <- dplyr::filter(df, source %in% c("segment", "refinement"))

# Reference lines (time limits commonly used)
ref_lines <- data.frame(
  xintercept = c(60, 120, 180),
  label      = c("60 s", "120 s", "180 s")
)

# ---------------------------------------------------------------------------
# Plot A: ECDF — solid = gap 0, dashed = gap > 0
# ---------------------------------------------------------------------------
p_a <- ggplot(df_fit,
              aes(x = runtime_seconds,
                  color    = source,
                  linetype = factor(gap_zero),
                  group    = interaction(source, gap_zero))) +
  geom_vline(data = ref_lines,
             aes(xintercept = xintercept),
             color = "grey50", linetype = "dotted", linewidth = 0.6,
             inherit.aes = FALSE) +
  geom_text(data = ref_lines,
            aes(x = xintercept, label = label),
            y = 0.05, hjust = -0.1, vjust = 0, angle = 90,
            color = "grey40", size = 3, inherit.aes = FALSE) +
  stat_ecdf(geom = "step", linewidth = 0.85, pad = FALSE) +
  scale_color_manual(name = "Phase", values = phase_colors) +
  scale_linetype_manual(
    name   = "Optimality",
    values = c("TRUE" = "solid", "FALSE" = "dashed"),
    labels = gap_labels
  ) +
  scale_x_log10(
    breaks = trans_breaks("log10", function(x) 10^x),
    labels = trans_format("log10", math_format(10^.x))
  ) +
  scale_y_continuous(
    labels = percent_format(accuracy = 1),
    breaks = seq(0, 1, 0.1)
  ) +
  labs(
    title    = "A: MIP Solve Time Distribution",
    subtitle = "ECDF by phase \u2014 solid = gap 0 (optimal), dashed = gap > 0 (timed-out)",
    x        = "Solve time (seconds, log scale)",
    y        = "Cumulative fraction of solves"
  ) +
  theme_bw(base_size = 12) +
  theme(
    legend.position  = "right",
    plot.title       = element_text(face = "bold"),
    panel.grid.minor = element_blank()
  )

# ---------------------------------------------------------------------------
# Plot B: runtime boxplot per method × source, jitter coloured by gap
# ---------------------------------------------------------------------------
phase_fill <- c(
  "construction" = "#7570b3",
  "segment"      = "#1b9e77",
  "refinement"   = "#d95f02"
)

df_b <- dplyr::filter(df, method %in% c("nn", "ci", "ge", "noport")) %>%
  dplyr::mutate(method = factor(method, levels = c("nn", "ci", "ge", "noport")))

dodge <- position_dodge(width = 0.75)

p_b <- ggplot(df_b, aes(x = method, y = runtime_seconds, fill = source)) +
  # Reference lines at common time limits
  geom_hline(data = ref_lines,
             aes(yintercept = xintercept),
             color = "grey50", linetype = "dotted", linewidth = 0.6,
             inherit.aes = FALSE) +
  geom_text(data = ref_lines,
            aes(y = xintercept, label = label),
            x = 0.4, hjust = 0, vjust = -0.3,
            color = "grey40", size = 3, inherit.aes = FALSE) +
  geom_boxplot(
    outlier.shape = NA,
    position      = dodge,
    width         = 0.6,
    alpha         = 0.55,
    linewidth     = 0.45
  ) +
  geom_jitter(
    aes(color = factor(gap_zero), shape = factor(gap_zero)),
    position  = position_jitterdodge(jitter.width = 0.18, dodge.width = 0.75),
    size      = 1.2,
    alpha     = 0.6
  ) +
  scale_fill_manual(name = "Phase",      values = phase_fill) +
  scale_color_manual(name = "Optimality", values = gap_colors, labels = gap_labels) +
  scale_shape_manual(name = "Optimality", values = gap_shapes, labels = gap_labels) +
  scale_y_log10(
    breaks = trans_breaks("log10", function(x) 10^x),
    labels = trans_format("log10", math_format(10^.x))
  ) +
  labs(
    title    = "B: Solve Time by Method & Phase",
    subtitle = "Boxes = IQR; points coloured by gap (blue = optimal, red = timed-out)",
    x        = NULL,
    y        = "Solve time (seconds, log scale)"
  ) +
  theme_bw(base_size = 12) +
  theme(
    legend.position  = "right",
    plot.title       = element_text(face = "bold"),
    panel.grid.minor = element_blank()
  )

# ---------------------------------------------------------------------------
# Plot C: MIP model size vs solve time, all phases, gap annotated by shape
# ---------------------------------------------------------------------------

# Build a clean colour palette that covers all l2seg_str levels
all_phases_ordered <- c(
  "segment",
  sort(unique(df$l2seg_str[df$source == "refinement"]))
)
all_phases_ordered <- intersect(all_phases_ordered, unique(df$l2seg_str))

phase_palette_c <- c(
  "segment" = "#1b9e77"
)
# Auto-assign oranges/reds for each refinement l2seg variant
ref_variants <- setdiff(all_phases_ordered, c("construction", "segment"))
ref_cols <- colorRampPalette(c("#fdae6b", "#a63603"))(max(1L, length(ref_variants)))
phase_palette_c <- c(phase_palette_c, setNames(ref_cols, ref_variants))

p_c <- ggplot(dplyr::filter(df, source != "construction"),
              aes(x = problem_size, y = runtime_seconds,
                  color = l2seg_str, shape = factor(gap_zero))) +
  # Reference lines (horizontal time limits)
  geom_hline(data = ref_lines,
             aes(yintercept = xintercept),
             color = "grey50", linetype = "dotted", linewidth = 0.6,
             inherit.aes = FALSE) +
  geom_text(data = ref_lines,
            aes(y = xintercept, label = label),
            x = -Inf, hjust = -0.15, vjust = -0.35,
            color = "grey40", size = 2.8, inherit.aes = FALSE) +
  geom_point(alpha = 0.55, size = 1.4) +
  geom_smooth(aes(x = problem_size, y = runtime_seconds,
                  group = l2seg_str, color = l2seg_str),
              method = "lm", formula = y ~ x,
              se = FALSE, linewidth = 0.6, alpha = 0.8,
              inherit.aes = FALSE) +
  scale_color_manual(name = "Phase / L\u2082seg", values = phase_palette_c) +
  scale_shape_manual(
    name   = "Optimality",
    values = gap_shapes,
    labels = gap_labels
  ) +
  scale_x_continuous(labels = scales::comma) +
  scale_y_log10(
    breaks = trans_breaks("log10", function(x) 10^x),
    labels = trans_format("log10", math_format(10^.x))
  ) +
  labs(
    title    = "C: MIP Model Size vs Solve Time",
    subtitle = "Each point is one solve; lines are OLS fits per phase (log-y scale)",
    x        = "Model size (number of variables/constraints proxy)",
    y        = "Solve time (seconds, log scale)"
  ) +
  theme_bw(base_size = 12) +
  theme(
    legend.position  = "right",
    plot.title       = element_text(face = "bold"),
    panel.grid.minor = element_blank()
  )

# ---------------------------------------------------------------------------
# Combine A + B (top row) and C (bottom, full width) then save
# ---------------------------------------------------------------------------
top_row  <- plot_grid(p_a, p_b, ncol = 2, rel_widths = c(1, 1))
combined <- plot_grid(top_row, p_c, ncol = 1, rel_heights = c(1, 1))


out_file <- file.path(sol_dir, "mip_runtime_vs_size.png")
ggsave(out_file, plot = combined, width = 18, height = 14, dpi = 150)
cat(sprintf("\nPlot saved to: %s\n", out_file))
