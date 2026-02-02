#!/usr/bin/env python3
import argparse
import csv
import math
import re
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
    avg_seg_changed = (sum(seg_changed_by_pass) / len(seg_changed_by_pass)) if seg_changed_by_pass else 0.0
    max_seg_changed = max(seg_changed_by_pass) if seg_changed_by_pass else 0
    avg_nseg = sum(nseg_by_pass) / len(nseg_by_pass)
    final_nseg = nseg_by_pass[-1]

    cap_solves = cap_accept = 0
    cap_gap_avg = None
    cap_time_avg = None
    if cap_log:
        cap_solves = len(cap_log)
        cap_accept = sum(1 for r in cap_log if inum(r.get("changed", 0)) == 1)
        gaps = [fnum(r.get("gap", "")) for r in cap_log]
        gaps = [g for g in gaps if g is not None and not math.isnan(g)]
        if gaps:
            cap_gap_avg = sum(gaps) / len(gaps)
        times = [fnum(r.get("runtime", "")) for r in cap_log]
        times = [t for t in times if t is not None and not math.isnan(t)]
        if times:
            cap_time_avg = sum(times) / len(times)

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
        "max_seg_changed": max_seg_changed,
        "avg_nseg": avg_nseg,
        "final_nseg": final_nseg,
        "cap_solves": cap_solves,
        "cap_accept": cap_accept,
        "cap_gap_avg": cap_gap_avg,
        "cap_time_avg": cap_time_avg,
        "per_pass": {
            "nseg": nseg_by_pass,
            "seg_changed": seg_changed_by_pass,
            "total": total,
            "best": best,
        },
    }


def fmt(val, digits=3):
    if val is None or (isinstance(val, float) and math.isnan(val)):
        return "nan"
    if isinstance(val, int):
        return str(val)
    return f"{val:.{digits}f}"


def latex_table(rows):
    header = (
        "run & passes & baseline & best & final & improv\\% & "
        "passes chg & avg seg chg & max seg chg & final nseg & "
        "cap acc/solves & cap gap avg \\\\"
    )
    lines = [
        "\\begin{tabular}{r r r r r r r r r r r r}",
        "\\hline",
        header,
        "\\hline",
    ]
    for r in rows:
        cap_acc = f"{r['cap_accept']}/{r['cap_solves']}" if r["cap_solves"] else "0/0"
        line = " & ".join([
            str(r["run"]),
            str(r["passes"]),
            fmt(r["base_total"]),
            fmt(r["best_total"]),
            fmt(r["final_total"]),
            fmt(r["improve_pct"], 2),
            str(r["passes_changed"]),
            fmt(r["avg_seg_changed"], 2),
            str(r["max_seg_changed"]),
            fmt(r["final_nseg"], 0),
            cap_acc,
            fmt(r["cap_gap_avg"], 3),
        ]) + " \\\\"
        lines.append(line)
    lines.append("\\hline")
    lines.append("\\end{tabular}")
    return "\n".join(lines)


def latex_per_pass(run_id, summary):
    per = summary["per_pass"]
    lines = [
        "\\begin{tabular}{r r r r r}",
        "\\hline",
        f"pass & segs chg & nseg & total & best \\\\",
        "\\hline",
    ]
    for i, (nseg, total, best) in enumerate(zip(per["nseg"], per["total"], per["best"])):
        if i == 0:
            seg_chg = 0
        else:
            seg_chg = per["seg_changed"][i - 1]
        lines.append(
            " & ".join([
                str(i),
                str(seg_chg),
                str(nseg),
                fmt(total),
                fmt(best),
            ]) + " \\\\"
        )
    lines.append("\\hline")
    lines.append("\\end{tabular}")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="Summarize cap_* runs and emit a LaTeX table.")
    parser.add_argument("--sol-dir", default="sol", help="Directory with cap_*.csv files.")
    parser.add_argument("--per-pass", action="store_true", help="Include a per-pass table for each run.")
    parser.add_argument("--out", default="", help="Write LaTeX output to a file instead of stdout.")
    args = parser.parse_args()

    sol_dir = Path(args.sol_dir)
    if not sol_dir.exists():
        raise SystemExit(f"Missing sol dir: {sol_dir}")

    run_files = {}
    for path in sol_dir.glob("cap_*.csv"):
        if path.name.endswith(".cap.csv"):
            continue
        m = re.match(r"cap_(\\d+)\\.csv$", path.name)
        if not m:
            continue
        run_files[int(m.group(1))] = path

    summaries = []
    for run_id in sorted(run_files):
        cap_csv = load_cap_csv(run_files[run_id])
        if not cap_csv:
            continue
        cap_log = load_cap_log(sol_dir / f"cap_{run_id}.cap.csv")
        summaries.append(summarize_run(run_id, cap_csv, cap_log))

    if not summaries:
        raise SystemExit("No cap_*.csv files found")

    out = []
    out.append(latex_table(summaries))
    if args.per_pass:
        for s in summaries:
            out.append("")
            out.append(f"% Run {s['run']} per-pass")
            out.append(latex_per_pass(s["run"], s))

    output = "\n".join(out)
    if args.out:
        Path(args.out).write_text(output)
    else:
        print(output)


if __name__ == "__main__":
    main()
