#!/usr/bin/env Rscript
# plot_mip_runtime.R
#
# Reads construction.json, segment.json and refinement.json for each method,
# extracts every individual MIP solve's runtime from the "mip" section (if
# present), and produces an ECDF plot of solve times coloured by method.
#
# Output: sol/mip_runtime_vs_size.png
#
# Run from the project root:
#   Rscript R/plot_mip_runtime.R

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

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

load_required_packages(c("jsonlite", "ggplot2", "cowplot"))

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

# Locate sol/ relative to this script's directory (works when sourced or
# Rscript'd from any working directory).
script_dir <- tryCatch(
  dirname(normalizePath(sys.frame(1)$ofile)),
  error = function(e) getwd()           # fallback when run interactively
)
sol_dir <- normalizePath(file.path(script_dir, "..", "sol"), mustWork = FALSE)

if (!dir.exists(sol_dir)) {
  # Last resort: try relative to cwd
  sol_dir <- "sol"
}

cat(sprintf("sol_dir: %s\n", sol_dir))

# ---------------------------------------------------------------------------
# Methods to process
# ---------------------------------------------------------------------------

methods <- c("nn", "ci", "ge", "noport", "fixedport")

# ---------------------------------------------------------------------------
# Extract MIP solves from a single JSON file
# ---------------------------------------------------------------------------

extract_mip_solves <- function(json_path, method, source_label) {
  if (!file.exists(json_path)) {
    return(NULL)
  }

  json <- tryCatch(
    jsonlite::fromJSON(json_path, simplifyVector = FALSE),
    error = function(e) {
      warning(sprintf("Failed to parse %s: %s", json_path, e$message))
      NULL
    }
  )
  if (is.null(json) || is.null(json$mip)) {
    return(NULL)
  }

  mip         <- json$mip
  tuple_names <- unlist(mip$solve_detail_tuple)
  solves      <- mip$solves

  if (length(solves) == 0) {
    return(NULL)
  }

  # construction + segment use ["size", "runtime_seconds", "gap_percent"]
  # refinement uses  [..., "station_count", ..., "runtime_seconds", ...]
  if (source_label %in% c("construction", "segment")) {
    size_col <- which(tuple_names == "size")
    rt_col   <- which(tuple_names == "runtime_seconds")
  } else {
    size_col <- which(tuple_names == "station_count")
    rt_col   <- which(tuple_names == "runtime_seconds")
  }

  if (length(size_col) == 0 || length(rt_col) == 0) {
    warning(sprintf(
      "Expected columns not found in %s (tuple: %s)",
      json_path,
      paste(tuple_names, collapse = ", ")
    ))
    return(NULL)
  }

  data.frame(
    method          = method,
    source          = source_label,
    problem_size    = vapply(solves, function(s) as.numeric(s[[size_col]]), numeric(1)),
    runtime_seconds = vapply(solves, function(s) as.numeric(s[[rt_col]]),   numeric(1)),
    stringsAsFactors = FALSE
  )
}

# ---------------------------------------------------------------------------
# Collect data for all methods and all three JSON types
# ---------------------------------------------------------------------------

all_data <- list()

for (method in methods) {
  method_dir <- file.path(sol_dir, method)

  for (src in c("construction", "segment", "refinement")) {
    fname <- switch(src,
      construction = "construction.json",
      segment      = "segment.json",
      refinement   = "refinement.json"
    )
    d <- extract_mip_solves(file.path(method_dir, fname), method, src)
    if (!is.null(d)) {
      cat(sprintf("  [%-10s] %-12s: %d solves\n", method, src, nrow(d)))
      all_data <- c(all_data, list(d))
    }
  }
}

if (length(all_data) == 0) {
  stop("No MIP data found in any of the method directories.")
}

df <- do.call(rbind, all_data)
df$method <- factor(df$method, levels = methods)
df$source <- factor(df$source, levels = c("construction", "segment", "refinement"))

cat(sprintf("\nTotal MIP solves collected: %d\n", nrow(df)))

# ---------------------------------------------------------------------------
# Phase colours (segment vs refinement only — construction ignored here)
# ---------------------------------------------------------------------------

phase_colors <- c(
  "segment"    = "#1b9e77",   # teal
  "refinement" = "#d95f02"    # orange
)

df_fit <- subset(df, source %in% c("segment", "refinement"))

# Reference lines
ref_lines <- data.frame(
  xintercept = c(10, 60),
  label      = c("10 s", "1 min")
)

