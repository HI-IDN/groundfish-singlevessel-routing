/usr/bin/env python3
"""
Convert a legacy src.org solver log file to a new-format JSON solution.

Usage:
  python convert_legacy_log.py \\
      --log        <legacy.log>            (e.g. log/capmut_120.log) \\
      --legacy-db  <legacy_distances.db>   (e.g. dat/legacy_distances.db) \\
      --gsp-db     <gsp.db>                (e.g. dat/gsp.db) \\
      [--out       <output.json>]          (default: debug/legacy_noport_converted.json)

The legacy log stores station indices as ExData loc_index values (ship=0, stations=1..N).
This script bridges them to gsp.db via:
  legacy from_loc_index -> ext_id  (from legacy_distances.db)
  ext_id -> gsp station.id, start_location_id, end_location_id  (from gsp.db)
"""

import argparse
import json
import re
import sqlite3
import sys
from pathlib import Path


# ---------------------------------------------------------------------------
# Log parsing
# ---------------------------------------------------------------------------

def parse_log(log_path: str) -> dict:
    """Extract all relevant fields from a legacy solver log file."""
    result = {
        "boat_name": None,
        "ship_cap": None,
        "mip_rows": None,
        "mip_cols": None,
        "noport_objective": None,
        "noport_runtime_s": None,
        "noport_grb_runtime_s": None,
        "noport_status_code": None,
        "noport_solution_count": None,
        "noport_node_count": None,
        "noport_best_obj": None,
        "noport_best_bound": None,
        "noport_gap_pct": None,
        "noport_station_order": [],   # unsigned loc_indices from "Initial station order"
        "segments": [],               # [{seg_num, end_label, station_ids, signed_ids}]
    }

    segments_by_num = {}
    current_plan_seg = None

    with open(log_path, encoding="utf-8", errors="replace") as fh:
        for raw in fh:
            line = raw.rstrip("\n")

            m = re.match(r"^Ship:\s+(.+)$", line)
            if m:
                result["boat_name"] = m.group(1).strip()

            m = re.match(r"^ShipCap:\s+(\d+)", line)
            if m:
                result["ship_cap"] = int(m.group(1))

            m = re.search(r"Optimize a model with (\d+) rows, (\d+) columns", line)
            if m:
                result["mip_rows"] = int(m.group(1))
                result["mip_cols"] = int(m.group(2))

            m = re.match(r"^Initial no-port objective:\s+([\d.]+)", line)
            if m:
                result["noport_objective"] = float(m.group(1))

            m = re.match(r"^Initial no-port TSP wall time:\s+(\d+)", line)
            if m:
                result["noport_runtime_s"] = int(m.group(1))

            m = re.match(
                r"^Initial no-port TSP stats: status=(\d+) solcount=(\d+) grb_runtime=([\d.]+)",
                line,
            )
            if m:
                result["noport_status_code"] = int(m.group(1))
                result["noport_solution_count"] = int(m.group(2))
                result["noport_grb_runtime_s"] = float(m.group(3))

            m = re.match(r"^Initial station order \(no ports\):\s+(.+)$", line)
            if m:
                result["noport_station_order"] = [int(x) for x in m.group(1).split()]

            # Gurobi summary line
            m = re.search(
                r"Best objective ([\d.e+\-]+), best bound ([\d.e+\-]+), gap ([\d.]+)%",
                line,
            )
            if m:
                result["noport_best_obj"] = float(m.group(1))
                result["noport_best_bound"] = float(m.group(2))
                result["noport_gap_pct"] = float(m.group(3))

            m = re.search(r"Explored (\d+) nodes .* in ([\d.]+) seconds", line)
            if m:
                result["noport_node_count"] = int(m.group(1))
                result["noport_grb_runtime_s"] = float(m.group(2))

            # Segment plan lines  "  Segment N -> ENDPOINT"
            m = re.match(r"^\s+Segment (\d+) -> (.+)$", line)
            if m:
                seg_num = int(m.group(1))
                end_label = m.group(2).strip()
                current_plan_seg = {"seg_num": seg_num, "end_label": end_label,
                                    "station_ids": [], "signed_ids": []}
                segments_by_num[seg_num] = current_plan_seg

            # Segment station list "    stations: 501 500 ..."
            m = re.match(r"^\s+stations:\s+(.+)$", line)
            if m and current_plan_seg is not None:
                current_plan_seg["station_ids"] = [int(x) for x in m.group(1).split()]

            # Segment optimised order "  Segment N order: -501 500 ..."
            m = re.match(r"^\s+Segment (\d+) order:\s+(.+)$", line)
            if m:
                seg_num = int(m.group(1))
                signed = [int(x) for x in m.group(2).split()]
                if seg_num in segments_by_num:
                    segments_by_num[seg_num]["signed_ids"] = signed

    result["segments"] = [segments_by_num[k] for k in sorted(segments_by_num)]
    return result


