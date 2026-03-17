#!/usr/bin/env bash
# Complete pipeline: Phase 0 (INIT) + Phase 1 (MH Sweep)
# Usage: bash scripts/batch_all.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "╔════════════════════════════════════════════════════════════╗"
echo "║  GSP Solver: Complete Pipeline (Phase 0 + Phase 1)         ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""
echo "This will run the complete optimization pipeline:"
echo "  1. Phase 0: Generate 4 initialization strategies (~10 min)"
echo "  2. Phase 1: Run MH sweep on 5 L2SEG values (~10-12 hours)"
echo ""
echo "⏱️  Total estimated time: 10-13 hours"
echo ""
read -p "Continue? (y/n) " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "Cancelled."
    exit 1
fi

# Phase 0: Initialization
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "PHASE 0: INITIALIZATION (4 strategies)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

bash "$SCRIPT_DIR/run_phase0_init.sh"
PHASE0_STATUS=$?

if [ $PHASE0_STATUS -ne 0 ]; then
    echo ""
    echo "❌ Phase 0 failed!"
    exit 1
fi

echo ""
echo "✓ Phase 0 completed successfully"
echo ""

# Phase 1: Matheuristic Sweep
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "PHASE 1: MATHEURISTIC SWEEP (5 L2SEG values)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

bash "$SCRIPT_DIR/run_phase1_sweep.sh"
PHASE1_STATUS=$?

if [ $PHASE1_STATUS -ne 0 ]; then
    echo ""
    echo "❌ Phase 1 failed!"
    exit 1
fi

echo ""
echo "✓ Phase 1 completed successfully"
echo ""

echo "╔════════════════════════════════════════════════════════════╗"
echo "║  COMPLETE PIPELINE SUCCESSFUL!                             ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""
echo "Results are stored in database: dat/gsp_data.db"
echo ""
echo "Query results:"
echo "  Init solutions:  SELECT * FROM init_runs WHERE boat_id=2;"
echo "  MH solutions:    SELECT * FROM mh_runs;"
echo "  MH iterations:   SELECT * FROM mh_iterations WHERE mh_run_id=N;"

