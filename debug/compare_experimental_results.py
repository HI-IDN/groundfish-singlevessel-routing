#!/usr/bin/env python3
"""
Compare initialization and sweep transit distances against the
legacy reference values recorded in docs/07-results.md.

Usage (from repo root):
    python debug/compare_experimental_results.py.py
    python debug/compare_experimental_results.py.py --sol-dir sol --out debug/README.md

The script reads:
  sol/<strategy>/init.json   -- always
  sol/<strategy>/sweep.json  -- if present (skipped with a note when missing)

It parses:
  - init baselines from the docs/07-results.md Baseline distances table
  - sweep baselines from the best, mean, and worst `Final distance`
    values in each docs/07-results.md `MH-<init>` sweep table
so the comparison stays in sync when the doc is edited.

Output is written to --out (default: debug/compare_legacy.md) and also
printed to stdout.
"""

import argparse
import json
import re
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Strategies and their label in docs/07-results.md
# ---------------------------------------------------------------------------

STRATEGIES = [
    {"key": "nn", "doc_label": "MH-NN"},
    {"key": "ge", "doc_label": "MH-GE"},
    {"key": "ci", "doc_label": "MH-CI"},
    {"key": "noport", "doc_label": "MH-noport"},
]


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def load_json(path: Path) -> dict | None:
    """Load a JSON file; return None if missing."""
    if not path.exists():
        return None
    with open(path, encoding="utf-8") as fh:
        return json.load(fh)


def get_transit(data: dict) -> float | None:
    """
    Extract grand_total.transit from the solution variant named by
    summary.distance_nm.final or falling back to 'capacity-feasible'.
    Returns None if the key path is absent.
    """
    if data is None:
        return None
    sol = data.get("solution", {})
    # Prefer the variant flagged as final in the summary
    final_variant = (
        data.get("summary", {}).get("status", {}).get("final")
        or "capacity-feasible"
    )
    variant = sol.get(final_variant) or sol.get("capacity-feasible")
    if variant is None:
        return None
    return variant.get("distance_nm", {}).get("grand_total", {}).get("transit")


