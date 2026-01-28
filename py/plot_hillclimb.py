import argparse
import csv
import os

import numpy as np
import matplotlib.pyplot as plt
from matplotlib.transforms import blended_transform_factory


def load_csv(path):
    with open(path, "r", newline="") as f:
        reader = csv.reader(f)
        header = next(reader, None)
        if not header:
            raise ValueError("CSV is empty")
        rows = []
        for row in reader:
            if not row:
                continue
            rows.append([float(x) for x in row])
    if not rows:
        raise ValueError("CSV has no data rows")
    data = np.array(rows, dtype=float)
    if data.shape[1] < 4 or (data.shape[1] - 1) % 3 != 0:
        raise ValueError("CSV columns must be: attempts + triples of distance, stations, amount")
    return header, data


def smooth_series_mean(y, window):
    if window <= 1:
        return y
    window = min(window, len(y))
    pad_left = window // 2
    pad_right = window - 1 - pad_left
    if np.isnan(y).any():
        ypad = np.pad(y, (pad_left, pad_right), mode="edge")
        out = np.empty_like(y, dtype=float)
        for i in range(len(y)):
            window_vals = ypad[i:i + window]
            if np.all(np.isnan(window_vals)):
                out[i] = np.nan
            else:
                out[i] = np.nanmean(window_vals)
        return out
    ypad = np.pad(y, (pad_left, pad_right), mode="edge")
    kernel = np.ones(window, dtype=float) / float(window)
    return np.convolve(ypad, kernel, mode="valid")


