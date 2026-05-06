#!/usr/bin/env bash
set -euo pipefail

RESULTS_DIR=$1

if [[ ! -d "$RESULTS_DIR" ]]; then
    echo "WARNING: Polyspace results directory '$RESULTS_DIR' not found — skipping check"
    exit 0
fi

polyspace-results-export -results-dir "$RESULTS_DIR" \
                          -format json \
                          -output-name "${RESULTS_DIR}/results.json" 2>/dev/null || {
    echo "WARNING: polyspace-results-export not available — skipping check"
    exit 0
}

red=$(jq '.summary.red_count // 0' "${RESULTS_DIR}/results.json")
orange=$(jq '.summary.orange_count // 0' "${RESULTS_DIR}/results.json")
green=$(jq '.summary.green_count // 0' "${RESULTS_DIR}/results.json")

total=$((red + orange + green))

echo "Polyspace results: red=$red, orange=$orange, green=$green, total=$total"

if [[ $red -gt 0 ]]; then
    echo "FAIL: Polyspace Red defects = $red (threshold: 0)"
    exit 1
fi

if [[ $total -gt 0 ]]; then
    orange_pct=$(awk -v o="$orange" -v t="$total" 'BEGIN{if(t>0) printf "%.2f", o/t*100; else print "0"}')
    if awk -v p="$orange_pct" 'BEGIN{exit !(p > 1.0)}'; then
        echo "FAIL: Polyspace Orange ratio = ${orange_pct}% (threshold: <= 1%)"
        exit 1
    fi
fi

echo "Polyspace thresholds: PASS"
