#!/usr/bin/env bash
# V3 — M2 World Model must publish /l3/m2/threat_state with TS1 entry during imazu-01-ho.
set -eu

CONTAINER="${CONTAINER:-mass-l3-sil-sil-nodes-1}"
RESULT_FILE="${RESULT_FILE:-/tmp/v3_m2_threats_result.json}"
TIMEOUT=10

echo "[V3] Checking /l3/m2/threat_state publication & content..."

# Topic existence
topic_exists=$(docker exec "$CONTAINER" bash -c \
  "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && ros2 topic list" 2>/dev/null \
  | grep -c "^/l3/m2/threat_state$" || echo 0)

# Sample one message
msg=$(docker exec "$CONTAINER" bash -c \
  "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && timeout ${TIMEOUT} ros2 topic echo /l3/m2/threat_state --once" 2>/dev/null || echo "")

has_threats=0
has_cpa=0
has_cpa_status=0

if [ -n "$msg" ]; then
  echo "$msg" | grep -q "mmsi:\|target_id:\|threat_id:" && has_threats=1 || true
  echo "$msg" | grep -q "cpa\|min_cpa" && has_cpa=1 || true
  echo "$msg" | grep -q "cpa_status\|status:" && has_cpa_status=1 || true
fi

# Rate check (5s window)
rate_output=$(docker exec "$CONTAINER" bash -c \
  "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && timeout 7 ros2 topic hz /l3/m2/threat_state" 2>&1 | grep -oE "average rate: [0-9.]+" | head -1 || echo "average rate: 0.0")
rate=$(echo "$rate_output" | grep -oE "[0-9.]+" || echo "0")

cat > "$RESULT_FILE" <<EOF
{
  "topic_exists": ${topic_exists},
  "has_threats": ${has_threats},
  "has_cpa": ${has_cpa},
  "has_cpa_status": ${has_cpa_status},
  "rate_hz": ${rate},
  "verdict": "$([ "$topic_exists" -gt 0 ] && [ "$has_threats" -gt 0 ] && [ "$has_cpa" -gt 0 ] && echo PASS || echo FAIL)"
}
EOF
cat "$RESULT_FILE"

if [ "$topic_exists" -gt 0 ] && [ "$has_threats" -gt 0 ] && [ "$has_cpa" -gt 0 ]; then
  echo "[V3] PASS: M2 publishes threats with CPA at ${rate} Hz"
  exit 0
else
  echo "[V3] FAIL: topic_exists=${topic_exists} has_threats=${has_threats} has_cpa=${has_cpa}"
  exit 1
fi