# ---------------------------------------------------------------------------
# Database lookups
# ---------------------------------------------------------------------------

def build_legacy_loc_to_extid(legacy_db: str) -> dict:
    """
    Return {from_loc_index: ext_id} for all station nodes (from_type=2).
    from_loc_index is 0-based in ExData (ship=0, stations=1..N).
    """
    con = sqlite3.connect(legacy_db)
    cur = con.cursor()
    cur.execute(
        """
        SELECT DISTINCT from_loc_index, from_name
        FROM legacy_distances
        WHERE from_type = 2
        ORDER BY from_loc_index
        """
    )
    rows = cur.fetchall()
    con.close()
    return {loc_idx: name.strip('"') for loc_idx, name in rows}


def build_gsp_maps(gsp_db: str):
    """
    Returns:
      sta_map:  {ext_id: {id, start_location_id, end_location_id}}
      boat_map: {name:   {id, location_id, lat, lon}}
      port_map: {name:   {id, location_id}}
    """
    con = sqlite3.connect(gsp_db)
    cur = con.cursor()

    cur.execute("SELECT ext_id, id, start_location_id, end_location_id FROM stations")
    sta_map = {
        row[0]: {"id": row[1], "start_location_id": row[2], "end_location_id": row[3]}
        for row in cur.fetchall()
    }

    cur.execute(
        "SELECT b.name, b.id, b.location_id, l.lat, l.lon "
        "FROM boats b JOIN locations l ON l.id = b.location_id"
    )
    boat_map = {
        row[0]: {"id": row[1], "location_id": row[2], "lat": row[3], "lon": row[4]}
        for row in cur.fetchall()
    }

    cur.execute("SELECT name, id, location_id FROM ports")
    port_map = {
        row[0]: {"id": row[1], "location_id": row[2]}
        for row in cur.fetchall()
    }

    con.close()
    return sta_map, boat_map, port_map


# ---------------------------------------------------------------------------
# Location sequence builder
# ---------------------------------------------------------------------------

def loc_seq_from_signed(signed_ids: list, legacy_to_gsp: dict,
                        dock_location_id: int) -> list:
    """
    Build the ordered location ID sequence for one segment.
    Positive sid -> forward (start_loc, end_loc).
    Negative sid -> reverse (end_loc, start_loc).
    Wraps with dock_location_id at start and end.
    """
    seq = [dock_location_id]
    missing = []
    for sid in signed_ids:
        key = abs(sid)
        gsp = legacy_to_gsp.get(key)
        if gsp is None:
            missing.append(key)
            continue
        if sid > 0:
            seq.append(gsp["start_location_id"])
            seq.append(gsp["end_location_id"])
        else:
            seq.append(gsp["end_location_id"])
            seq.append(gsp["start_location_id"])
    seq.append(dock_location_id)
    if missing:
        print(f"  WARNING: {len(missing)} legacy loc_indices had no gsp mapping: "
              f"{missing[:10]}{'...' if len(missing)>10 else ''}", file=sys.stderr)
    return seq


def resolve_port_location(end_label: str, port_map: dict):
    """
    Parse port name from labels like 'PORT-588("Ólafsvík")' or 'START' / 'BOAT-END'.
    Returns location_id from gsp.db or None.
    """
    m = re.search(r'\("([^"]+)"\)', end_label)
    if m:
        name = m.group(1)
        if name in port_map:
            return port_map[name]["location_id"]
        for k, v in port_map.items():
            if k.lower() == name.lower():
                return v["location_id"]
    return None