def smooth_series_median(y, window):
    if window <= 1:
        return y
    window = min(window, len(y))
    pad_left = window // 2
    pad_right = window - 1 - pad_left
    ypad = np.pad(y, (pad_left, pad_right), mode="edge")
    out = np.empty_like(y, dtype=float)
    for i in range(len(y)):
        window_vals = ypad[i:i + window]
        if np.all(np.isnan(window_vals)):
            out[i] = np.nan
        else:
            out[i] = np.nanmedian(window_vals)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", required=True, help="Path to hillclimb CSV output")
    ap.add_argument("--window", type=int, default=7, help="Smoothing window (default: 7)")
    ap.add_argument("--smooth", choices=["mean", "median"], default="mean",
                    help="Smoothing method (default: mean)")
    ap.add_argument("--save", help="Write the plot to this file instead of showing it")
    ap.add_argument("--pgf-out", help="Write PGF/TikZ output to this file")
    args = ap.parse_args()

    if args.save:
        os.environ.setdefault("MPLBACKEND", "Agg")

    header, data = load_csv(args.csv)
    attempts = data[:, 0]
    seg_count = (data.shape[1] - 1) // 3
    iterations = np.arange(1, data.shape[0] + 1)

    dist_cols = data[:, 1::3]
    station_cols = data[:, 2::3]
    amount_cols = data[:, 3::3]
    outlier_threshold = 100000.0
    outlier_mask = dist_cols > outlier_threshold
    invalid_mask = dist_cols <= 0.0
    dist_cols_plot = dist_cols.copy()
    dist_cols_plot[outlier_mask | invalid_mask] = np.nan
    outlier_rows = np.any(outlier_mask | invalid_mask, axis=1)
    total_dist_plot = np.nansum(dist_cols_plot, axis=1)
    all_nan = np.all(np.isnan(dist_cols_plot), axis=1)
    total_dist_plot[all_nan] = np.nan

    if args.smooth == "median":
        smoother = smooth_series_median
    else:
        smoother = smooth_series_mean

    total_dist_s = smoother(total_dist_plot, args.window)
    dist_cols_s = np.vstack([smoother(dist_cols_plot[:, s], args.window)
                             for s in range(seg_count)])
    amount_cols_s = np.vstack([smoother(amount_cols[:, s], args.window)
                               for s in range(seg_count)])
    attempts_s = smoother(attempts, args.window)

    colors = plt.get_cmap("tab20", max(seg_count, 1))
    dot_size = 2.0
    dot_alpha = 0.35
    best_idx = None
    if np.isfinite(total_dist_plot).any():
        best_idx = int(np.nanargmin(total_dist_plot))
        best_iter = iterations[best_idx]

    fig, axes = plt.subplots(4, 1, figsize=(10, 12), sharex=True)

    axes[0].plot(iterations, total_dist_plot, color="black", marker=".", linestyle="None",
                 markersize=dot_size, alpha=dot_alpha)
    axes[0].plot(iterations, total_dist_s, color="black", linewidth=1.4)
    if best_idx is not None:
        axes[0].hlines(total_dist_plot[best_idx], best_iter, iterations[-1],
                       color="gray", linewidth=0.9, alpha=0.6)
        trans0 = blended_transform_factory(axes[0].transData, axes[0].transAxes)
        axes[0].plot([best_iter - 0.4, best_iter + 0.4], [0, 1], transform=trans0,
                     color="gray", linewidth=0.9, alpha=0.6)
    axes[0].set_ylabel("Total distance (nm)")
    axes[0].grid(True, linestyle=":", linewidth=0.5, alpha=0.6)

    for s in range(seg_count):
        axes[1].plot(iterations, dist_cols_plot[:, s], color=colors(s), marker=".",
                     linestyle="None", markersize=dot_size, alpha=dot_alpha)
        axes[1].plot(iterations, dist_cols_s[s], color=colors(s), linewidth=1.0)
        if best_idx is not None:
            axes[1].hlines(dist_cols_plot[best_idx, s], best_iter, iterations[-1],
                           color="gray", linewidth=0.7, alpha=0.45)
    if best_idx is not None:
        trans1 = blended_transform_factory(axes[1].transData, axes[1].transAxes)
        axes[1].plot([best_iter - 0.4, best_iter + 0.4], [0, 1], transform=trans1,
                     color="gray", linewidth=0.9, alpha=0.6)
    axes[1].set_ylabel("Segment distance (nm)")
    axes[1].grid(True, linestyle=":", linewidth=0.5, alpha=0.6)

    for s in range(seg_count):
        axes[2].plot(iterations, amount_cols[:, s], color=colors(s), marker=".",
                     linestyle="None", markersize=dot_size, alpha=dot_alpha)
        axes[2].plot(iterations, amount_cols_s[s], color=colors(s), linewidth=1.0)
        if best_idx is not None:
            axes[2].hlines(amount_cols[best_idx, s], best_iter, iterations[-1],
                           color="gray", linewidth=0.7, alpha=0.45)
    if best_idx is not None:
        trans2 = blended_transform_factory(axes[2].transData, axes[2].transAxes)
        axes[2].plot([best_iter - 0.4, best_iter + 0.4], [0, 1], transform=trans2,
                     color="gray", linewidth=0.9, alpha=0.6)
    axes[2].set_ylabel("Segment amount")
    axes[2].grid(True, linestyle=":", linewidth=0.5, alpha=0.6)

    axes[3].plot(iterations, attempts, color="black", marker=".", linestyle="None",
                 markersize=dot_size, alpha=dot_alpha)
    axes[3].plot(iterations, attempts_s, color="black", linewidth=1.2)
    if best_idx is not None:
        trans3 = blended_transform_factory(axes[3].transData, axes[3].transAxes)
        axes[3].plot([best_iter - 0.4, best_iter + 0.4], [0, 1], transform=trans3,
                     color="gray", linewidth=0.9, alpha=0.6)
    axes[3].set_ylabel("Attempts")
    axes[3].set_xlabel("Iteration")
    axes[3].grid(True, linestyle=":", linewidth=0.5, alpha=0.6)

    fig.tight_layout()
    if args.save:
        fig.savefig(args.save, dpi=200, bbox_inches="tight")
    if args.pgf_out:
        from matplotlib.backends.backend_pgf import FigureCanvasPgf
        FigureCanvasPgf(fig)
        fig.savefig(args.pgf_out)
    if not args.save and not args.pgf_out:
        plt.show()


if __name__ == "__main__":
    main()
