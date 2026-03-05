#!/usr/bin/env bash
# Phase 1: Matheuristic sweep improvement
# Run using cached init solutions (from Phase 0)
# Usage: bash scripts/run_phase1_sweep.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"

SOLVER="${ROOT_DIR}/build/gsp_init.exe"
DB="${ROOT_DIR}/dat/gsp_data.db"
BOAT_ID=2
INIT_STRATEGY="opt"  # Use OPT initialization
CONFIG="${ROOT_DIR}/config/gsp_solver.yaml"

# L2SEG values to test (from config/gsp_solver.yaml)
L2SEG_VALUES=(60 120 180 240 300)

# Verify build exists
if [ ! -f "$SOLVER" ]; then
    echo "❌ ERROR: Solver not found at $SOLVER"
    echo "   Run: mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j4"
    exit 1
fi

# Get init_run_id from database
INIT_RUN_ID=$(sqlite3 "$DB" "SELECT id FROM init_runs WHERE boat_id=$BOAT_ID AND strategy='$INIT_STRATEGY' LIMIT 1" 2>/dev/null || echo "")

if [ -z "$INIT_RUN_ID" ]; then
    echo "❌ ERROR: No $INIT_STRATEGY initialization found for boat_id=$BOAT_ID"
    echo "   Run Phase 0 first: bash scripts/run_phase0_init.sh"
    exit 1
fi

echo "============================================================"
echo "Phase 1: Matheuristic Sweep Improvement"
echo "============================================================"
echo ""
echo "Using Init: $INIT_STRATEGY (init_run_id=$INIT_RUN_ID)"
echo "Database: $DB"
echo "L2SEG Values: ${L2SEG_VALUES[@]}"
echo ""

# Get initial solution distance
INIT_DISTANCE=$(sqlite3 "$DB" "SELECT total_distance FROM init_runs WHERE id=$INIT_RUN_ID LIMIT 1")
echo "Initial Distance: $INIT_DISTANCE nm (from $INIT_STRATEGY)"
echo ""

start_time=$(date +%s)
total_improvements=0

for L2SEG in "${L2SEG_VALUES[@]}"; do
    STRIDE=$((L2SEG / 2))  # 50% overlap

    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "Sweep: L2SEG=$L2SEG STRIDE=$STRIDE"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "[$(date '+%H:%M:%S')] Starting MH sweep with L2SEG=$L2SEG..."

    "$SOLVER" --mode sweep \
        --init-run-id "$INIT_RUN_ID" \
        --boat-id $BOAT_ID \
        --l2seg "$L2SEG" \
        --stride "$STRIDE" \
        --mip-time-limit 120 \
        --max-iterations 100 \
        --config "$CONFIG"

    if [ $? -eq 0 ]; then
        # Get result
        MH_RUN_ID=$(sqlite3 "$DB" "SELECT id FROM mh_runs WHERE init_run_id=$INIT_RUN_ID AND l2seg=$L2SEG LIMIT 1")
        FINAL_DISTANCE=$(sqlite3 "$DB" "SELECT final_distance FROM mh_runs WHERE id=$MH_RUN_ID LIMIT 1")
        IMPROVEMENT=$(echo "$INIT_DISTANCE - $FINAL_DISTANCE" | bc)
        PCT=$(echo "scale=2; 100 * $IMPROVEMENT / $INIT_DISTANCE" | bc)

        echo "✓ L2SEG=$L2SEG completed"
        echo "  Final Distance: $FINAL_DISTANCE nm"
        echo "  Improvement: $IMPROVEMENT nm ($PCT%)"
        total_improvements=$(echo "$total_improvements + $IMPROVEMENT" | bc)
    else
        echo "✗ ERROR: L2SEG=$L2SEG failed"
        exit 1
    fi
    echo ""
done

end_time=$(date +%s)
elapsed=$((end_time - start_time))

echo "============================================================"
echo "Phase 1 Complete!"
echo "============================================================"
echo "Total Time: $((elapsed / 60)) minutes $((elapsed % 60)) seconds"
echo "Total Improvement: $(echo "scale=2; $total_improvements" | bc) nm"
echo ""
echo "Results Summary (all L2SEG values):"
sqlite3 "$DB" \
    "SELECT
        mh.l2seg,
        mh.stride,
        ROUND(mh.final_distance, 2) as best_distance_nm,
        mh.final_num_segments,
        mh.iterations_completed,
        ROUND(100.0*($INIT_DISTANCE - mh.final_distance)/$INIT_DISTANCE, 2) as improvement_pct,
        ROUND(mh.total_runtime_seconds/60.0, 1) as runtime_min
    FROM mh_runs mh
    WHERE mh.init_run_id=$INIT_RUN_ID
    ORDER BY mh.l2seg;"

echo ""
echo "To analyze convergence for specific L2SEG:"
echo "  sqlite3 $DB \"SELECT iteration, total_distance, best_distance FROM mh_iterations WHERE mh_run_id=N ORDER BY iteration;\""

