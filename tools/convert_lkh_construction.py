#!/usr/bin/env python3
"""Convert the Auckland/LKH external solution into this repo's native schema.

Reads  : sol/lkh/construction_raw.json   (Adams & Walker LKH solution, verbatim)
         dat/gsp.db                       (stations / locations / ports / boats)
Writes : sol/lkh/construction.json        (native "construction" schema)

segment.json is intentionally NOT written here: it is produced by the pipeline
(`make -C src segment METHOD=lkh`), which re-segments the station order into
capacity-feasible trips like any other method.

The raw file is node-compressed: one location node per station (the endpoint
their tour used). This repo lists BOTH station endpoints in travel order, so
each compressed node is expanded to [used_endpoint, other_endpoint] and the
station is signed +id when the used endpoint is the station's start_location_id
(traversed start->end) or -id otherwise (end->start). Ports at segment
boundaries are kept as-is; no coastline waypoints are inserted.

Run from the repo root (PowerShell 7):
    python tools/convert_lkh_construction.py
"""
import json
import math
import os
import sqlite3

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RAW = os.path.join(REPO, "sol", "lkh", "construction_raw.json")
GSP_DB = os.path.join(REPO, "dat", "gsp.db")
OUT_CONSTRUCTION = os.path.join(REPO, "sol", "lkh", "construction.json")

EARTH_NM = 3440.065  # nautical miles


def haversine_nm(lat1, lon1, lat2, lon2):
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dphi = math.radians(lat2 - lat1)
    dlmb = math.radians(lon2 - lon1)
    a = math.sin(dphi / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dlmb / 2) ** 2
    return 2 * EARTH_NM * math.asin(math.sqrt(a))


def main():
    raw = json.load(open(RAW, encoding="utf-8"))
    segs = raw["tour_segment_location_ids"]
    seg_catch = raw["segment_catch_amount"]
    seg_total_nm = raw["segment_distance_nm"]

    con = sqlite3.connect(GSP_DB)
    cur = con.cursor()
    loc = {r[0]: (r[1], r[2]) for r in cur.execute("SELECT id,lat,lon FROM locations")}
    port_locids = {r[0] for r in cur.execute("SELECT location_id FROM ports")}
    # station lookups keyed by endpoint location id
    st_start = {}  # start_location_id -> (station_id, start_loc, end_loc)
    st_end = {}    # end_location_id   -> (station_id, start_loc, end_loc)
    trawl_of_station = {}
    for sid, sl, el in cur.execute(
        "SELECT id,start_location_id,end_location_id FROM stations"
    ):
        st_start[sl] = (sid, sl, el)
        st_end[el] = (sid, sl, el)
        (la1, lo1), (la2, lo2) = loc[sl], loc[el]
        trawl_of_station[sid] = haversine_nm(la1, lo1, la2, lo2)

    # boat metadata: single vessel docked at the home port. Pick the smallest-
    # capacity boat at that port that still covers the largest trip catch, so the
    # 45 t vessel (not the 14 t one, also docked at port 5) is chosen.
    home_port_locid = segs[0][0]
    max_catch = max(seg_catch)
    brow = cur.execute(
        "SELECT id,name,capacity,location_id FROM boats "
        "WHERE location_id=? AND capacity>=? ORDER BY capacity ASC LIMIT 1",
        (home_port_locid, max_catch),
    ).fetchone()
    if not brow:  # fall back to the 45 t vessel anywhere
        brow = cur.execute(
            "SELECT id,name,capacity,location_id FROM boats ORDER BY abs(capacity-45000) LIMIT 1"
        ).fetchone()
    boat_id, boat_name, capacity, boat_locid = brow
    home_lat, home_lon = loc[boat_locid]

    tour_loc, tour_st = [], []
    seg_transit, seg_total = [], []
    dock_ids = set()
    for si, seg in enumerate(segs):
        p_start, p_end = seg[0], seg[-1]
        dock_ids.add(p_start)
        dock_ids.add(p_end)
        interior = seg[1:-1]
        loc_row = [p_start]
        st_row = []
        trawl_sum = 0.0
        for node in interior:
            if node in st_start:
                sid, sl, el = st_start[node]
                loc_row += [sl, el]      # start->end
                st_row.append(sid)       # +id
            elif node in st_end:
                sid, sl, el = st_end[node]
                loc_row += [el, sl]      # end->start
                st_row.append(-sid)      # -id
            else:
                raise SystemExit(f"seg {si}: node {node} is neither a station endpoint nor a port")
            trawl_sum += trawl_of_station[sid]
        loc_row.append(p_end)
        tour_loc.append(loc_row)
        tour_st.append(st_row)
        total = seg_total_nm[si]
        seg_total.append(round(total, 6))
        seg_transit.append(round(total - trawl_sum, 6))

    n_stations = sum(len(s) for s in tour_st)
    distance_nm = {
        "grand_total": {
            "transit": round(sum(seg_transit), 6),
            "total": round(sum(seg_total), 6),
        },
        "segment": {"transit": seg_transit, "total": seg_total},
    }

    def solution_variant(variant):
        return {
            "variant": variant,
            "tour_segments_location_ids": tour_loc,
            "dock_location_ids": sorted(dock_ids),
            "unique_waypoint_location_ids": [],
            "tour_segments_station_ids": tour_st,
            "station_count": n_stations,
            "segment_count": len(tour_loc),
            "segment_catch_amount": seg_catch,
            "distance_nm": distance_nm,
            "feasible": True,
        }

    metadata = {
        "solver_version": "external_lkh_import_1.0",
        "source": "Adams & Walker (Univ. of Auckland) LKH-3 heuristic",
        "mode": "construction",
        "strategy": "lkh",
        "boat_id": boat_id,
        "boat_name": boat_name,
        "boat_docked_location": {"lat": home_lat, "lon": home_lon},
        "boat_location_id": boat_locid,
    }
    problem = {"num_nodes": 2 * n_stations, "num_stations": n_stations, "capacity": capacity}
    summary = {"status": {"final": None}, "runtime_seconds": {"grandtotal": 180}}

    # construction.json
    md_c = dict(metadata, mode="construction")
    summary_c = {"status": {"final": "construction"}, "runtime_seconds": {"grandtotal": 180}}
    doc_c = {
        "metadata": md_c,
        "problem": problem,
        "solution": {"construction": solution_variant("construction")},
        "summary": summary_c,
    }
    json.dump(doc_c, open(OUT_CONSTRUCTION, "w", encoding="utf-8"), indent=2, ensure_ascii=False)

    print(f"boat: id={boat_id} name={boat_name!r} capacity={capacity} home_locid={boat_locid}")
    print(f"segments={len(tour_loc)} stations={n_stations} ports={sorted(dock_ids)}")
    print(f"grand transit={distance_nm['grand_total']['transit']:.1f} nm  "
          f"total={distance_nm['grand_total']['total']:.1f} nm  "
          f"(trawl={sum(seg_total)-sum(seg_transit):.1f} nm)")
    print(f"wrote {OUT_CONSTRUCTION}")


if __name__ == "__main__":
    main()
