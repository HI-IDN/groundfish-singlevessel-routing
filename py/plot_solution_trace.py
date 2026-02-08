import argparse
import os
import numpy as np

(tSHIP, tSTAT, tWAYP, tENDP, tPORT) = (1, 2, 3, 4, 5)


def load_trace(path: str):
    with open(path, "r", encoding="utf8") as f:
        lines = [ln.strip() for ln in f if ln.strip()]

    if not lines or lines[0] != "PLOT_TRACE_V1":
        raise ValueError("Not a PLOT_TRACE_V1 file")

    def find_prefix(prefix):
        for ln in lines:
            if ln.startswith(prefix):
                return ln
        raise ValueError(f"Missing line starting with '{prefix}'")

    size_line = find_prefix("Size ")
    sel_line = find_prefix("SelectedSize ")
    type_line = find_prefix("Type")
    amt_line = find_prefix("Amount")

    Size = int(size_line.split()[1])
    SelectedSize = int(sel_line.split()[1])

    Type = np.array([int(x) for x in type_line.split()[1:]], dtype=np.int32)
    if Type.size != SelectedSize:
        raise ValueError(f"Type length {Type.size} != SelectedSize {SelectedSize}")

    Amount = np.array([float(x) for x in amt_line.split()[1:]], dtype=np.float64)
    if Amount.size != SelectedSize:
        raise ValueError(f"Amount length {Amount.size} != SelectedSize {SelectedSize}")

    # Name block
    name_idx = None
    for i, ln in enumerate(lines):
        if ln == "Name":
            name_idx = i
            break
    names = None
    if name_idx is not None:
        name_lines = lines[name_idx + 1: name_idx + 1 + SelectedSize]
        if len(name_lines) != SelectedSize:
            raise ValueError("Name block truncated")
        names = []
        for ln in name_lines:
            if len(ln) >= 2 and ln[0] == '"' and ln[-1] == '"':
                names.append(ln[1:-1])
            else:
                names.append(ln)

    # LatLonRad block
    lat_idx = None
    for i, ln in enumerate(lines):
        if ln == "LatLonRad":
            lat_idx = i
            break
    if lat_idx is None:
        raise ValueError("Missing LatLonRad block header")
    latlon_lines = lines[lat_idx + 1: lat_idx + 1 + SelectedSize]
    if len(latlon_lines) != SelectedSize:
        raise ValueError("LatLonRad block truncated")
    LatLonRad = np.array([[float(v) for v in ln.split()] for ln in latlon_lines], dtype=np.float64)
    if LatLonRad.shape != (SelectedSize, 4):
        raise ValueError(f"LatLonRad shape {LatLonRad.shape} != ({SelectedSize}, 4)")

    frames = []
    i = 0
    while i < len(lines):
        if not lines[i].startswith("Frame "):
            i += 1
            continue
        parts = lines[i].split()
        if len(parts) < 2:
            raise ValueError(f"Malformed Frame line: {lines[i]}")
        step = int(parts[1])
        meta = {"step": step, "pass": None, "total": None, "note": ""}
        for tok in parts[2:]:
            if "=" not in tok:
                continue
            k, v = tok.split("=", 1)
            if k == "pass":
                meta["pass"] = int(v)
            elif k == "total":
                meta["total"] = float(v)
            elif k == "note":
                meta["note"] = v
        if i + 1 >= len(lines) or not lines[i + 1].startswith("Tour"):
            raise ValueError(f"Missing Tour line after Frame {step}")
        tour_parts = lines[i + 1].split()
        if len(tour_parts) < 2:
            raise ValueError(f"Malformed Tour line after Frame {step}")
        tour_len = int(tour_parts[1])
        tour_vals = [int(x) for x in tour_parts[2:]]
        if len(tour_vals) != tour_len:
            raise ValueError(f"Tour length {len(tour_vals)} != {tour_len} at Frame {step}")
        meta["letour"] = np.array(tour_vals, dtype=int)
        frames.append(meta)
        i += 2

    return Size, SelectedSize, Type, Amount, LatLonRad, names, frames


def normalize_letour(letour, Type):
    if letour.size == 0:
        return letour
    k0 = np.where(letour == 0)[0]
    if k0.size > 0:
        k0 = k0[0]
        letour = np.concatenate((letour[k0:], letour[:k0]))
    if letour.size > 1 and Type[abs(letour[1])] == tPORT:
        letour[1:] = -letour[:0:-1]
    port_mask = Type[np.abs(letour)] == tPORT
    letour[port_mask] = np.abs(letour[port_mask])
    return letour


