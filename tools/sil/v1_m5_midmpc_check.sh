#!/usr/bin/env bash
# V1 — Verify M5 Mid-MPC runs on main path (not geometric fallback).
# Run AFTER Plan A/B/C/D merged to main + docker compose up + scenario activated.
set -eu
RESULT_FILE="${RESULT_FILE:-/tmp/v1_m5_midmpc_result.json}"
DURATION_S=300  # observe 300s of avoidance plans
CONTAINER="${CONTAINER:-mass-l3-tacticallayer-sil-nodes-1}"

echo "[V1] Sampling /l3/m5/avoidance_plan rationale & waypoint count for ${DURATION_S}s..."

declare -i mpc_count=0 fallback_count=0 sample_count=0 max_wp=0
end_t=$(($(date +%s) + DURATION_S))
while [ "$(date +%s)" -lt "$end_t" ]; do
  msg=$(docker exec "$CONTAINER" bash -c \
    "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && timeout 3 ros2 topic echo /l3/m5/avoidance_plan --once" 2>/dev/null || echo "")
  if [ -z "$msg" ]; then sleep 2; continue; fi
  sample_count=$((sample_count + 1))
  rationale=$(echo "$msg" | grep -m1 "rationale:" | head -1 || echo "")
  wp_count=$(echo "$msg" | grep -c "^- schema_version:" || echo 0)
  if [ "$wp_count" -gt "$max_wp" ]; then max_wp=$wp_count; fi
  if echo "$rationale" | grep -qE "Mid.MPC|BC.MPC"; then
    mpc_count=$((mpc_count + 1))
  elif echo "$rationale" | grep -qE "geometric.*fallback|starboard fallback"; then
    fallback_count=$((fallback_count + 1))
  fi
  sleep 5
done

cat > "$RESULT_FILE" <<EOF
{
  "samples": ${sample_count},
  "mpc_count": ${mpc_count},
  "fallback_count": ${fallback_count},
  "max_waypoints": ${max_wp},
  "verdict": "$([ ${mpc_count} -ge $((sample_count * 7 / 10)) ] && echo PASS || echo FAIL)"
}
EOF
cat "$RESULT_FILE"

if [ "${mpc_count}" -ge $((sample_count * 7 / 10)) ] && [ "${max_wp}" -ge 5 ]; then
  echo "[V1] PASS: M5 Mid-MPC/BC-MPC runs >=70% of samples, max waypoints=${max_wp}"
  exit 0
else
  echo "[V1] FAIL: only ${mpc_count}/${sample_count} samples on MPC path, max_wp=${max_wp} (<5)"
  exit 1
fi
