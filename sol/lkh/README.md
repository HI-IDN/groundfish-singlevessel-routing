# LKH single-vessel baseline (Adams & Walker, Auckland)

`construction_raw.json` is an externally produced single-vessel solution to the
full 580-station Groundfish Survey Problem, provided by **Thomas Adams**
(thomas.adams@auckland.ac.nz) and **Cameron Walker** (cameron.walker@auckland.ac.nz)
of the University of Auckland. They were given this repository and its 2023 routing
data (`dat/gsp.db`) and produced the solution with their own Lin–Kernighan (LKH-3)
heuristic in a single 3-minute run. The file is kept here verbatim (a byte-for-byte
copy of their `single_vessel.json`, only renamed).

Their reported result is **4166 nm** of sailing for one vessel out of Hafnarfjörður
at 45 t capacity — against 4391 nm for Ingimundardóttir et al. (LION 2026).
Cross-checked against `dat/gsp.db`, the solution is consistent: 13 trips across
7 ports, all 580 stations trawled exactly once, every trip catch ≤ 44,862 kg
(within the 45 t cap), and per-trip distances summing to 6409 nm = **4165 nm
sailing + 2244 nm fixed trawling**, reproducing their figure.

## Method (TL;DR)

A Lin–Kernighan (LKH-3) local-search heuristic [10], adapted to the GSP:

- casts the multi-trip capacitated problem as a TSP — doubling vertices to make
  asymmetric costs symmetric, and copying the home-port vertex to allow multiple
  trips — then applies Lin–Kernighan k-opt moves;
- scores every candidate by a *(penalty, cost)* pair and optimises
  lexicographically: drive infeasibility to zero first, then minimise distance;
- uses a custom penalty adding the amount by which each trip exceeds the vessel's
  capacity, the maximum distance between port calls, and the maximum total voyage
  distance.

### References

- [10] K. Helsgaun. "An effective implementation of the Lin–Kernighan traveling
  salesman heuristic." *European Journal of Operational Research* (2000).
- [1] D. L. Applegate, ed. *The Traveling Salesman Problem: A Computational Study.*
  Princeton University Press, 2006.

## How it runs

`tools/convert_lkh_construction.py` reads `construction_raw.json` + `dat/gsp.db` and
writes this repo's native `construction.json`: it renames the segments key, expands
each node-compressed station into its two `start`/`end` endpoints (entering at the
endpoint their tour used, leaving by the other — signed `+id` for start→end, `−id`
for end→start), and adds the `metadata` / `problem` blocks. Their location IDs are
already this repo's, so no remapping is needed.

From there it is an ordinary method in the pipeline, run as `METHOD=lkh`:

    python tools\convert_lkh_construction.py      # -> construction.json
    make -C src segment          METHOD=lkh       # -> segment.json
    make -C src refinement_l2seg METHOD=lkh L2SEG=180
    make -C src plot-construction-segment         # -> mh_phase0.png

`L2SEG=180` is the two-segment C-MIP time limit recommended in the LION 2026 paper
(short, repeated local solves; no gain beyond ~3 minutes per boundary).