def letour_distance(letour, dist_mtrx):
    if letour.size == 0:
        return 0.0
    total = 0.0
    prev_node = 0
    for idx in letour[1:]:
        if idx == 0:
            continue
        i = int(abs(idx))
        entry = 2 * i + (1 if idx < 0 else 0)
        exit_node = 2 * i + (0 if idx < 0 else 1)
        total += dist_mtrx[prev_node, entry]
        total += dist_mtrx[entry, exit_node]
        prev_node = exit_node
    total += dist_mtrx[prev_node, 1]
    return total


def segment_stats(letour, Type, Amount, dist_mtrx, names):
    stats = []
    prev_node = 0
    boat_name = names[0] if names else "BOAT"
    start_label = boat_name
    stations = 0
    amount = 0.0
    dist = 0.0

    for idx in letour[1:]:
        if idx == 0:
            continue
        i = int(abs(idx))
        entry = 2 * i + (1 if idx < 0 else 0)
        exit_node = 2 * i + (0 if idx < 0 else 1)
        dist += dist_mtrx[prev_node, entry]
        dist += dist_mtrx[entry, exit_node]
        prev_node = exit_node

        if Type[i] == tSTAT:
            stations += 1
            amount += Amount[i]

        if Type[i] == tPORT:
            if names and i < len(names) and names[i]:
                end_label = names[i]
            else:
                end_label = f"PORT-{i}"
            stats.append((start_label, end_label, stations, dist, amount))
            start_label = end_label
            stations = 0
            amount = 0.0
            dist = 0.0

    dist += dist_mtrx[prev_node, 1]
    stats.append((start_label, f"{boat_name}-END", stations, dist, amount))
    return stats


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trace", required=True, help="Path to .trace.txt file")
    ap.add_argument("--frame", type=int, default=0, help="Frame index to render (default 0)")
    ap.add_argument("--save", help="Write a single frame image to this path")
    ap.add_argument("--save-dir", help="Write all frames to this directory")
    ap.add_argument("--every", type=int, default=1, help="Stride when saving all frames")
    ap.add_argument("--ship-cap", type=float, help="Ship capacity for segment legend")
    ap.add_argument("--legend", action="store_true", help="Show segment amount legend")
    args = ap.parse_args()

    if args.save or args.save_dir:
        os.environ.setdefault("MPLBACKEND", "Agg")
    if "MPLCONFIGDIR" not in os.environ:
        mpl_dir = os.path.join(os.getcwd(), ".mplconfig")
        os.makedirs(mpl_dir, exist_ok=True)
        os.environ["MPLCONFIGDIR"] = mpl_dir

    from survey_utils import DistanceLink, drawTour

    Size, SelectedSize, Type, Amount, LatLonRad, names, frames = load_trace(args.trace)
    if not frames:
        raise SystemExit("No frames found in trace file")

    StartEnd = LatLonRad[0, :]
    DistMtrx, FsbleMtrx = DistanceLink(Type[:Size], LatLonRad, StartEnd, Size, SelectedSize)

    def render_frame(frame, save_path=None):
        letour = normalize_letour(frame["letour"], Type)
        total_dist = letour_distance(letour, DistMtrx)
        title = f"Total distance: {total_dist:.3f} nm"
        stats = segment_stats(letour, Type, Amount, DistMtrx, names)
        legend_labels = None
        if args.ship_cap is not None and args.ship_cap > 0:
            legend_labels = []
            for i, (_s, _e, _st, _d, amount) in enumerate(stats, 1):
                label = f"Seg {i}: {amount:.0f}/{args.ship_cap:.0f}"
                if amount > args.ship_cap:
                    label += " OVER"
                legend_labels.append(label)
        elif args.legend:
            legend_labels = [f"Seg {i}: {amount:.0f}" for i, (_s, _e, _st, _d, amount) in enumerate(stats, 1)]
        drawTour(letour, LatLonRad, Type, Amount, DistMtrx, FsbleMtrx,
                 legend_labels=legend_labels,
                 save_path=save_path, show=not bool(save_path), title=title)

    if args.save_dir:
        os.makedirs(args.save_dir, exist_ok=True)
        for idx, frame in enumerate(frames):
            if idx % max(args.every, 1) != 0:
                continue
            out_path = os.path.join(args.save_dir, f"frame_{idx:04d}.png")
            render_frame(frame, save_path=out_path)
    else:
        frame_idx = args.frame
        if frame_idx < 0 or frame_idx >= len(frames):
            raise SystemExit(f"Frame {frame_idx} out of range (0..{len(frames)-1})")
        render_frame(frames[frame_idx], save_path=args.save)


if __name__ == "__main__":
    main()
