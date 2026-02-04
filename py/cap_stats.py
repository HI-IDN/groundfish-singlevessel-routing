#!/usr/bin/env python3
import argparse
import csv
import math
import re
import statistics
import sys
from pathlib import Path


def fnum(val):
    try:
        return float(val)
    except Exception:
        return None


def inum(val):
    try:
        return int(val)
    except Exception:
        return None


def is_nan(x):
    return x is None or (isinstance(x, float) and math.isnan(x))


def segment_columns(header, suffix):
    return [c for c in header if c.endswith(suffix)]


def count_segments(row, seg_count):
    nseg = 0
    for s in range(1, seg_count + 1):
        n = inum(row.get(f"seg{s}_stations", ""))
        d = fnum(row.get(f"seg{s}_distance", ""))
        if n is not None and n > 0:
            nseg += 1
        elif d is not None and not math.isnan(d):
            nseg += 1
    return nseg


def segments_changed(prev, cur, seg_count, tol=1e-6):
    changed = 0
    for s in range(1, seg_count + 1):
        d0 = fnum(prev.get(f"seg{s}_distance", ""))
        d1 = fnum(cur.get(f"seg{s}_distance", ""))
        n0 = inum(prev.get(f"seg{s}_stations", ""))
        n1 = inum(cur.get(f"seg{s}_stations", ""))
        a0 = fnum(prev.get(f"seg{s}_amount", ""))
        a1 = fnum(cur.get(f"seg{s}_amount", ""))

        def neq(a, b):
            if is_nan(a) or is_nan(b):
                return False
            if isinstance(a, float) or isinstance(b, float):
                return abs(a - b) > tol
            return a != b

        if neq(d0, d1) or neq(n0, n1) or neq(a0, a1):
            changed += 1
    return changed


def load_cap_csv(path):
    with path.open() as f:
        rows = list(csv.DictReader(f))
    if not rows:
        return None
    header = rows[0].keys()
    seg_count = len(segment_columns(header, "_distance"))
    return rows, seg_count


def load_cap_log(path):
    if not path.exists():
        return None
    with path.open() as f:
        rows = list(csv.DictReader(f))
    return rows