# ---------------------------------------------------------------------------
# Main conversion
# ---------------------------------------------------------------------------

def convert(log_path: str, legacy_db: str, gsp_db: str, out_path: str):
    print(f"Parsing log:        {log_path}")
    log = parse_log(log_path)

    print(f"Loading legacy DB:  {legacy_db}")
    legacy_loc_to_extid = build_legacy_loc_to_extid(legacy_db)
    print(f"  station loc_index entries: {len(legacy_loc_to_extid)}")

    print(f"Loading gsp DB:     {gsp_db}")
    sta_map, boat_map, port_map = build_gsp_maps(gsp_db)
    print(f"  gsp stations: {len(sta_map)}  boats: {len(boat_map)}  ports: {len(port_map)}")

    # Combined mapping: legacy loc_index -> gsp station info
    legacy_to_gsp = {}
    unresolved_extid = []
    for loc_idx, ext_id in legacy_loc_to_extid.items():
        if ext_id in sta_map:
            legacy_to_gsp[loc_idx] = sta_map[ext_id]
        else:
            unresolved_extid.append((loc_idx, ext_id))
    print(f"  resolved {len(legacy_to_gsp)} / {len(legacy_loc_to_extid)} station mappings")
    if unresolved_extid:
        print(f"  WARNING: {len(unresolved_extid)} ext_ids not found in gsp.db: "
              f"{unresolved_extid[:5]}", file=sys.stderr)

    # Resolve boat
    boat_name = log["boat_name"] or ""
    boat_name_clean = boat_name.strip('"')
    boat_info = boat_map.get(boat_name_clean, {})
    dock_loc_id = boat_info.get("location_id")
    if dock_loc_id is None:
        if boat_map:
            boat_info = next(iter(boat_map.values()))
            dock_loc_id = boat_info["location_id"]
            print(f"  WARNING: boat '{boat_name_clean}' not found; using first boat", file=sys.stderr)
        else:
            dock_loc_id = -1
            print("  WARNING: no boats in gsp.db", file=sys.stderr)

    # ---- legacy-converted: single-segment no-port tour ----------------------
    np_order = log["noport_station_order"]   # unsigned loc_indices
    np_loc_seq = loc_seq_from_signed(np_order, legacy_to_gsp, dock_loc_id)
    np_station_ids = [legacy_to_gsp[i]["id"] for i in np_order if i in legacy_to_gsp]

    legacy_converted = {
        "variant": "legacy-converted",
        "feasible": False,
        "note": "no-port TSP optimal from legacy src.org solver (all stations treated as forward; sign data unavailable for raw order)",
        "tour_segments_location_ids": [np_loc_seq],
        "dock_location_ids": [dock_loc_id, dock_loc_id],
        "tour_segments_station_ids": [np_station_ids],
        "segment_count": 1,
        "tour_length": [len(np_station_ids)],
        "segment_distance_nm": [log["noport_objective"]],
        "total_distance_nm": log["noport_objective"],
    }

    # ---- segmented plan (port-inserted, signed) -----------------------------
    seg_solutions = {}
    if log["segments"]:
        seg_locs = []
        seg_station_ids = []
        seg_signed = []
        seg_docks = [dock_loc_id]

        for seg in log["segments"]:
            signed = seg.get("signed_ids") or [i for i in seg["station_ids"]]
            loc_seq = loc_seq_from_signed(signed, legacy_to_gsp, dock_loc_id)
            seg_locs.append(loc_seq)
            seg_station_ids.append(
                [legacy_to_gsp[abs(s)]["id"] for s in signed if abs(s) in legacy_to_gsp]
            )
            seg_signed.append(signed)

            end_label = seg["end_label"]
            if "BOAT-END" in end_label or end_label == "END":
                seg_docks.append(dock_loc_id)
            else:
                port_loc = resolve_port_location(end_label, port_map)
                seg_docks.append(port_loc if port_loc is not None else dock_loc_id)

        seg_solutions["segmented"] = {
            "variant": "segmented",
            "feasible": None,
            "note": "port-inserted plan from legacy log segment breakdown",
            "tour_segments_location_ids": seg_locs,
            "dock_location_ids": seg_docks,
            "tour_segments_station_ids": seg_station_ids,
            "signed_station_ids_per_segment": seg_signed,
            "segment_count": len(log["segments"]),
            "tour_length": [len(s) for s in seg_station_ids],
            "segment_distance_nm": [None] * len(log["segments"]),
            "total_distance_nm": None,
        }

    # ---- assemble JSON -------------------------------------------------------
    out = {
        "metadata": {
            "note": "Converted from legacy src.org log by convert_legacy_log.py",
            "source_log": str(log_path),
            "legacy_db": str(legacy_db),
            "gsp_db": str(gsp_db),
            "solver_version": "legacy_src.org",
            "mode": "noport_mip",
            "strategy": "noport",
            "boat_id": boat_info.get("id"),
            "boat_name": boat_name_clean,
            "boat_docked_location": {
                "lat": boat_info.get("lat"),
                "lon": boat_info.get("lon"),
            },
            "boat_location_id": dock_loc_id,
        },
        "problem": {
            "num_nodes": len(np_order) * 2,
            "num_stations": len(np_order),
            "capacity": log["ship_cap"],
        },
        "solution": {
            "legacy-converted": legacy_converted,
            **seg_solutions,
        },
        "mip": {
            "phase": "noport",
            "status": "optimal" if log["noport_status_code"] == 2 else str(log["noport_status_code"]),
            "timeout_seconds": 0.0,
            "global_time_limit_seconds": log["noport_runtime_s"],
            "solve_detail_tuple": ["node_count", "mip_size", "runtime_seconds", "gap_percent"],
            "solves": [[
                log["noport_node_count"],
                [log["mip_cols"], log["mip_rows"]],
                log["noport_grb_runtime_s"],
                round(log["noport_gap_pct"] / 100, 6) if log["noport_gap_pct"] else None,
            ]],
            "best_objective": log["noport_best_obj"],
            "best_bound": log["noport_best_bound"],
            "gap_percent": log["noport_gap_pct"],
            "solution_count": log["noport_solution_count"],
        },
        "summary": {
            "final": "legacy-converted",
            "status": "noport_complete",
            "feasible": False,
            "total_distance_nm": [log["noport_objective"]],
            "final_total_distance_nm": log["noport_objective"],
            "solution_runtime_seconds": [log["noport_grb_runtime_s"]],
            "mip_solves": 1,
            "mip_gap_percent": {"mean": log["noport_gap_pct"], "max": log["noport_gap_pct"]},
            "method": "legacy_noport_mip",
        },
        "solver_stats": {
            "status": "optimal" if log["noport_status_code"] == 2 else str(log["noport_status_code"]),
            "runtime_seconds": log["noport_grb_runtime_s"],
            "mip_gap": round(log["noport_gap_pct"] / 100, 8) if log["noport_gap_pct"] else None,
            "method": "legacy_noport_mip",
        },
    }

    Path(out_path).parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as fh:
        json.dump(out, fh, indent=2, ensure_ascii=False)
    print(f"\nWrote: {out_path}")
    print(f"  no-port distance:  {log['noport_objective']} nm")
    print(f"  stations mapped:   {len(np_station_ids)} / {len(np_order)}")
    print(f"  segments in plan:  {len(log['segments'])}")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description="Convert legacy src.org log to new JSON format")
    ap.add_argument("--log",        required=True,  help="Legacy solver log file")
    ap.add_argument("--legacy-db",  required=True,  help="legacy_distances.db")
    ap.add_argument("--gsp-db",     required=True,  help="gsp.db")
    ap.add_argument("--out",        default="debug/legacy_noport_converted.json",
                    help="Output JSON path")
    args = ap.parse_args()

    convert(args.log, args.legacy_db, args.gsp_db, args.out)


if __name__ == "__main__":
    main()

