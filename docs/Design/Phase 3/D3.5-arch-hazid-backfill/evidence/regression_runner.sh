#!/usr/bin/env bash
# D3.5 HAZID calibration regression runner.
#
# Usage:
#   ./regression_runner.sh [override_yaml] [scenario_filter] [baseline_csv]
#
# Defaults:
#   override_yaml   = evidence/hazid_calibrated.yaml
#   scenario_filter = all  (runs all 22 Imazu scenarios)
#   baseline_csv    = evidence/traceability_baseline.csv

set -euo pipefail

OVERRIDE_YAML="${1:-docs/Design/Phase\ 3/D3.5-arch-hazid-backfill/evidence/hazid_calibrated.yaml}"
FILTER="${2:-all}"
BASELINE_CSV="${3:-docs/Design/Phase\ 3/D3.5-arch-hazid-backfill/evidence/traceability_baseline.csv}"
OUTPUT_DIR="docs/Design/Phase 3/D3.5-arch-hazid-backfill/evidence/regression_$(date +%Y%m%d_%H%M%S)"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"

echo "=== D3.5 Regression Runner ==="
echo "Override YAML : ${OVERRIDE_YAML}"
echo "Scenario filter: ${FILTER}"
echo "Baseline CSV  : ${BASELINE_CSV}"
echo "Output dir    : ${OUTPUT_DIR}"
echo ""

if [[ ! -f "${OVERRIDE_YAML}" ]]; then
  echo "ERROR: override YAML not found: ${OVERRIDE_YAML}" >&2
  exit 1
fi
if [[ ! -f "${BASELINE_CSV}" ]]; then
  echo "ERROR: baseline CSV not found: ${BASELINE_CSV}" >&2
  echo "       Run D1.3.1/D3.6 CI to generate traceability_baseline.csv first." >&2
  exit 1
fi

mkdir -p "${OUTPUT_DIR}"
CALIBRATED_CSV="${OUTPUT_DIR}/traceability_calibrated.csv"
REPORT_MD="${OUTPUT_DIR}/regression_report.md"

TMP_YAML="/tmp/d3_5_override_$(date +%s).yaml"
cp "${OVERRIDE_YAML}" "${TMP_YAML}"

echo "Starting simulation (may take up to 2h for all 22 Imazu scenarios)..."

ros2 launch fcb_simulator simulator.launch.py \
  "params_file:=${TMP_YAML}" \
  "scenario_filter:=${FILTER}" \
  "output_csv:=${CALIBRATED_CSV}"

EXIT_CODE=$?
rm -f "${TMP_YAML}"

if [[ ${EXIT_CODE} -ne 0 ]]; then
  echo "ERROR: ros2 launch exited with code ${EXIT_CODE}" >&2
  exit ${EXIT_CODE}
fi

if [[ ! -f "${CALIBRATED_CSV}" ]]; then
  echo "ERROR: simulation did not produce output CSV at ${CALIBRATED_CSV}" >&2
  exit 1
fi

echo "Simulation complete. Running diff..."

python3 "docs/Design/Phase 3/D3.5-arch-hazid-backfill/evidence/diff_traceability.py" \
  --baseline "${BASELINE_CSV}" \
  --calibrated "${CALIBRATED_CSV}" \
  --output "${REPORT_MD}"

echo "Report written: ${REPORT_MD}"
