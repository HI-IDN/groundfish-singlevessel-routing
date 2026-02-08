import argparse
import csv
import os

import numpy as np
import matplotlib.pyplot as plt


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
        out[i] = np.median(ypad[i:i + window])
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", required=True, help="Path to hillclimb CSV output")
    ap.add_argument("--window", type=int, default=7, help="Smoothing window (default: 7)")
    ap.add_argument("--smooth", choices=["mean", "median"], default="mean",
                    help="Smoothing method (default: mean)")
    ap.add_argument("--save", help="Write the plot to this file instead of showing it")
    args = ap.parse_args()

    if args.save:
        os.environ.setdefault("MPLBACKEND", "Agg")

    _, data = load_csv(args.csv)
    seg_count = (data.shape[1] - 1) // 3
    iterations = np.arange(1, data.shape[0] + 1)

    dist_cols = data[:, 1::3]
    amount_cols = data[:, 3::3]
    total_dist = dist_cols.sum(axis=1)

    if args.smooth == "median":
        smoother = smooth_series_median
    else:
        smoother = smooth_series_mean

    total_dist_s = smoother(total_dist, args.window)
    amount_s = np.vstack([smoother(amount_cols[:, s], args.window)
                          for s in range(seg_count)])

    colors = plt.get_cmap("tab20", max(seg_count, 1))

    fig, axes = plt.subplots(2, 1, figsize=(10, 8), sharex=True)

    axes[0].plot(iterations, total_dist, color="black", marker=".", linestyle="None", alpha=0.35)
    axes[0].plot(iterations, total_dist_s, color="black", linewidth=1.6)
    axes[0].set_ylabel("Total distance (nm)")
    axes[0].grid(True, linestyle=":", linewidth=0.5, alpha=0.6)

    for s in range(seg_count):
        axes[1].plot(iterations, amount_cols[:, s], color=colors(s),
                     marker=".", linestyle="None", alpha=0.3)
        axes[1].plot(iterations, amount_s[s], color=colors(s), linewidth=1.2)
    axes[1].set_ylabel("Segment amount")
    axes[1].set_xlabel("Iteration")
    axes[1].grid(True, linestyle=":", linewidth=0.5, alpha=0.6)

    fig.tight_layout()
    if args.save:
        fig.savefig(args.save, dpi=200, bbox_inches="tight")
    else:
        plt.show()


if __name__ == "__main__":
    main()
