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
    ap.add_argument("--csv", required=True, help="Path to cap hillclimb CSV output")
    ap.add_argument("--window", type=int, default=5, help="Smoothing window (default: 5)")
    ap.add_argument("--smooth", choices=["mean", "median"], default="mean",
                    help="Smoothing method (default: mean)")
    ap.add_argument("--save", help="Write the plot to this file instead of showing it")
    ap.add_argument("--pgf-out", help="Write PGF/TikZ output to this file")
    args = ap.parse_args()

    if args.save:
        os.environ.setdefault("MPLBACKEND", "Agg")

    header, data = load_csv(args.csv)
    col_index = {name: i for i, name in enumerate(header)}
    for key in ("attempts", "changed", "total", "best", "stall"):
        if key not in col_index:
            raise ValueError("CSV must include columns: attempts, changed, total, best, stall")

    attempts = data[:, col_index["attempts"]]
    changed = data[:, col_index["changed"]]
    total = data[:, col_index["total"]]
    best = data[:, col_index["best"]]
    stall = data[:, col_index["stall"]]

    def seg_id(name, suffix):
        if not name.endswith(suffix):
            return None
        prefix = name[: -len(suffix)]
        if not prefix.startswith("seg"):
            return None
        try:
            return int(prefix[3:])
        except ValueError:
            return None

    dist_idx = {}
    station_idx = {}
    amount_idx = {}
    for name, idx in col_index.items():
        sid = seg_id(name, "_distance")
        if sid is not None:
            dist_idx[sid] = idx
            continue
        sid = seg_id(name, "_stations")
        if sid is not None:
            station_idx[sid] = idx
            continue
        sid = seg_id(name, "_amount")
        if sid is not None:
            amount_idx[sid] = idx

    seg_ids = sorted(dist_idx.keys())
    if not seg_ids:
        raise ValueError("CSV has no segment distance columns")
    for sid in seg_ids:
        if sid not in station_idx or sid not in amount_idx:
            raise ValueError(f"Missing segment columns for seg{sid}")
    seg_count = len(seg_ids)

    dist_cols = np.vstack([data[:, dist_idx[sid]] for sid in seg_ids]).T
    station_cols = np.vstack([data[:, station_idx[sid]] for sid in seg_ids]).T
    amount_cols = np.vstack([data[:, amount_idx[sid]] for sid in seg_ids]).T

    outlier_threshold = 100000.0
    outlier_mask = dist_cols > outlier_threshold
    invalid_mask = dist_cols <= 0.0
    dist_cols_plot = dist_cols.copy()
    dist_cols_plot[outlier_mask | invalid_mask] = np.nan

    total_plot = total.copy()
    total_plot[total_plot <= 0.0] = np.nan

    if args.smooth == "median":
        smoother = smooth_series_median
    else:
        smoother = smooth_series_mean

    total_s = smoother(total_plot, args.window)
    best_s = smoother(best, args.window)
    dist_cols_s = np.vstack([smoother(dist_cols_plot[:, s], args.window)
                             for s in range(seg_count)])
    amount_cols_s = np.vstack([smoother(amount_cols[:, s], args.window)
                               for s in range(seg_count)])

    colors = plt.get_cmap("tab20", max(seg_count, 1))
    dot_size = 2.0
    dot_alpha = 0.35

    best_idx = None
    if np.isfinite(best).any():
        best_idx = int(np.nanargmin(best))
        best_iter = attempts[best_idx]

    fig, axes = plt.subplots(4, 1, figsize=(10, 12), sharex=True)

    axes[0].plot(attempts, total_plot, color="black", marker=".", linestyle="None",
                 markersize=dot_size, alpha=dot_alpha, label="total")
    axes[0].plot(attempts, total_s, color="black", linewidth=1.4)
    axes[0].plot(attempts, best, color="tab:blue", linewidth=1.2, label="best")
    axes[0].plot(attempts, best_s, color="tab:blue", linewidth=1.4, alpha=0.6)
    if best_idx is not None:
        axes[0].hlines(best[best_idx], best_iter, attempts[-1],
                       color="gray", linewidth=0.9, alpha=0.6)
        trans0 = blended_transform_factory(axes[0].transData, axes[0].transAxes)
        axes[0].plot([best_iter - 0.4, best_iter + 0.4], [0, 1], transform=trans0,
                     color="gray", linewidth=0.9, alpha=0.6)
    axes[0].set_ylabel("Total distance (nm)")
    axes[0].grid(True, linestyle=":", linewidth=0.5, alpha=0.6)
    axes[0].legend(loc="upper right")

    for s in range(seg_count):
        axes[1].plot(attempts, dist_cols_plot[:, s], color=colors(s), marker=".",
                     linestyle="None", markersize=dot_size, alpha=dot_alpha)
        axes[1].plot(attempts, dist_cols_s[s], color=colors(s), linewidth=1.0)
        if best_idx is not None:
            axes[1].hlines(dist_cols_plot[best_idx, s], best_iter, attempts[-1],
                           color="gray", linewidth=0.7, alpha=0.45)
    if best_idx is not None:
        trans1 = blended_transform_factory(axes[1].transData, axes[1].transAxes)
        axes[1].plot([best_iter - 0.4, best_iter + 0.4], [0, 1], transform=trans1,
                     color="gray", linewidth=0.9, alpha=0.6)
    axes[1].set_ylabel("Segment distance (nm)")
    axes[1].grid(True, linestyle=":", linewidth=0.5, alpha=0.6)

    for s in range(seg_count):
        axes[2].plot(attempts, amount_cols[:, s], color=colors(s), marker=".",
                     linestyle="None", markersize=dot_size, alpha=dot_alpha)
        axes[2].plot(attempts, amount_cols_s[s], color=colors(s), linewidth=1.0)
        if best_idx is not None:
            axes[2].hlines(amount_cols[best_idx, s], best_iter, attempts[-1],
                           color="gray", linewidth=0.7, alpha=0.45)
    if best_idx is not None:
        trans2 = blended_transform_factory(axes[2].transData, axes[2].transAxes)
        axes[2].plot([best_iter - 0.4, best_iter + 0.4], [0, 1], transform=trans2,
                     color="gray", linewidth=0.9, alpha=0.6)
    axes[2].set_ylabel("Segment amount")
    axes[2].grid(True, linestyle=":", linewidth=0.5, alpha=0.6)

    axes[3].plot(attempts, changed, color="tab:green", marker=".",
                 linestyle="None", markersize=dot_size, alpha=0.6, label="changed")
    axes[3].plot(attempts, stall, color="tab:red", linewidth=1.2, label="stall")
    axes[3].set_ylabel("Changed / Stall")
    axes[3].set_xlabel("Pass")
    axes[3].grid(True, linestyle=":", linewidth=0.5, alpha=0.6)
    axes[3].legend(loc="upper right")

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
