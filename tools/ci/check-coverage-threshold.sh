#!/usr/bin/env bash
set -euo pipefail

COVERAGE_FILE=$1
PACKAGE=$2

# Coverage thresholds per package
declare -A THRESHOLDS=(
    [m1_odd_envelope_manager]=90
    [m2_world_model]=90
    [m3_mission_manager]=90
    [m4_behavior_arbiter]=90
    [m5_tactical_planner]=90
    [m6_colregs_reasoner]=90
    [m7_safety_supervisor]=95
    [m8_hmi_transparency_bridge]=80
    [l3_msgs]=70
    [l3_external_msgs]=70
)

threshold=${THRESHOLDS[$PACKAGE]:-90}
actual=$(lcov --summary "$COVERAGE_FILE" 2>&1 | awk '/lines/{print $2}' | tr -d '%' || echo "0")

echo "Coverage: ${actual}% (threshold: ${threshold}%)"

if awk -v a="$actual" -v t="$threshold" 'BEGIN{exit !(a < t)}'; then
    echo "FAIL: Coverage ${actual}% < threshold ${threshold}%"
    exit 1
fi

echo "Coverage threshold: PASS"