def summarize_run(run_id, cap_csv, cap_log):
    rows, seg_count = cap_csv
    passes = [inum(r["attempts"]) for r in rows]
    total = [fnum(r["total"]) for r in rows]
    best = [fnum(r["best"]) for r in rows]
    changed_flag = [inum(r["changed"]) for r in rows]
    mut_success = [inum(r.get("mut_success", 0)) for r in rows]
    mut_attempts = [inum(r.get("mut_attempts", 0)) for r in rows]
    elapsed = [fnum(r.get("elapsed_sec", None)) for r in rows]

    base_total = total[0]
    final_total = total[-1]
    best_total = min(b for b in best if b is not None)
    best_pass = best.index(best_total)
    improve_abs = (base_total - best_total) if base_total is not None else None
    improve_pct = (improve_abs / base_total * 100.0) if base_total else None

    nseg_by_pass = [count_segments(r, seg_count) for r in rows]
    seg_changed_by_pass = []
    for i in range(1, len(rows)):
        seg_changed_by_pass.append(segments_changed(rows[i - 1], rows[i], seg_count))

    passes_changed = sum(1 for c in changed_flag if c)
    seg_series = seg_changed_by_pass if seg_changed_by_pass else [0]
    avg_seg_changed = (sum(seg_series) / len(seg_series)) if seg_series else 0.0
    max_seg_changed = max(seg_series) if seg_series else 0
    min_seg_changed = min(seg_series) if seg_series else 0
    med_seg_changed = statistics.median(seg_series) if seg_series else 0
    avg_nseg = sum(nseg_by_pass) / len(nseg_by_pass)
    final_nseg = nseg_by_pass[-1]

    cap_solves = cap_accept = 0
    cap_gap_avg = None
    cap_gap_min = None
    cap_gap_max = None
    cap_gap_std = None
    cap_time_avg = None
    cap_time_sum = None
    cap_by_pass = {}
    if cap_log:
        cap_solves = len(cap_log)
        cap_accept = sum(1 for r in cap_log if inum(r.get("changed", 0)) == 1)
        gaps = [fnum(r.get("gap", "")) for r in cap_log]
        gaps = [g for g in gaps if g is not None and math.isfinite(g) and g >= 0.0 and g <= 10.0]
        if gaps:
            cap_gap_avg = sum(gaps) / len(gaps)
            cap_gap_min = min(gaps)
            cap_gap_max = max(gaps)
            cap_gap_std = statistics.pstdev(gaps) if len(gaps) > 1 else 0.0
        times = [fnum(r.get("runtime", "")) for r in cap_log]
        times = [t for t in times if t is not None and not math.isnan(t)]
        if times:
            cap_time_avg = sum(times) / len(times)
            cap_time_sum = sum(times)
        for r in cap_log:
            p = inum(r.get("pass", 0))
            if p is None:
                continue
            bucket = cap_by_pass.setdefault(p, {
                "solves": 0,
                "accept": 0,
                "no_inc": 0,
                "gap_vals": [],
                "time_vals": [],
            })
            bucket["solves"] += 1
            if inum(r.get("changed", 0)) == 1:
                bucket["accept"] += 1
            if inum(r.get("solcount", 0)) == 0:
                bucket["no_inc"] += 1
            g = fnum(r.get("gap", ""))
            if g is not None and math.isfinite(g) and g >= 0.0 and g <= 10.0:
                bucket["gap_vals"].append(g)
            t = fnum(r.get("runtime", ""))
            if t is not None and math.isfinite(t):
                bucket["time_vals"].append(t)

    run_elapsed = None
    if elapsed:
        for val in reversed(elapsed):
            if val is not None and not math.isnan(val):
                run_elapsed = val
                break

    return {
        "run": run_id,
        "passes": passes[-1],
        "base_total": base_total,
        "best_total": best_total,
        "best_pass": best_pass,
        "final_total": final_total,
        "improve_abs": improve_abs,
        "improve_pct": improve_pct,
        "passes_changed": passes_changed,
        "avg_seg_changed": avg_seg_changed,
        "min_seg_changed": min_seg_changed,
        "med_seg_changed": med_seg_changed,
        "max_seg_changed": max_seg_changed,
        "avg_nseg": avg_nseg,
        "final_nseg": final_nseg,
        "cap_solves": cap_solves,
        "cap_accept": cap_accept,
        "cap_gap_avg": cap_gap_avg,
        "cap_gap_min": cap_gap_min,
        "cap_gap_max": cap_gap_max,
        "cap_gap_std": cap_gap_std,
        "cap_time_avg": cap_time_avg,
        "cap_time_sum": cap_time_sum,
        "run_elapsed": run_elapsed,
        "cap_by_pass": cap_by_pass,
        "per_pass": {
            "nseg": nseg_by_pass,
            "seg_changed": seg_changed_by_pass,
            "total": total,
            "best": best,
            "mut_success": mut_success,
            "mut_attempts": mut_attempts,
            "elapsed": elapsed,
        },
    }


def fmt(val, digits=3):
    if val is None or (isinstance(val, float) and math.isnan(val)):
        return "nan"
    if isinstance(val, int):
        if digits <= 0:
            return str(val)
        return f"{val:.{digits}f}"
    return f"{val:.{digits}f}"


