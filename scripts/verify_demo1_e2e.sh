#!/bin/bash
set -euo pipefail

TIMESTAMP=$(date -u +'%Y%m%dT%H%M%SZ')
TIMEOUT=120
CONTAINER="sil-nodes"
SCENARIO_YAML="scenarios/IMAZU标准测试/imazu-08-ms.yaml"
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
PASS=0; FAIL=0

while [[ $# -gt 0 ]]; do
    case $1 in
        --timeout)      TIMEOUT="$2";       shift 2 ;;
        --container)    CONTAINER="$2";     shift 2 ;;
        --scenario-yaml) SCENARIO_YAML="$2"; shift 2 ;;
        *) echo "Unknown arg: $1"; exit 2 ;;
    esac
done

# Extract expected initial_lat from scenario YAML
EXPECTED_LAT=$(python3 -c "
import yaml, sys
try:
    with open('$SCENARIO_YAML') as f:
        d = yaml.safe_load(f)
    lat = d.get('ownShip', {}).get('initial', {}).get('position', {}).get('latitude')
    print(lat if lat is not None else 'NOT_FOUND')
except Exception as e:
    print('NOT_FOUND')
" 2>/dev/null || echo "NOT_FOUND")

# JSON accumulation (one JSON object per line in tmp file)
CHECKS_TMPFILE=$(mktemp)
trap "rm -f \$CHECKS_TMPFILE" EXIT

_add_json_result() {
    local id="$1" result="$2" detail="$3"
    python3 -c "
import json, sys
print(json.dumps({'id': sys.argv[1], 'result': sys.argv[2], 'detail': sys.argv[3]}))
" "$id" "$result" "$detail" >> "$CHECKS_TMPFILE"
}

# ─── Check helpers ─────────────────────────────────────────────────────────────

# Standard topic/node check: cmd output must contain expected string
check() {
    local desc="$1" cmd="$2" expected="$3"
    echo -n "  [$desc] ... "
    local result
    result=$(docker compose exec -T "$CONTAINER" bash -c \
        "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && $cmd" 2>&1) || true
    if echo "$result" | grep -q "$expected"; then
        echo -e "${GREEN}PASS${NC}"; PASS=$((PASS + 1))
        _add_json_result "$desc" "PASS" "${result:0:120}"
    else
        echo -e "${RED}FAIL${NC}"
        echo "    Expected: $expected"
        echo "    Got:      ${result:0:200}"
        FAIL=$((FAIL + 1))
        _add_json_result "$desc" "FAIL" "${result:0:200}"
    fi
}

# Hz check: ros2 topic hz --window N must report average rate >= min_hz
check_hz_min() {
    local desc="$1" topic="$2" min_hz="$3"
    echo -n "  [R1a $desc ≥${min_hz}Hz] ... "
    local result
    result=$(docker compose exec -T "$CONTAINER" bash -c \
        "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
         timeout 8 ros2 topic hz --window 5 $topic 2>&1 | head -5") || true
    local hz_val
    hz_val=$(echo "$result" | awk '/average rate:/ {print $3}' | head -1 || true)
    hz_val="${hz_val:-0}"
    if python3 -c "import sys; sys.exit(0 if float('$hz_val') >= $min_hz else 1)" 2>/dev/null; then
        echo -e "${GREEN}PASS${NC} (${hz_val} Hz)"; PASS=$((PASS + 1))
        _add_json_result "R1a $desc" "PASS" "hz=$hz_val expected>=$min_hz"
    else
        echo -e "${RED}FAIL${NC} (${hz_val} Hz, need ≥ $min_hz)"
        FAIL=$((FAIL + 1))
        _add_json_result "R1a $desc" "FAIL" "hz=$hz_val expected>=$min_hz; raw=${result:0:200}"
    fi
}

# Param check: ros2 param get /node param must match expected float (±tolerance)
check_param_float() {
    local desc="$1" node="$2" param="$3" expected="$4" tol="${5:-0.0001}"
    echo -n "  [R1b $desc] ... "
    local result
    result=$(docker compose exec -T "$CONTAINER" bash -c \
        "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
         ros2 param get $node $param 2>&1") || true
    local val
    val=$(echo "$result" | sed -E 's/.*:[[:space:]]+//' | tr -d '\r' | xargs | head -1 || true)
    val="${val:-NOT_FOUND}"
    if python3 -c "
import sys
try:
    sys.exit(0 if abs(float('$val') - $expected) <= $tol else 1)
except:
    sys.exit(1)
" 2>/dev/null; then
        echo -e "${GREEN}PASS${NC} ($val)"; PASS=$((PASS + 1))
        _add_json_result "R1b $desc" "PASS" "param=$val expected=$expected"
    else
        echo -e "${RED}FAIL${NC} (got $val, expected $expected ±$tol)"
        echo "    Raw: ${result:0:200}"
        FAIL=$((FAIL + 1))
        _add_json_result "R1b $desc" "FAIL" "param=$val expected=$expected tol=$tol; raw=${result:0:200}"
    fi
}

# Topic echo check: ros2 topic echo --once must produce non-empty output (msg arrived)
check_topic_nonempty() {
    local id="$1" topic="$2"
    echo -n "  [$id $topic] ... "
    local result
    result=$(docker compose exec -T "$CONTAINER" bash -c \
        "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
         timeout 10 ros2 topic echo --once $topic 2>&1") || true
    # Valid output contains "---" separator or field values; error output won't have "---"
    if echo "$result" | grep -q -e "---" -e "stamp:" -e "header:"; then
        echo -e "${GREEN}PASS${NC}"; PASS=$((PASS + 1))
        _add_json_result "$id $topic" "PASS" "msg received"
    else
        echo -e "${RED}FAIL${NC} (no msg within 10s or topic missing)"
        echo "    Got: ${result:0:200}"
        FAIL=$((FAIL + 1))
        _add_json_result "$id $topic" "FAIL" "${result:0:200}"
    fi
}

# Fatal-log check: docker compose logs <service> must contain no 'fatal' (case-insensitive)
check_no_fatal_logs() {
    local desc="$1" service="$2"
    echo -n "  [$desc] ... "
    local fatal_lines
    fatal_lines=$(docker compose logs "$service" 2>&1 | grep -i "fatal" || true)
    if [[ -z "$fatal_lines" ]]; then
        echo -e "${GREEN}PASS${NC}"; PASS=$((PASS + 1))
        _add_json_result "$desc" "PASS" "no fatal lines"
    else
        local count
        count=$(echo "$fatal_lines" | wc -l | tr -d ' ')
        echo -e "${RED}FAIL${NC} ($count fatal line(s))"
        echo "${fatal_lines:0:400}"
        FAIL=$((FAIL + 1))
        _add_json_result "$desc" "FAIL" "fatal_count=$count; sample=${fatal_lines:0:200}"
    fi
}

# ─── Header ────────────────────────────────────────────────────────────────────

echo "============================================"
echo " DEMO-1 End-to-End Verification"
echo " Container/Service: $CONTAINER"
echo " Timeout:           ${TIMEOUT}s"
echo " Scenario YAML:     $SCENARIO_YAML"
echo " Expected lat:      $EXPECTED_LAT"
echo " Timestamp:         $TIMESTAMP"
echo "============================================"

# ─── Stage 1: wait for /sil/own_ship_state first frame ─────────────────────────

echo ""; echo "[1/4] Stage check: /sil/own_ship_state first frame (max 30s)..."
STAGE1_ELAPSED=0
STAGE1_GOT_FRAME=0
while [ $STAGE1_ELAPSED -lt 30 ]; do
    if docker compose exec -T "$CONTAINER" bash -c \
        "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
         ros2 topic hz /sil/own_ship_state --window 3 2>&1 | grep -q 'average rate'" 2>/dev/null; then
        echo -e "  ${GREEN}/sil/own_ship_state publishing after ${STAGE1_ELAPSED}s${NC}"
        STAGE1_GOT_FRAME=1
        break
    fi
    sleep 2; STAGE1_ELAPSED=$((STAGE1_ELAPSED + 2))
done
if [ "$STAGE1_GOT_FRAME" -eq 0 ]; then
    echo -e "${RED}FAIL: /sil/own_ship_state not publishing after 30s${NC}"
    echo "=== Diagnostic: container logs (last 50 lines) ==="
    docker compose logs --tail=50 sil-nodes 2>/dev/null || true
    echo "=== Diagnostic: node list ==="
    docker compose exec -T "$CONTAINER" bash -c \
        "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && ros2 node list" 2>/dev/null || true
    exit 1
fi

# ─── Stage 2: wait for 16+ nodes ───────────────────────────────────────────────

echo ""; echo "[2/4] Waiting for all 16+ nodes active (max ${TIMEOUT}s)..."
ELAPSED=0
NODE_COUNT=0
while [ $ELAPSED -lt $TIMEOUT ]; do
    NODE_COUNT=$(docker compose exec -T "$CONTAINER" bash -c \
        "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
         ros2 node list 2>/dev/null | wc -l" 2>/dev/null || echo "0")
    if [ "$NODE_COUNT" -ge 16 ]; then
        echo -e "  ${GREEN}$NODE_COUNT nodes detected — system ready${NC}"; break
    fi
    echo "  $NODE_COUNT nodes (need >=16) ... waiting 5s"
    sleep 5; ELAPSED=$((ELAPSED + 5))
done
if [ "$NODE_COUNT" -lt 16 ]; then
    echo -e "${RED}FAIL: Only $NODE_COUNT nodes after ${TIMEOUT}s${NC}"
    docker compose exec -T "$CONTAINER" bash -c \
        "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && ros2 node list" 2>/dev/null || true
    echo ""; echo "=== Diagnostic: node info for expected nodes ==="
    for node_name in m1_odd_envelope_manager m2_world_model m3_mission_manager behavior_arbiter m5_mid_mpc_node m6_colregs_reasoner m7_safety_supervisor m8_hmi_transparency_bridge ship_dynamics_node; do
        echo "--- $node_name ---"
        docker compose exec -T "$CONTAINER" bash -c \
            "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
             ros2 node info /$node_name 2>&1 || echo 'NOT FOUND'" 2>/dev/null || true
    done
    exit 1
fi

# ─── Stage 3: Baseline topic / node checks ─────────────────────────────────────

echo ""; echo "[3/4] Running baseline verification checks..."

# L3 node presence
check "L3 m1_odd_envelope_manager" "ros2 node list" "m1_odd_envelope_manager"
check "L3 m2_world_model" "ros2 node list" "m2_world_model"
check "L3 m3_mission_manager" "ros2 node list" "m3_mission_manager"
check "L3 behavior_arbiter" "ros2 node list" "behavior_arbiter"
check "L3 m5_mid_mpc_node" "ros2 node list" "m5_mid_mpc_node"
check "L3 m6_colregs_reasoner" "ros2 node list" "m6_colregs_reasoner"
check "L3 m7_safety_supervisor" "ros2 node list" "m7_safety_supervisor"
check "L3 m8_hmi_transparency_bridge" "ros2 node list" "m8_hmi_transparency_bridge"

# SIL sim topics
check "/sil/own_ship_state publishing" \
    "timeout 5 ros2 topic hz /sil/own_ship_state --window 3 2>&1 | grep -m 1 'average rate'" "average rate"

# L3 internal topics
check "/l3/m2/world_state publishing" \
    "timeout 5 ros2 topic hz /l3/m2/world_state --window 3 2>&1 | grep -m 1 'average rate'" "average rate"
check "/l3/m4/behavior_plan publishing" \
    "timeout 5 ros2 topic hz /l3/m4/behavior_plan --window 3 2>&1 | grep -m 1 'average rate'" "average rate"
check "/l3/m5/avoidance_plan publishing" \
    "timeout 5 ros2 topic hz /l3/m5/avoidance_plan --window 3 2>&1 | grep -m 1 'average rate'" "average rate"

# Bridge topics
check "/sil/actuator_cmd publishing (bridge)" \
    "timeout 5 ros2 topic hz /sil/actuator_cmd --window 3 2>&1 | grep -m 1 'average rate'" "average rate"
check "/sil/module_pulse publishing (bridge)" \
    "timeout 5 ros2 topic hz /sil/module_pulse --window 3 2>&1 | grep -m 1 'average rate'" "average rate"
check "/sil/m8_ui_state publishing (bridge)" \
    "timeout 5 ros2 topic hz /sil/m8_ui_state --window 3 2>&1 | grep -m 1 'average rate'" "average rate"

# ─── Stage 4: R1 deep validation ───────────────────────────────────────────────

echo ""; echo "[4/4] R1 deep validation checks..."

# R1a: ship_dynamics publishing ≥ 40 Hz (nominal 50 Hz)
check_hz_min "own_ship_state" "/sil/own_ship_state" 40

# R1b: B2 scenario parameter injection — origin_lat matches scenario YAML
if [[ "$EXPECTED_LAT" != "NOT_FOUND" ]]; then
    check_param_float "origin_lat=scenario" \
        "/ship_dynamics_node" "origin_lat" "$EXPECTED_LAT" "0.0001"
else
    echo -e "  [R1b origin_lat] ${YELLOW}SKIP${NC} (could not read $SCENARIO_YAML)"
fi

# R1c: M4 BehaviorArbiter truly publishing (not stub)
check_topic_nonempty "R1c M4" "/l3/m4/behavior_plan"

# R1d: M5 TacticalPlanner truly publishing (not stub)
check_topic_nonempty "R1d M5" "/l3/m5/avoidance_plan"

# R1e: M7 SafetySupervisor heartbeat alive
check_topic_nonempty "R1e M7" "/l3/m7/heartbeat"

# R1f: B3 fail-loud — no fatal lines in sil-nodes logs
check_no_fatal_logs "R1f no-fatal-logs" "sil-nodes"

# ─── Summary ───────────────────────────────────────────────────────────────────

echo ""
echo "============================================"
echo -e " Results: ${GREEN}$PASS passed${NC}, ${RED}$FAIL failed${NC}"
echo "============================================"

# ─── JSON report ───────────────────────────────────────────────────────────────

mkdir -p evidence
REPORT_FILE="evidence/demo1-e2e-verification-${TIMESTAMP}.json"

python3 - <<PYEOF
import json, sys

checks = []
try:
    with open('$CHECKS_TMPFILE') as f:
        for line in f:
            line = line.strip()
            if line:
                checks.append(json.loads(line))
except Exception as e:
    checks = [{"id": "parse_error", "result": "FAIL", "detail": str(e)}]

report = {
    "timestamp": "$TIMESTAMP",
    "scenario": "imazu-08-ms",
    "service": "$CONTAINER",
    "scenario_yaml": "$SCENARIO_YAML",
    "expected_lat": "$EXPECTED_LAT",
    "checks": checks,
    "summary": {
        "pass": $PASS,
        "fail": $FAIL,
        "total": $PASS + $FAIL
    }
}
with open('$REPORT_FILE', 'w') as f:
    json.dump(report, f, indent=2)
print(f"  Report written: $REPORT_FILE")
PYEOF

if [ "$FAIL" -gt 0 ]; then
    echo ""
    echo "=== Diagnostic: node list ==="
    docker compose exec -T "$CONTAINER" bash -c \
        "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && ros2 node list" 2>/dev/null || true
    echo ""
    echo "=== Diagnostic: topic list ==="
    docker compose exec -T "$CONTAINER" bash -c \
        "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && ros2 topic list" 2>/dev/null || true
    exit 1
fi

echo -e "${GREEN}All DEMO-1 E2E checks passed.${NC}"
exit 0
