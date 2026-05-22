#!/usr/bin/env bash
set -euo pipefail
ORCHESTRATOR="${SIL_ORCHESTRATOR:-http://localhost:8000}"
TARGET_MINUTES="${TARGET_MINUTES:-220}"
TARGET_SECONDS=$((TARGET_MINUTES * 60))
SCENARIO_DURATION_S=600

start_ts=$(date +%s); iteration=0

IMAZU_IDS=($(curl -s "${ORCHESTRATOR}/api/v1/scenario/list" \
  | python3 -c "import json,sys; data=json.load(sys.stdin); print('\n'.join(s['id'] for s in data if 'imazu' in s['id'].lower()))"))

if [ ${#IMAZU_IDS[@]} -eq 0 ]; then
  echo "[ERR] No Imazu scenarios found at ${ORCHESTRATOR}"
  exit 1
fi

echo "[INFO] Found ${#IMAZU_IDS[@]} Imazu scenarios"

while true; do
  elapsed=$(( $(date +%s) - start_ts ))
  if [ "$elapsed" -ge "$TARGET_SECONDS" ]; then
    echo "[OK] Ran ${iteration} scenarios over $((elapsed/60)) minutes"
    break
  fi
  for scenario_id in "${IMAZU_IDS[@]}"; do
    elapsed=$(( $(date +%s) - start_ts ))
    if [ "$elapsed" -ge "$TARGET_SECONDS" ]; then break 2; fi
    echo "[$(date +%H:%M:%S)] Scenario: ${scenario_id} (iter $((++iteration)), elapsed: $((elapsed/60))m)"
    curl -s -X POST "${ORCHESTRATOR}/api/v1/lifecycle/configure" -H "Content-Type: application/json" -d "{\"scenario_id\":\"${scenario_id}\"}" > /dev/null
    curl -s -X POST "${ORCHESTRATOR}/api/v1/lifecycle/activate" -H "Content-Type: application/json" -d '{}' > /dev/null
    sleep "${SCENARIO_DURATION_S}"
    curl -s -X POST "${ORCHESTRATOR}/api/v1/lifecycle/deactivate" -H "Content-Type: application/json" -d '{}' > /dev/null
    curl -s -X POST "${ORCHESTRATOR}/api/v1/lifecycle/cleanup" -H "Content-Type: application/json" -d '{}' > /dev/null
    sleep 2
  done
done
