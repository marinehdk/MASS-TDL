#!/usr/bin/env bash
# V2 — M7 Safety Supervisor must NOT import M4/M5 internal algorithm files.
# Topic subscriptions (.msg) are OK; including .hpp from m4/m5/src is FAIL.
set -eu

M7_DIR="src/l3_tdl_kernel/m7_safety_supervisor"
RESULT_FILE="${RESULT_FILE:-/tmp/v2_m7_independence_result.json}"

echo "[V2] Auditing ${M7_DIR} for M4/M5 algorithm coupling..."

# Forbidden: any include of m4_behavior_arbiter or m5_tactical_planner internal headers
violations=$(grep -rnE '#include.*"m4_behavior_arbiter|"m5_tactical_planner|<m4_|<m5_' \
  "$M7_DIR/src" "$M7_DIR/include" 2>/dev/null \
  | grep -v "_msgs\|_pb\|msg/\|/msg" \
  || true)

# Count violations
if [ -z "$violations" ]; then
  violation_count=0
else
  violation_count=$(echo "$violations" | wc -l | tr -d ' ')
fi

# Count allowed includes
allowed_includes=$(grep -rE '#include' "$M7_DIR/src" "$M7_DIR/include" 2>/dev/null \
  | grep -cE 'rclcpp|<std|<chrono|<memory|_msgs|_pb|sil_proto' || echo 0)

cat > "$RESULT_FILE" <<EOF
{
  "violations": "${violations}",
  "violation_count": ${violation_count},
  "allowed_include_count": ${allowed_includes},
  "verdict": "$([ "$violation_count" -eq 0 ] && echo PASS || echo FAIL)"
}
EOF
cat "$RESULT_FILE"

if [ "$violation_count" -eq 0 ]; then
  echo "[V2] PASS: M7 has no M4/M5 algorithm coupling (${allowed_includes} allowed includes)"
  exit 0
else
  echo "[V2] FAIL: ${violation_count} violations:"
  echo "$violations"
  exit 1
fi
