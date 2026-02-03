import argparse
import os
import numpy as np

(tSHIP, tSTAT, tWAYP, tENDP, tPORT) = (1, 2, 3, 4, 5)

def node_tour_to_letour(node_tour, Size):
    # node_tour contains node ids 0..2*Size-1
    letour = []
    for node in node_tour:
        city = int(node // 2)
        if city not in [abs(x) for x in letour]:
            if node % 2 == 1:
                letour.append(-city)
            else:
                letour.append(city)

    letour = np.array(letour, dtype=int)

    # rotate so that 0 (ship) is first
    k0 = np.where(letour == 0)[0]
    if k0.size > 0:
        k0 = k0[0]
        letour = np.concatenate((letour[k0:], letour[:k0]))
    return letour

def load_plot_bundle(path: str):
    with open(path, "r", encoding="utf8") as f:
        lines = [ln.strip() for ln in f if ln.strip()]

    if not lines or not lines[0].startswith("PLOT_BUNDLE_"):
        raise ValueError("Not a plot bundle file")

    version = lines[0]

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

    if version in ("PLOT_BUNDLE_V2", "PLOT_BUNDLE_V3"):
        node_line = find_prefix("NodeTour")
        parts = node_line.split()
        if len(parts) < 3:
            raise ValueError("NodeTour line missing data")
        node_len = int(parts[1])
        tour = np.array([int(x) for x in parts[2:]], dtype=np.int32)
        if tour.size != node_len:
            raise ValueError(f"NodeTour length {tour.size} != {node_len}")
    elif version == "PLOT_BUNDLE_V1":
        tour_line = find_prefix("Tour")
        tour = np.array([int(x) for x in tour_line.split()[1:]], dtype=np.int32)
    else:
        raise ValueError(f"Unsupported plot bundle version: {version}")

    Type = np.array([int(x) for x in type_line.split()[1:]], dtype=np.int32)
    if Type.size != SelectedSize:
        raise ValueError(f"Type length {Type.size} != SelectedSize {SelectedSize}")

    Amount = np.array([float(x) for x in amt_line.split()[1:]], dtype=np.float64)
    if Amount.size != SelectedSize:
        raise ValueError(f"Amount length {Amount.size} != SelectedSize {SelectedSize}")

    # LatLonRad block: locate line "LatLonRad" then read next SelectedSize lines
    idx = None
    for i, ln in enumerate(lines):
        if ln == "LatLonRad":
            idx = i
            break
    if idx is None:
        raise ValueError("Missing LatLonRad block header")

    latlon_lines = lines[idx + 1: idx + 1 + SelectedSize]
    if len(latlon_lines) != SelectedSize:
        raise ValueError("LatLonRad block truncated")

    LatLonRad = np.array([[float(v) for v in ln.split()] for ln in latlon_lines], dtype=np.float64)
    if LatLonRad.shape != (SelectedSize, 4):
        raise ValueError(f"LatLonRad shape {LatLonRad.shape} != ({SelectedSize}, 4)")

    dist_mtrx = None
    fsb_mtrx = None
    names = None
    if version == "PLOT_BUNDLE_V3":
        full_line = find_prefix("FullMatrixSize ")
        full_m = int(full_line.split()[1])
        dist_idx = None
        fsb_idx = None
        for i, ln in enumerate(lines):
            if ln == "DistMtrx":
                dist_idx = i
            elif ln == "FsbleMtrx":
                fsb_idx = i
        if dist_idx is None or fsb_idx is None:
            raise ValueError("Missing DistMtrx/FsbleMtrx blocks in V3 bundle")
        dist_lines = lines[dist_idx + 1: dist_idx + 1 + full_m]
        fsb_lines = lines[fsb_idx + 1: fsb_idx + 1 + full_m]
        if len(dist_lines) != full_m or len(fsb_lines) != full_m:
            raise ValueError("DistMtrx/FsbleMtrx blocks truncated")
        dist_mtrx = np.array([[float(v) for v in ln.split()] for ln in dist_lines], dtype=np.float64)
        fsb_mtrx = np.array([[int(v) for v in ln.split()] for ln in fsb_lines], dtype=np.int32)
        if dist_mtrx.shape != (full_m, full_m):
            raise ValueError(f"DistMtrx shape {dist_mtrx.shape} != ({full_m}, {full_m})")
        if fsb_mtrx.shape != (full_m, full_m):
            raise ValueError(f"FsbleMtrx shape {fsb_mtrx.shape} != ({full_m}, {full_m})")

    # Optional Name block
    name_idx = None
    for i, ln in enumerate(lines):
        if ln == "Name":
            name_idx = i
            break
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

    return Size, SelectedSize, tour, Type, Amount, LatLonRad, version, dist_mtrx, fsb_mtrx, names


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

def reverse_letour(letour, Type):
    if letour.size == 0:
        return letour
    rev = letour[::-1].copy()
    for i, idx in enumerate(rev):
        if idx == 0:
            continue
        if Type[abs(idx)] != tPORT:
            rev[i] = -idx
        else:
            rev[i] = abs(idx)
    k0 = np.where(rev == 0)[0]
    if k0.size > 0:
        rev = np.concatenate((rev[k0[0]:], rev[:k0[0]]))
    port_mask = Type[np.abs(rev)] == tPORT
    rev[port_mask] = np.abs(rev[port_mask])
    return rev


def tour_distance(node_tour, dist_mtrx):
    if node_tour.size == 0:
        return 0.0
    total = 0.0
    for i in range(node_tour.size):
        a = int(node_tour[i])
        b = int(node_tour[(i + 1) % node_tour.size])
        total += dist_mtrx[a, b]
    return total


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


def format_label(idx, Type, names, boat_name):
    if idx == 0:
        return boat_name
    if Type[idx] == tPORT:
        if names and idx < len(names) and names[idx]:
            return names[idx]
        return f"PORT-{idx}"
    if Type[idx] == tSTAT:
        return f"STAT-{idx}"
    return f"NODE-{idx}"


def print_edges(letour, Type, dist_mtrx, names, full=False):
    boat_name = names[0] if names else "BOAT"
    prev_node = 0
    prev_label = f"{boat_name}-START"
    total = 0.0
    for idx in letour[1:]:
        if idx == 0:
            continue
        i = int(abs(idx))
        entry = 2 * i + (1 if idx < 0 else 0)
        exit_node = 2 * i + (0 if idx < 0 else 1)
        base_label = format_label(i, Type, names, boat_name)
        entry_label = f"{base_label} [entry]"
        exit_label = f"{base_label} [exit]"
        d = dist_mtrx[prev_node, entry]
        total += d
        if full:
            print(f"{prev_label} -> {entry_label} (distance: {d:.3f}, total: {total:.3f})")
        else:
            print(f"{prev_label} -> {base_label} (distance: {d:.3f})")
        if full:
            d_in = dist_mtrx[entry, exit_node]
            if d_in > 1e-9:
                total += d_in
                print(f"{entry_label} -> {exit_label} (distance: {d_in:.3f}, total: {total:.3f})")
        prev_node = exit_node
        prev_label = exit_label if full else base_label
    d_end = dist_mtrx[prev_node, 1]
    total += d_end
    if full:
        print(f"{prev_label} -> {boat_name}-END (distance: {d_end:.3f}, total: {total:.3f})")
        print(f"TOTAL distance: {total:.3f}")
    else:
        print(f"{prev_label} -> {boat_name}-END (distance: {d_end:.3f})")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sol", required=True, help="Path to solution_plot.txt (from C)")
    ap.add_argument("--recompute", action="store_true",
                    help="Recompute Dist/Fsble with DistanceLink instead of using stored matrices")
    ap.add_argument("--save", help="Write the plot to this file instead of showing it")
    ap.add_argument("--ship-cap", type=float,
                    help="Ship capacity for segment legend (e.g., 45000)")
    ap.add_argument("--legend", action="store_true",
                    help="Show segment amount legend even without ship capacity")
    ap.add_argument("--edges", action="store_true",
                    help="Print edge-by-edge distances for the route")
    ap.add_argument("--edges-full", action="store_true",
                    help="Print edge-by-edge distances including entry->exit legs")
    ap.add_argument("--tikz", action="store_true", help="Print TikZ code to stdout")
    ap.add_argument("--tikz-out", help="Write TikZ code to this file")
    args = ap.parse_args()

    if args.save:
        os.environ.setdefault("MPLBACKEND", "Agg")
    if "MPLCONFIGDIR" not in os.environ:
        mpl_dir = os.path.join(os.getcwd(), ".mplconfig")
        os.makedirs(mpl_dir, exist_ok=True)
        os.environ["MPLCONFIGDIR"] = mpl_dir

    # Import after MPL configuration to allow non-interactive backends.
    from survey_utils import DistanceLink, drawTour, drawTour_tikz

    Size, SelectedSize, tour, Type, Amount, LatLonRad, version, dist_mtrx, fsb_mtrx, names = load_plot_bundle(args.sol)

    node_tour = None
    if version == "PLOT_BUNDLE_V2" or (tour.size == 2 * Size and tour.min() >= 0):
        node_tour = tour
        letour = node_tour_to_letour(tour, Size)
    else:
        letour = tour.astype(int)

    letour = normalize_letour(letour, Type)
    # StartEnd is the ship row after your stacking; in your pipeline it's row 0.
    StartEnd = LatLonRad[0, :]

    if args.recompute or dist_mtrx is None or fsb_mtrx is None:
        # DistanceLink expects (Type_firstSize, LatLon, StartEnd, Size, SelectedSize)
        DistMtrx, FsbleMtrx = DistanceLink(Type[:Size], LatLonRad, StartEnd, Size, SelectedSize)
    else:
        DistMtrx, FsbleMtrx = dist_mtrx, fsb_mtrx

    title = None
    if node_tour is not None:
        total_dist = tour_distance(node_tour, DistMtrx)
    else:
        total_dist = letour_distance(letour, DistMtrx)
    title = f"Total distance: {total_dist:.3f} nm"
    print(f"Total distance (nm): {total_dist:.3f}")

    stats = segment_stats(letour, Type, Amount, DistMtrx, names)
    for i, (start, end, stations, dist, amount) in enumerate(stats, 1):
        print(f"Segment {i}: {start} -> {end} | stations={stations} distance={dist:.3f} amount={amount:.0f}")

    if args.edges_full:
        print_edges(letour, Type, DistMtrx, names, full=True)
    elif args.edges:
        print_edges(letour, Type, DistMtrx, names, full=False)

    legend_labels = None
    if args.ship_cap is not None and args.ship_cap > 0:
        legend_labels = []
        for i, (_start, _end, _stations, _dist, amount) in enumerate(stats, 1):
            label = f"Seg {i}: {amount:.0f}/{args.ship_cap:.0f}"
            if amount > args.ship_cap:
                label += " OVER"
            legend_labels.append(label)
    elif args.legend:
        legend_labels = []
        for i, (_start, _end, _stations, _dist, amount) in enumerate(stats, 1):
            legend_labels.append(f"Seg {i}: {amount:.0f}")

    drawTour(letour, LatLonRad, Type, Amount, DistMtrx, FsbleMtrx,
             legend_labels=legend_labels,
             save_path=args.save, show=not bool(args.save), title=title)
    if args.tikz or args.tikz_out:
        tikz_code = drawTour_tikz(letour, LatLonRad, Type, Amount, DistMtrx, FsbleMtrx,
                                  title=title)
        if args.tikz_out:
            with open(args.tikz_out, "w", encoding="utf8") as f:
                f.write(tikz_code)
        if args.tikz:
            print(tikz_code)


if __name__ == "__main__":
    main()
