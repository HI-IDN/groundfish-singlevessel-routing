#!/usr/bin/env bash
# Phase 0: Generate all 4 initialization strategies
# Run once and cache results in database
# Usage: bash scripts/run_phase0_init.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"

SOLVER="${ROOT_DIR}/build/gsp_init.exe"
DB="${ROOT_DIR}/dat/gsp_data.db"
CONFIG="${ROOT_DIR}/config/gsp_solver.yaml"

# Verify build exists
if [ ! -f "$SOLVER" ]; then
    echo "❌ ERROR: Solver not found at $SOLVER"
    echo "   Run: mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j4"
    exit 1
fi

# Verify database exists
if [ ! -f "$DB" ]; then
    echo "❌ ERROR: Database not found at $DB"
    echo "   Run preprocessing first to create database"
    exit 1
fi

# Verify config exists
if [ ! -f "$CONFIG" ]; then
    echo "❌ ERROR: Configuration not found at $CONFIG"
    exit 1
fi

echo "============================================================"
echo "Phase 0: Initialization (OPT, NN, GE, CI)"
echo "============================================================"
echo ""
echo "Database: $DB"
echo "Config: $CONFIG"
echo ""

start_time=$(date +%s)

# Run 4 initialization strategies
for STRATEGY in opt nn ge ci; do
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "Running: $STRATEGY"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

    case "$STRATEGY" in
        opt)
            # OPT: Expensive, needs time limit
            echo "[$(date '+%H:%M:%S')] Starting OPT initialization (expensive, ~7-8 minutes)..."
            "$SOLVER" --mode init --strategy opt \
                --time-limit 600 \
                --config "$CONFIG"
            ;;
        nn)
            # NN: Fast heuristic
            echo "[$(date '+%H:%M:%S')] Starting NN initialization (fast)..."
            "$SOLVER" --mode init --strategy nn \
                --config "$CONFIG"
            ;;
        ge)
            # GE: Fast construction
            echo "[$(date '+%H:%M:%S')] Starting GE initialization (fast)..."
            "$SOLVER" --mode init --strategy ge \
                --config "$CONFIG"
            ;;
        ci)
            # CI: Moderate heuristic
            echo "[$(date '+%H:%M:%S')] Starting CI initialization (moderate)..."
            "$SOLVER" --mode init --strategy ci \
                --config "$CONFIG"
            ;;
    esac

    if [ $? -eq 0 ]; then
        echo "✓ $STRATEGY completed successfully"
    else
        echo "✗ ERROR: $STRATEGY failed"
        exit 1
    fi
    echo ""
done

end_time=$(date +%s)
elapsed=$((end_time - start_time))

echo "============================================================"
echo "Phase 0 Complete!"
echo "============================================================"
echo "Total Time: $((elapsed / 60)) minutes $((elapsed % 60)) seconds"
echo ""
echo "Results Summary:"
sqlite3 "$DB" \
    "SELECT
        strategy,
        ROUND(total_distance, 2) as distance_nm,
        num_stations,
        num_segments,
        ROUND(runtime_seconds, 1) as runtime_sec
    FROM init_runs
    ORDER BY total_distance ASC;"

echo ""
echo "Next step: Run Phase 1 matheuristic sweep"
echo "  bash scripts/run_phase1_sweep.sh"