def latex_table(rows, base_total_all=None, final_nseg_all=None):
    if base_total_all is None:
        base_total_all = []
    if final_nseg_all is None:
        final_nseg_all = []
    base_same = bool(base_total_all) and len(set(base_total_all)) == 1
    nseg_same = bool(final_nseg_all) and len(set(final_nseg_all)) == 1
    if base_same:
        header_top = (
            "time & num. & final & seg change & accepted/ & MIP gap (\\%) & run time \\\\"
        )
        header_sub = (
            "limit (s) & passes & distance & mean/med/max & solves & min/avg/max/std & (min) \\\\"
        )
        colspec = "r r r r r r r"
        caption = (
            "Summary of cap runs. Baseline total distance (pass 0) is "
            f"{fmt(base_total_all[0])} for all runs. "
        )
        if nseg_same:
            caption += f"Final number of segments is {fmt(final_nseg_all[0], 0)} for all runs. "
        elif final_nseg_all:
            caption += (
                f"Final number of segments ranges from "
                f"{fmt(min(final_nseg_all), 0)} to {fmt(max(final_nseg_all), 0)}. "
            )
        caption += (
            "Columns show capacity MIP time limit (seconds), number of passes, final total distance, "
            "mean/median/max segments changed per pass, accepted/total capacity solves, "
            "capacity MIP gap min/avg/max/std (percent), and total run time (minutes). "
            "Initialization uses --init_timelimit if set (otherwise no limit)."
        )
    else:
        header_top = (
            "time & num. & baseline & final & seg change & accepted/ & MIP gap (\\%) & run time \\\\"
        )
        header_sub = (
            "limit (s) & passes &  &  & mean/med/max & solves & min/avg/max/std & (min) \\\\"
        )
        colspec = "r r r r r r r r"
        caption = (
            "Summary of cap runs. Columns show capacity MIP time limit (seconds), number of passes, "
            "baseline total distance (pass 0), "
            "final total distance, mean/median/max segments changed per pass, "
            "accepted/total capacity solves, capacity MIP gap "
            "min/avg/max/std (percent), and total run time (minutes). "
            "Initialization uses --init_timelimit if set (otherwise no limit)."
        )
    lines = [
        "\\begin{table}[ht]",
        "\\centering",
        f"\\caption{{{caption}}}",
        f"\\begin{{tabular}}{{{colspec}}}",
        "\\hline",
        header_top,
        header_sub,
        "\\hline",
    ]
    for r in rows:
        cap_acc = f"{r['cap_accept']}/{r['cap_solves']}" if r["cap_solves"] else "0/0"
        seg_chg = "/".join([
            fmt(r["avg_seg_changed"], 1),
            fmt(r["med_seg_changed"], 1),
            fmt(r["max_seg_changed"], 1),
        ])
        gap_min = fmt(r["cap_gap_min"] * 100.0 if r["cap_gap_min"] is not None else None, 0)
        gap_avg = fmt(r["cap_gap_avg"] * 100.0 if r["cap_gap_avg"] is not None else None, 0)
        gap_max = fmt(r["cap_gap_max"] * 100.0 if r["cap_gap_max"] is not None else None, 0)
        gap_std = fmt(r["cap_gap_std"] * 100.0 if r["cap_gap_std"] is not None else None, 0)
        gap_stats = f"{gap_min}/{gap_avg}/{gap_max}/{gap_std}"
        run_time = fmt(r["run_elapsed"] / 60.0 if r["run_elapsed"] is not None else None, 0)
        items = [
            str(r["run"]),
            str(r["passes"]),
        ]
        if not base_same:
            items.append(fmt(r["base_total"]))
        items.extend([
            fmt(r["final_total"]),
            seg_chg,
            cap_acc,
            gap_stats,
            run_time,
        ])
        line = " & ".join(items) + " \\\\"
        lines.append(line)
    lines.append("\\hline")
    lines.append("\\end{tabular}")
    lines.append("\\end{table}")
    return "\n".join(lines)


def latex_per_pass(run_id, summary):
    per = summary["per_pass"]
    cap_by_pass = summary.get("cap_by_pass", {})
    lines = [
        "\\begin{tabular}{r r r r r r r r r r r r}",
        "\\hline",
        f"pass & total & best & $\\Delta$ & seg chg & nseg & mut & cap acc/solves & cap no-inc & cap gap (\\%) & cap time & elapsed \\\\",
        "\\hline",
    ]
    for i, (nseg, total, best) in enumerate(zip(per["nseg"], per["total"], per["best"])):
        if i == 0:
            seg_chg = 0
            delta = None
        else:
            seg_chg = per["seg_changed"][i - 1]
            prev = per["total"][i - 1]
            delta = None if prev is None or total is None else (total - prev)
        mut_s = per.get("mut_success", [0]*len(per["total"]))[i] or 0
        mut_a = per.get("mut_attempts", [0]*len(per["total"]))[i] or 0
        mut_str = f"{mut_s}/{mut_a}"
        cap = cap_by_pass.get(i, None)
        if cap:
            cap_acc = f"{cap['accept']}/{cap['solves']}"
            cap_no = str(cap["no_inc"])
            if cap["gap_vals"]:
                cap_gap = sum(cap["gap_vals"]) / len(cap["gap_vals"]) * 100.0
            else:
                cap_gap = None
            if cap["time_vals"]:
                cap_time = sum(cap["time_vals"]) / len(cap["time_vals"])
            else:
                cap_time = None
        else:
            cap_acc = "0/0"
            cap_no = "0"
            cap_gap = None
            cap_time = None
        elapsed = per.get("elapsed", [None]*len(per["total"]))[i]
        lines.append(
            " & ".join([
                str(i),
                fmt(total),
                fmt(best),
                fmt(delta),
                str(seg_chg),
                str(nseg),
                mut_str,
                cap_acc,
                cap_no,
            fmt(cap_gap, 0),
                fmt(cap_time, 2),
                fmt(elapsed, 2),
            ]) + " \\\\"
        )
    lines.append("\\hline")
    lines.append("\\end{tabular}")
    return "\n".join(lines)


