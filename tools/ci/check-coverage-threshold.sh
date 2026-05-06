#!/usr/bin/env bash
# Verify per-package coverage threshold (lcov info file → percentage → threshold check)
#
# Wave 0 fixes:
#   F-CRIT-C-004: tightened lcov parsing (anchored regex), explicit empty-string fail,
#                 separated lcov call from awk pipeline so lcov errors don't get masked.
#   F-MIN-001: added bash >= 4.0 check (declare -A requires bash 4.x)
#   F-MIN-004: added jq presence check (not used here but consistent style for other scripts)
set -euo pipefail

if (( BASH_VERSINFO[0] < 4 )); then
    echo "FATAL: requires bash >= 4.0 (got ${BASH_VERSION}). On macOS: 'brew install bash'." >&2
    exit 2
fi

if [[ $# -lt 2 ]]; then
    echo "Usage: $0 <coverage.info> <package_name>" >&2
    exit 2
fi

COVERAGE_FILE=$1
PACKAGE=$2

if [[ ! -f "$COVERAGE_FILE" ]]; then
    echo "FATAL: coverage file '$COVERAGE_FILE' not found" >&2
    exit 1
fi

# Per-package thresholds (per docs/Implementation/00-implementation-master-plan.md §5.2 DoD)
declare -A THRESHOLDS=(
    [m1_odd_envelope_manager]=95   # PATH-S
    [m2_world_model]=90            # PATH-D
    [m3_mission_manager]=90        # PATH-D
    [m4_behavior_arbiter]=90       # PATH-D
    [m5_tactical_planner]=90       # PATH-D
    [m6_colregs_reasoner]=90       # PATH-D
    [m7_safety_supervisor]=95      # PATH-S
    [m8_hmi_transparency_bridge]=80  # PATH-H
    [l3_msgs]=70                   # IDL-only package
    [l3_external_msgs]=70          # IDL-only package
    [l3_external_mock_publisher]=80   # Python ament package
)

threshold=${THRESHOLDS[$PACKAGE]:-90}

# F-CRIT-C-004: separate lcov from awk; capture status; fail loud on lcov errors
summary_output=""
if ! summary_output=$(lcov --summary "$COVERAGE_FILE" 2>&1); then
    echo "FATAL: lcov --summary failed:" >&2
    echo "$summary_output" >&2
    exit 1
fi

# Anchored regex (NOT just /lines/) — only match the actual summary line:
#   "  lines......: 92.5% (185 of 200 lines)"
actual=$(echo "$summary_output" | awk '/^[[:space:]]*lines\.+:/ {gsub("%","",$2); print $2; exit}')

if [[ -z "$actual" ]]; then
    echo "FATAL: cannot parse coverage percentage from lcov output:" >&2
    echo "$summary_output" >&2
    exit 1
fi

# Sanity check: must be numeric
if ! [[ "$actual" =~ ^[0-9]+(\.[0-9]+)?$ ]]; then
    echo "FATAL: parsed coverage value '$actual' is not numeric" >&2
    exit 1
fi

echo "[${PACKAGE}] Coverage: ${actual}% (threshold: ${threshold}%)"

# Compare with awk (handles floats); explicit positive failure check
if awk -v a="$actual" -v t="$threshold" 'BEGIN{exit !(a < t)}'; then
    echo "FAIL: [${PACKAGE}] coverage ${actual}% < threshold ${threshold}%"
    exit 1
fi

echo "PASS: [${PACKAGE}] coverage threshold met"
