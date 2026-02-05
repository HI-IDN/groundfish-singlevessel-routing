This README documents how the solution files in this folder were produced and which source code / scripts generated them.

Overview
--------
This `sol/` folder contains solution outputs and logs produced by running the capacity matheuristic variants. The main variants present here are:

- `capmut_*.txt`, `capmut_*.dat`, `capmut_*.csv` — produced by the original matheuristic binary `matcapmutheur` (C source: `src/matcapmutheur.c`).
- `capmut_v3_*.txt`, `capmut_v3_*.dat`, `capmut_v3_*.csv` — produced by the V3 variant `matcapmutheur_v3` (C source: `src/matcapmutheur_v3.c`).
- `capmut_v3_cheapest_*.txt`, `capmut_v3_cheapest_*.dat`, `capmut_v3_cheapest_*.csv` — produced by running the V3 binary with the initialization strategy `--init-strategy cheapest` (implemented in `src/matcapmutheur_v3.c`).

Evidence & reproduction
-----------------------
1) Original capmut runs (non‑V3)
- Script used: `script_capmut.sh` (top‑level `Code/` folder).
  - Key lines:
    - out_dat = `${SOL_DIR}/capmut_${cap_t}.dat`
    - executes `${BIN_DIR}/matcapmutheur` with arguments `DAT_FILE 1 --time-limit 300 --cap-time-limit <cap> --write-dat <out_dat> --verbose-init > <out_log>`
  - See: `script_capmut.sh` (lines ~10–23)
- Source/binary: `src/matcapmutheur.c` compiled into `bin/matcapmutheur` by the `src/Makefile` (target `matcapmutheur`).
- Reproduce (from repo root):
```bash
# build the binary
make -C src matcapmutheur
# run the original script (this will write into sol/ and create logs in repo root)
bash script_capmut.sh
```

2) V3 runs
- Script used: `script_capmut_v3.sh` (top‑level `Code/` folder).
  - Key lines:
    - out_dat = `${SOL_DIR}/capmut_v3_${cap_t}.dat`
    - executes `${BIN_DIR}/matcapmutheur_v3` with arguments `DAT_FILE 1 --cap-time-limit <cap> --write-dat <out_dat> --init-capacity 42000 --verbose-init > <out_log>`
  - See: `script_capmut_v3.sh` (lines ~10–22)
- Source/binary: `src/matcapmutheur_v3.c` compiled into `bin/matcapmutheur_v3` (Makefile target `matcapmutheur_v3`).
- Reproduce (from repo root):
```bash
make -C src matcapmutheur_v3
bash script_capmut_v3.sh
```

3) V3 with "cheapest" init strategy
- Files: `capmut_v3_cheapest_*.txt` / `.dat` / `.csv` in `sol/`.
- How it was produced: these are variants of the V3 binary run with the initialization strategy `--init-strategy cheapest` (the V3 source contains an `--init-strategy` flag and an implementation of the `cheapest` insertion: see `src/matcapmutheur_v3.c`).
  - Source evidence: `src/matcapmutheur_v3.c` contains code paths that print `Init strategy: cheapest insertion` when `--init-strategy cheapest` is used and when `--verbose-init` is enabled.
- Reproduce example (from repo root):
```bash
make -C src matcapmutheur_v3
# run only cap 60 as example (writes sol/capmut_v3_cheapest_60.dat and capmut_v3_cheapest_60.txt)
./bin/matcapmutheur_v3 dat/singleboatdata2023spring.dat 1 --cap-time-limit 60 --write-dat sol/capmut_v3_cheapest_60.dat --init-strategy cheapest --verbose-init > capmut_v3_cheapest_60.txt
```