def print_time_summary(summaries, init_time_limit, out):
    rows = []
    for s in summaries:
        run = s.get("run")
        if not isinstance(run, int):
            continue
        solves = s.get("cap_solves", 0) or 0
        actual_sec = s.get("run_elapsed")
        mip_ub = run * solves
        mip_init_ub = mip_ub + init_time_limit
        rows.append((run, solves, mip_ub, mip_init_ub, actual_sec))

    if not rows:
        return

    print(f"Assumed init time limit: {int(init_time_limit)}s", file=out)
    print(
        f"{'TL(s)':>6} {'solves':>6} {'MIP UB (min)':>12} "
        f"{'MIP+init UB (min)':>18} {'actual (min)':>12} {'actual (s)':>12}",
        file=out,
    )
    for tl, solves, mip_ub, mip_init_ub, actual_sec in rows:
        actual_min = (actual_sec / 60.0) if actual_sec is not None else None
        actual_min_str = f"{actual_min:12.1f}" if actual_min is not None else f"{'nan':>12}"
        actual_sec_str = f"{actual_sec:12.1f}" if actual_sec is not None else f"{'nan':>12}"
        print(
            f"{tl:6d} {solves:6d} {mip_ub/60:12.1f} {mip_init_ub/60:18.1f} "
            f"{actual_min_str} {actual_sec_str}",
            file=out,
        )


def main():
    parser = argparse.ArgumentParser(description="Summarize cap_* runs and emit a LaTeX table.")
    parser.add_argument("csv_paths", nargs="*", help="Optional list of <base>_*.csv files.")
    parser.add_argument("--base", default="cap", help="Base name for sol files (e.g., cap, capmut).")
    parser.add_argument("--sol-dir", default="sol", help="Directory with <base>_*.csv files.")
    parser.add_argument("--per-pass", action="store_true", help="Include a per-pass table for each run.")
    parser.add_argument("--out", default="", help="Write LaTeX output to a file instead of stdout.")
    parser.add_argument("--time-summary", action="store_true",
                        help="Print time-limit upper bounds vs actual run time to stderr.")
    parser.add_argument("--compare-times", action="store_true",
                        help="Alias for --time-summary.")
    parser.add_argument("--init-time-limit", type=float, default=300.0,
                        help="Initialization time limit (seconds) for upper-bound calc.")
    args = parser.parse_args()

    run_files = {}
    if args.csv_paths:
        for raw in args.csv_paths:
            path = Path(raw)
            if not path.exists():
                raise SystemExit(f"Missing file: {path}")
            if path.name.endswith(".cap.csv"):
                continue
            run_id = None
            m = re.match(r"cap_(\d+)\.csv$", path.name)
            if m:
                run_id = int(m.group(1))
            m = re.match(r"capmut_(\d+)\.csv$", path.name)
            if m:
                run_id = int(m.group(1))
            if run_id is None:
                run_id = path.stem
            run_files[run_id] = path
    else:
        sol_dir = Path(args.sol_dir)
        if not sol_dir.exists():
            raise SystemExit(f"Missing sol dir: {sol_dir}")

        pattern = f"{args.base}_*.csv"
        rx = re.compile(rf"{re.escape(args.base)}_(\d+)\.csv$")
        for path in sol_dir.glob(pattern):
            if path.name.endswith(".cap.csv"):
                continue
            m = rx.match(path.name)
            if not m:
                continue
            run_files[int(m.group(1))] = path

    summaries = []
    for run_id in sorted(run_files, key=lambda x: (isinstance(x, str), x)):
        cap_csv = load_cap_csv(run_files[run_id])
        if not cap_csv:
            continue
        cap_log = None
        cap_path = run_files[run_id]
        cap_log_path = cap_path.with_suffix(".cap.csv")
        if cap_log_path.exists():
            cap_log = load_cap_log(cap_log_path)
        summaries.append(summarize_run(run_id, cap_csv, cap_log))

    if not summaries:
        raise SystemExit("No cap_*.csv files found")

    if args.time_summary or args.compare_times:
        print_time_summary(summaries, args.init_time_limit, sys.stderr)

    out = []
    base_total_all = [s["base_total"] for s in summaries if s.get("base_total") is not None]
    final_nseg_all = [s["final_nseg"] for s in summaries if s.get("final_nseg") is not None]
    out.append(latex_table(summaries, base_total_all=base_total_all, final_nseg_all=final_nseg_all))
    if args.per_pass:
        for s in summaries:
            out.append("")
            out.append(f"% Time limit {s['run']} per-pass")
            out.append(latex_per_pass(s["run"], s))

    output = "\n".join(out)
    if args.out:
        Path(args.out).write_text(output)
    else:
        print(output)


if __name__ == "__main__":
    main()
