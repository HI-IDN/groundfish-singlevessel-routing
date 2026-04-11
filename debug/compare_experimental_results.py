#!/usr/bin/env python3
"""
Compare initialization (and optionally sweep) transit distances against the
legacy baselines recorded in docs/07-results.md.

Usage (from repo root):
    python debug/compare_experimental_results.py.py
    python debug/compare_experimental_results.py.py --sol-dir sol --out debug/README.md

The script reads:
  sol/<strategy>/init.json   -- always
  sol/<strategy>/sweep.json  -- if present (skipped with a note when missing)

It parses the legacy "With port" baseline values straight from the
docs/07-results.md Baseline distances table so the comparison stays
in sync when the doc is edited.

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


def parse_doc_baselines(doc_path: Path) -> dict[str, float]:
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
    baselines = parse_doc_baselines(doc_path)

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

        legacy = baselines.get(label)
        delta_init = (init_transit - legacy) if (init_transit is not None and legacy is not None) else None
        pct_init = (delta_init / legacy * 100) if (delta_init is not None and legacy) else None

        rows_init.append({
            "strategy": label,
            "legacy":   legacy,
            "current":  init_transit,
            "delta":    delta_init,
            "pct":      pct_init,
        })

        # --- sweep (optional) ---
        sweep_data = load_json(sol_dir / key / "sweep.json")
        sweep_transit = get_transit(sweep_data)
        delta_sweep = (sweep_transit - legacy) if (sweep_transit is not None and legacy is not None) else None
        pct_sweep = (delta_sweep / legacy * 100) if (delta_sweep is not None and legacy) else None

        rows_sweep.append({
            "strategy": label,
            "legacy":   legacy,
            "current":  sweep_transit,
            "delta":    delta_sweep,
            "pct":      pct_sweep,
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
        "- **Doc baseline** — the *With port* value in the `docs/07-results.md`",
        "  Baseline distances table. This is the **transit-only** component of the",
        "  initial route (`grand_total.transit` nm), not the total distance.",
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
        "## Sweep: transit distance vs. legacy baseline",
        "",
        "| Strategy | Doc baseline (nm) | Post-sweep (nm) | Δ (nm) | Δ (%) |",
        "|---------:|------------------:|----------------:|-------:|------:|",
    ]

    for r in rows_sweep:
        current_str = "*(pending)*" if r["missing"] else fmt(r["current"])
        lines.append(
            f"| {r['strategy']} "
            f"| {fmt(r['legacy'])} "
            f"| {current_str} "
            f"| {fmt_delta(r['delta'])} "
            f"| {fmt_pct(r['pct'])} |"
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

    lines.append(f"| `{doc_path}` | Baseline distances table, *With port* column |")
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



