#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN_DIR="${ROOT_DIR}/bin"
SOL_DIR="${ROOT_DIR}/sol"
DAT_FILE="${ROOT_DIR}/dat/singleboatdata2023spring.dat"

CAP_TIMELIMITS=(60 120 180 240 300 360 420 480)

mkdir -p "${SOL_DIR}"

for cap_t in "${CAP_TIMELIMITS[@]}"; do
  out_dat="${SOL_DIR}/capmut_v3_${cap_t}.dat"
  out_log="${ROOT_DIR}/capmut_v3_${cap_t}.txt"
  echo "Running cap-time-limit=${cap_t} -> ${out_dat}"
  "${BIN_DIR}/matcapmutheur_v3" "${DAT_FILE}" 1 \
    --cap-time-limit "${cap_t}" \
    --write-dat "${out_dat}" \
    --init-capacity 42000 \
    --verbose-init \
    > "${out_log}"
done
