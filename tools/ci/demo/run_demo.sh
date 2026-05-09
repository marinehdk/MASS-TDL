#!/usr/bin/env bash
# tools/ci/demo/run_demo.sh — DEMO-1 6/15 RL-isolation lint live demonstration.
# Per spec §v3.1.10 Demo Charter (~5 min slot).
set -uo pipefail

DEMO_SRC="tools/ci/demo/violation_fixture.cpp"
DEMO_DST="src/sim_workbench/fcb_simulator/src/_demo_violation.cpp"

cleanup() { rm -f "$DEMO_DST"; }
trap cleanup EXIT

echo "=== STEP 1: Tree topology ==="
tree src/ -L 2 || ls -R src/ | head -20

echo ""
echo "=== STEP 2: Interface visibility ==="
grep -n "export_fmu_interface" \
     src/sim_workbench/ship_sim_interfaces/include/ship_sim_interfaces/ship_motion_simulator.hpp

echo ""
echo "=== STEP 3: Inject violation fixture ==="
cp "$DEMO_SRC" "$DEMO_DST"
echo "Copied $DEMO_SRC -> $DEMO_DST"

echo ""
echo "=== STEP 4: lint runs, expects exit 1 ==="
set +e
bash tools/ci/check-rl-isolation.sh
rc=$?
set -e
echo "lint exit=$rc (expected 1)"

echo ""
echo "=== STEP 5: cleanup, lint rerun, expects exit 0 ==="
rm -f "$DEMO_DST"
set +e
bash tools/ci/check-rl-isolation.sh
rc=$?
set -e
echo "lint exit=$rc (expected 0)"
