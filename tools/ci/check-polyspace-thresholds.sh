#!/usr/bin/env bash
# Polyspace results threshold gate (PATH-S 路径强制门禁)
#   - Red defects: 0 (hard threshold; CCS入级证据强制)
#   - Orange ratio: ≤ 1% (per docs/Implementation/static-analysis-policy.md §5.3)
#
# Wave 0 fixes:
#   F-IMP-C-003: removed silent-PASS on missing tool — PATH-S 路径不可豁免
#                 (results-dir missing 或 polyspace-results-export 缺失 → exit 1)
#   F-MIN-001/004: bash >= 4.0 + jq presence check
set -euo pipefail

if (( BASH_VERSINFO[0] < 4 )); then
    echo "FATAL: requires bash >= 4.0 (got ${BASH_VERSION})." >&2
    exit 2
fi

if ! command -v jq >/dev/null 2>&1; then
    echo "FATAL: jq not installed (required for Polyspace JSON parsing)" >&2
    exit 2
fi

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <polyspace_results_dir>" >&2
    exit 2
fi

RESULTS_DIR=$1

if [[ ! -d "$RESULTS_DIR" ]]; then
    echo "FATAL: Polyspace results directory '$RESULTS_DIR' not found" >&2
    echo "       PATH-S routes (M1/M7) require Polyspace results — cannot skip" >&2
    echo "       Ensure stage-3-polyspace-path-s job ran successfully" >&2
    exit 1
fi

# F-IMP-C-003: polyspace-results-export missing is FATAL (was silent PASS)
if ! command -v polyspace-results-export >/dev/null 2>&1; then
    echo "FATAL: polyspace-results-export not in PATH" >&2
    echo "       PATH-S routes require Polyspace CLI — fix Dockerfile.ci (F-CRIT-A-009)" >&2
    exit 1
fi

if ! polyspace-results-export -results-dir "$RESULTS_DIR" \
                              -format json \
                              -output-name "${RESULTS_DIR}/results.json" 2>/dev/null; then
    echo "FATAL: polyspace-results-export failed for $RESULTS_DIR" >&2
    exit 1
fi

if [[ ! -f "${RESULTS_DIR}/results.json" ]]; then
    echo "FATAL: results.json not produced by polyspace-results-export" >&2
    exit 1
fi

red=$(jq '.summary.red_count // 0' "${RESULTS_DIR}/results.json")
orange=$(jq '.summary.orange_count // 0' "${RESULTS_DIR}/results.json")
green=$(jq '.summary.green_count // 0' "${RESULTS_DIR}/results.json")

total=$((red + orange + green))

echo "Polyspace results: red=$red, orange=$orange, green=$green, total=$total"

# Red threshold: 0 (no exceptions, CCS audit gate)
if [[ $red -gt 0 ]]; then
    echo "FAIL: Polyspace Red defects = $red (threshold: 0)"
    exit 1
fi

# Orange ratio threshold: ≤ 1%
if [[ $total -gt 0 ]]; then
    orange_pct=$(awk -v o="$orange" -v t="$total" 'BEGIN{if(t>0) printf "%.2f", o/t*100; else print "0"}')
    if awk -v p="$orange_pct" 'BEGIN{exit !(p > 1.0)}'; then
        echo "FAIL: Polyspace Orange ratio = ${orange_pct}% (threshold: <= 1%)"
        exit 1
    fi
    echo "Orange ratio: ${orange_pct}% (within threshold)"
fi

echo "Polyspace thresholds: PASS"