p_a <- ggplot(df_fit, aes(x = runtime_seconds, color = source)) +
  # Reference lines
  geom_vline(data = ref_lines,
             aes(xintercept = xintercept),
             color = "grey50", linetype = "dotted", linewidth = 0.7) +
  geom_text(data = ref_lines,
            aes(x = xintercept, label = label),
            y = 0.05, hjust = -0.15, vjust = 0,
            color = "grey40", size = 3.2, inherit.aes = FALSE) +
  # ECDF curves
  stat_ecdf(geom = "step", linewidth = 0.9, pad = FALSE) +
  scale_color_manual(name = "Phase", values = phase_colors) +
  scale_x_log10(
    breaks = scales::trans_breaks("log10", function(x) 10^x),
    labels = scales::trans_format("log10", scales::math_format(10^.x))
  ) +
  scale_y_continuous(
    labels = scales::percent_format(accuracy = 1),
    breaks = seq(0, 1, by = 0.1)
  ) +
  labs(
    title    = "A: MIP Solve Time Distribution",
    subtitle = "ECDF across all methods — segment vs refinement phase",
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
# Plot B: boxplot per method × phase (construction included)
# ---------------------------------------------------------------------------

phase_fill <- c(
  "construction" = "#7570b3",   # purple
  "segment"      = "#1b9e77",   # teal
  "refinement"   = "#d95f02"    # orange
)

# Only the four methods the user cares about; drop fixedport
df_b <- subset(df, method %in% c("nn", "ci", "ge", "noport"))
df_b$method <- factor(df_b$method, levels = c("nn", "ci", "ge", "noport"))
df_b$source <- factor(df_b$source,
                      levels = c("construction", "segment", "refinement"))

p_b <- ggplot(df_b,
              aes(x = method, y = runtime_seconds, fill = source)) +
  geom_boxplot(
    outlier.size  = 0.8,
    outlier.alpha = 0.5,
    position      = position_dodge(width = 0.8),
    width         = 0.6
  ) +
  scale_fill_manual(name = "Phase", values = phase_fill) +
  scale_y_log10(
    breaks = scales::trans_breaks("log10", function(x) 10^x),
    labels = scales::trans_format("log10", scales::math_format(10^.x))
  ) +
  labs(
    title    = "B: Solve Time by Method & Phase",
    subtitle = "Construction, segment and refinement MIP solves (log scale)",
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
# Combine A + B side by side with cowplot and save
# ---------------------------------------------------------------------------

combined <- plot_grid(p_a, p_b, labels = NULL, ncol = 2, rel_widths = c(1, 1))

# ---------------------------------------------------------------------------
# Save
# ---------------------------------------------------------------------------

out_file <- file.path(sol_dir, "mip_runtime_vs_size.png")
ggsave(out_file, plot = combined, width = 18, height = 7, dpi = 150)
cat(sprintf("\nPlot saved to: %s\n", out_file))




library(dplyr)
library(knitr)

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

runtime_for <- function(method_name, source_name) {
  json_path <- file.path(sol_dir, method_name, sprintf("%s.json", source_name))
  if (!file.exists(json_path)) {
    return("---")
  }

  json <- tryCatch(
    jsonlite::fromJSON(json_path, simplifyVector = FALSE),
    error = function(e) NULL
  )
  value <- json$summary$runtime_seconds$grandtotal

  if (is.null(value) || length(value) == 0 || is.na(value)) {
    return("---")
  }

  format_runtime(as.numeric(value))
}

runtime_for_sources <- function(method_name, source_names) {
  values <- vapply(source_names, function(source_name) {
    json_path <- file.path(sol_dir, method_name, sprintf("%s.json", source_name))
    if (!file.exists(json_path)) {
      return(NA_real_)
    }

    json <- tryCatch(
      jsonlite::fromJSON(json_path, simplifyVector = FALSE),
      error = function(e) NULL
    )
    value <- json$summary$runtime_seconds$grandtotal

    if (is.null(value) || length(value) == 0 || is.na(value)) {
      return(NA_real_)
    }

    as.numeric(value)
  }, numeric(1))

  if (all(is.na(values))) {
    return("---")
  }

  format_runtime(sum(values, na.rm = TRUE))
}

read_config_timeout_seconds <- function(key) {
  config_path <- normalizePath(
    file.path(sol_dir, "..", "config", "gsp_solver.yaml"),
    mustWork = FALSE
  )
  if (!file.exists(config_path)) {
    warning(sprintf("Missing solver config: %s", config_path))
    return(NA_real_)
  }

  lines <- readLines(config_path, warn = FALSE)
  match <- grep(sprintf("^\\s*%s:\\s*[0-9]+", key), lines, value = TRUE)
  if (length(match) == 0) {
    warning(sprintf("Missing timeout key '%s' in %s", key, config_path))
    return(NA_real_)
  }

  as.numeric(sub(sprintf("^\\s*%s:\\s*([0-9]+).*$", key), "\\1", match[[1]]))
}

format_timeout_runtime <- function(seconds) {
  if (is.na(seconds)) {
    return("---")
  }

  if (seconds %% 3600 == 0) {
    return(sprintf("%.0f h", seconds / 3600))
  }

  sprintf("%.1f min", seconds / 60)
}

read_result_json <- function(method, source) {
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

extract_summary_distance <- function(method, source) {
  json <- read_result_json(method, source)
  value <- json$summary$distance_nm$final

  if (is.null(value) || length(value) == 0 || is.na(value)) {
    return(NA_real_)
  }

  as.numeric(value)
}

format_distance <- function(value) {
  if (is.na(value)) {
    return("---")
  }
  sprintf("%.2f", value)
}

construction_distance <- function(method) {
  format_distance(extract_summary_distance(method, "construction"))
}

segment_distance <- function(method) {
  format_distance(extract_summary_distance(method, "segment"))
}

cmip_timeout_seconds <- read_config_timeout_seconds("Xseg")
cmip_timeout_runtime <- format_timeout_runtime(cmip_timeout_seconds)

runtime_rows <- tibble::tribble(
  ~method,     ~Runtime,
  "noport",    runtime_for("noport", "construction"),
  "fixedport", cmip_timeout_runtime,
  "nn",        runtime_for_sources("nn", c("construction", "segment")),
  "ci",        runtime_for_sources("ci", c("construction", "segment")),
  "ge",        runtime_for_sources("ge", c("construction", "segment")),
  "noport_mh", runtime_for("noport", "segment")
)

method_rows <- tibble::tribble(
  ~method,     ~Notation, ~Variant,                       ~"No port",    ~"With port",
  "noport",    "NP-MIP",  "No-port directed TSP",          construction_distance("noport"), "---",
  "fixedport", "C-MIP",   "Capacity-aware MIP",            "---",       "timeout",
  "nn",        "MH-NN",   "MH with nearest-neighbor",      "---",       segment_distance("nn"),
  "ci",        "MH-CI",   "MH with cheapest-insertion",    construction_distance("ci"), segment_distance("ci"),
  "ge",        "MH-GE",   "MH with greedy-edge",           construction_distance("ge"), segment_distance("ge"),
  "noport_mh", "MH-OPT",  "MH with NP-based initialization","(NP-MIP)",  segment_distance("noport")
)

# If MH-OPT data are stored under "noport", you may need to join manually
# or duplicate the noport summary row under method = "noport_mh".

table_tbl <- method_rows %>%
  left_join(runtime_rows, by = "method") %>%
  mutate(
    Runtime = ifelse(is.na(Runtime), "---", Runtime)
  ) %>%
  select(
    Notation, Variant, "No port", "With port", Runtime
  )

phase_runtime_summary <- df %>%
  filter(source %in% c("segment", "refinement")) %>%
  group_by(source) %>%
  summarise(
    n = n(),
    mean_s = mean(runtime_seconds, na.rm = TRUE),
    max_s = max(runtime_seconds, na.rm = TRUE),
    .groups = "drop"
  ) %>%
  mutate(
    label = dplyr::case_when(
      source == "segment" ~ "Single-segment post-optimization",
      source == "refinement" ~ "Two-segment refinement",
      TRUE ~ as.character(source)
    ),
    summary = sprintf(
      "%s subproblems: $n=%d$, mean %.1f s, max %.1f s",
      label, n, mean_s, max_s
    )
  )

latex_table <- kable(
  table_tbl,
  format = "latex",
  booktabs = TRUE,
  escape = FALSE,
  linesep = "",
  label = "variant-notation",
  caption = "Variant notation, baseline initialization distances, and total runtime for each scenario.
  The first distance column gives the underlying no-port initialization, and the second shows the
  corresponding capacity-feasible port segmentation.
  Some variants do not entail either a no-port or a with-port solution."
)

cat(latex_table)
cat("\n\n")
cat("\\noindent ")
cat(paste(phase_runtime_summary$summary, collapse = "; "))
cat(".\n")