def parse_doc_init_baselines(doc_path: Path) -> dict[str, float]:
    """
    Parse the 'Baseline distances' table from docs/07-results.md.
    Returns {doc_label: with_port_nm} for rows where With port is a number.

    Expected table format:
    | Notation | Variant | Solver limits | No port | With port |
    | MH-NN | ... | ... | N/A | 6911.67 |
    """
    baselines: dict[str, float] = {}
    if not doc_path.exists():
        print(f"  WARNING: doc not found: {doc_path}", file=sys.stderr)
        return baselines

    in_table = False
    with open(doc_path, encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            # Detect start of baseline table by its header
            if re.search(r"\|\s*Notation\s*\|.*With port", line, re.IGNORECASE):
                in_table = True
                continue
            if not in_table:
                continue
            # Stop at blank line or next heading
            if not line.startswith("|") or line.startswith("| ---") or line.startswith("|---"):
                if line and not line.startswith("|"):
                    in_table = False
                continue
            cols = [c.strip() for c in line.strip("|").split("|")]
            if len(cols) < 5:
                continue
            notation = cols[0]
            with_port = cols[4]
            try:
                baselines[notation] = float(with_port.replace(",", ""))
            except ValueError:
                pass  # N/A, timeout, etc.

    return baselines


def parse_doc_sweep_baselines(doc_path: Path) -> dict[str, dict[str, float]]:
    """
    Parse best/mean/worst 'Final distance' from each strategy section table:
      ## MH-NN
      ## MH-GE
      ## MH-CI

    Returns {doc_label: {"best": ..., "mean": ..., "worst": ...}}.
    """
    collected: dict[str, list[float]] = {}
    current_label: str | None = None
    in_table = False
    final_distance_col: int | None = None

    if not doc_path.exists():
        print(f"  WARNING: doc not found: {doc_path}", file=sys.stderr)
        return baselines

    strategy_labels = {s["doc_label"] for s in STRATEGIES}

    with open(doc_path, encoding="utf-8") as fh:
        for raw_line in fh:
            line = raw_line.strip()

            heading = re.match(r"^##\s+(MH-[A-Za-z0-9_-]+)\s*$", line)
            if heading:
                label = heading.group(1)
                current_label = label if label in strategy_labels else None
                if current_label is not None and current_label not in collected:
                    collected[current_label] = []
                in_table = False
                final_distance_col = None
                continue

            if current_label is None:
                continue

            if re.match(r"^##\s+", line):
                current_label = None
                in_table = False
                final_distance_col = None
                continue

            if not in_table and line.startswith("|") and "Final distance" in line:
                cols = [c.strip() for c in line.strip("|").split("|")]
                for idx, col in enumerate(cols):
                    if col.lower() == "final distance":
                        final_distance_col = idx
                        break
                in_table = final_distance_col is not None
                continue

            if not in_table:
                continue

            if not line.startswith("|"):
                in_table = False
                final_distance_col = None
                continue

            if line.startswith("| ---") or line.startswith("|---"):
                continue

            cols = [c.strip() for c in line.strip("|").split("|")]
            if final_distance_col is None or final_distance_col >= len(cols):
                continue

            try:
                value = float(cols[final_distance_col].replace(",", ""))
            except ValueError:
                continue

            collected[current_label].append(value)

    baselines: dict[str, dict[str, float]] = {}
    for label, values in collected.items():
        if not values:
            continue
        baselines[label] = {
            "best": min(values),
            "mean": sum(values) / len(values),
            "worst": max(values),
        }
    return baselines


def fmt(val: float | None, decimals: int = 2) -> str:
    if val is None:
        return "—"
    return f"{val:,.{decimals}f}"


def fmt_delta(delta: float | None) -> str:
    if delta is None:
        return "—"
    sign = "−" if delta < 0 else "+"
    return f"{sign}{abs(delta):,.2f}"


def fmt_pct(pct: float | None) -> str:
    if pct is None:
        return "—"
    sign = "−" if pct < 0 else "+"
    return f"{sign}{abs(pct):.1f}%"


# ---------------------------------------------------------------------------
# Report builder
# ---------------------------------------------------------------------------

def build_report(sol_dir: Path, doc_path: Path) -> str:
    init_baselines = parse_doc_init_baselines(doc_path)
    sweep_baselines = parse_doc_sweep_baselines(doc_path)

    rows_init: list[dict] = []
    rows_sweep: list[dict] = []
    haul_values: list[float] = []

    for strat in STRATEGIES:
        key = strat["key"]
        label = strat["doc_label"]

        # --- init ---
        init_data = load_json(sol_dir / key / "init.json")
        init_transit = get_transit(init_data)

        # haul is the same for all variants; grab it once per strategy
        if init_data:
            sol = init_data.get("solution", {})
            for vname in ("capacity-feasible", "baseline-capacity-feasible"):
                v = sol.get(vname)
                if v:
                    h = v.get("distance_nm", {}).get("grand_total", {}).get("haul")
                    if h is not None:
                        haul_values.append(h)
                        break

        init_legacy = init_baselines.get(label)
        delta_init = (init_transit - init_legacy) if (init_transit is not None and init_legacy is not None) else None
        pct_init = (delta_init / init_legacy * 100) if (delta_init is not None and init_legacy) else None

        rows_init.append({
            "strategy": label,
            "legacy":   init_legacy,
            "current":  init_transit,
            "delta":    delta_init,
            "pct":      pct_init,
        })

        # --- sweep (optional) ---
        sweep_data = load_json(sol_dir / key / "sweep.json")
        sweep_transit = get_transit(sweep_data)
        sweep_legacy = sweep_baselines.get(label, {})
        sweep_best = sweep_legacy.get("best")
        sweep_mean = sweep_legacy.get("mean")
        sweep_worst = sweep_legacy.get("worst")

        rows_sweep.append({
            "strategy": label,
            "best":     sweep_best,
            "mean":     sweep_mean,
            "worst":    sweep_worst,
            "current":  sweep_transit,
            "delta_best":  (sweep_transit - sweep_best) if (sweep_transit is not None and sweep_best is not None) else None,
            "pct_best":    ((sweep_transit - sweep_best) / sweep_best * 100) if (sweep_transit is not None and sweep_best) else None,
            "delta_mean":  (sweep_transit - sweep_mean) if (sweep_transit is not None and sweep_mean is not None) else None,
            "pct_mean":    ((sweep_transit - sweep_mean) / sweep_mean * 100) if (sweep_transit is not None and sweep_mean) else None,
            "delta_worst": (sweep_transit - sweep_worst) if (sweep_transit is not None and sweep_worst is not None) else None,
            "pct_worst":   ((sweep_transit - sweep_worst) / sweep_worst * 100) if (sweep_transit is not None and sweep_worst) else None,
            "missing":  sweep_data is None,
        })

    haul_note = (
        f"Haul distance is identical across all strategies "
        f"({fmt(haul_values[0])} nm) because the per-segment MIP TSP "
        f"produces the same optimal haul regardless of initialization."
        if haul_values else
        "Haul distance could not be determined."
    )

    sweep_available = any(not r["missing"] for r in rows_sweep)
    sweep_note = (
        "" if sweep_available
        else "\n> ⚠️  No `sweep.json` files found — sweep section shows no data yet.\n"
    )

    lines = [
        "# Initialization Baseline Comparison: Legacy vs. Current",
        "",
        f"Generated by `debug/compare_experimental_results.py.py`  ",
        f"Doc source: `{doc_path}`  ",
        f"Solution dir: `{sol_dir}`",
        "",
        "## What is being compared",
        "",
        "- **Doc baseline for init** — the *With port* value in the `docs/07-results.md`",
        "  Baseline distances table. This is the **transit-only** component of the",
        "  initial route (`grand_total.transit` nm), not the total distance.",
        "- **Doc sweep references** — the best, mean, and worst `Final distance` values",
        "  reported in the matching `## MH-<init>` section in `docs/07-results.md`.",
        "- **Current init** — `distance_nm.grand_total.transit` from the",
        "  `capacity-feasible` variant in each `sol/<strategy>/init.json`.",
        "- **Current sweep** — same field from `sol/<strategy>/sweep.json`",
        "  (only available after sweep runs complete).",
        "",
        f"> {haul_note}",
        "",
        "## Init: transit distance vs. legacy baseline",
        "",
        "| Strategy | Doc baseline (nm) | Current init (nm) | Δ (nm) | Δ (%) |",
        "|---------:|------------------:|------------------:|-------:|------:|",
    ]

    for r in rows_init:
        lines.append(
            f"| {r['strategy']} "
            f"| {fmt(r['legacy'])} "
            f"| {fmt(r['current'])} "
            f"| {fmt_delta(r['delta'])} "
            f"| {fmt_pct(r['pct'])} |"
        )

    lines += [
        "",
        sweep_note,
        "## Sweep: transit distance vs. legacy sweep range",
        "",
        "| Strategy | Legacy best (nm) | Legacy mean (nm) | Legacy worst (nm) | Post-sweep (nm) | Δ vs best | Δ vs mean | Δ vs worst |",
        "|---------:|-----------------:|-----------------:|------------------:|----------------:|----------:|----------:|-----------:|",
    ]

    for r in rows_sweep:
        current_str = "*(pending)*" if r["missing"] else fmt(r["current"])
        lines.append(
            f"| {r['strategy']} "
            f"| {fmt(r['best'])} "
            f"| {fmt(r['mean'])} "
            f"| {fmt(r['worst'])} "
            f"| {current_str} "
            f"| {fmt_delta(r['delta_best'])} ({fmt_pct(r['pct_best'])}) "
            f"| {fmt_delta(r['delta_mean'])} ({fmt_pct(r['pct_mean'])}) "
            f"| {fmt_delta(r['delta_worst'])} ({fmt_pct(r['pct_worst'])}) |"
        )

    lines += [
        "",
        "## Sources",
        "",
        "| File | Key field |",
        "|------|-----------|",
    ]
    for strat in STRATEGIES:
        k = strat["key"]
        r = next(x for x in rows_init if x["strategy"] == strat["doc_label"])
        lines.append(
            f"| `sol/{k}/init.json` | "
            f"`solution.capacity-feasible.distance_nm.grand_total.transit` = {fmt(r['current'])} nm |"
        )
        sr = next(x for x in rows_sweep if x["strategy"] == strat["doc_label"])
        sweep_val = "*(pending)*" if sr["missing"] else f"{fmt(sr['current'])} nm"
        lines.append(
            f"| `sol/{k}/sweep.json` | "
            f"`solution.capacity-feasible.distance_nm.grand_total.transit` = {sweep_val} |"
        )

    lines.append(f"| `{doc_path}` | Baseline distances table, *With port* column; `MH-*` tables, best/mean/worst `Final distance` |")
    lines.append("")

    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main():
    repo_root = Path(__file__).resolve().parent.parent

    ap = argparse.ArgumentParser(description="Compare init/sweep baselines against legacy doc values")
    ap.add_argument("--sol-dir", default=str(repo_root / "sol"),
                    help="Path to sol/ directory (default: <repo>/sol)")
    ap.add_argument("--doc", default=str(repo_root / "docs" / "07-results.md"),
                    help="Path to results doc (default: <repo>/docs/07-results.md)")
    ap.add_argument("--out", default=str(repo_root / "debug" / "README.md"),
                    help="Output markdown file (default: debug/README.md)")
    args = ap.parse_args()

    sol_dir = Path(args.sol_dir)
    doc_path = Path(args.doc)
    out_path = Path(args.out)

    report = build_report(sol_dir, doc_path)

    print(report)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(report, encoding="utf-8")
    print(f"\n-- wrote {out_path}", file=sys.stderr)


if __name__ == "__main__":
    main()



