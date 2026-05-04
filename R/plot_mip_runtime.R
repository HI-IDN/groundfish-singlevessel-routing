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
