#!/bin/bash
set -euo pipefail

TIMEOUT=120
CONTAINER="mass-l3-sil-nodes-1"
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
PASS=0; FAIL=0

while [[ $# -gt 0 ]]; do
    case $1 in --timeout) TIMEOUT="$2"; shift 2 ;; --container) CONTAINER="$2"; shift 2 ;; *) echo "Unknown arg: $1"; exit 2 ;; esac
done

check() {
    local desc="$1" cmd="$2" expected="$3"
    echo -n "  [$desc] ... "
    local result
    result=$(docker compose exec -T "$CONTAINER" bash -c "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && $cmd" 2>&1) || true
    if echo "$result" | grep -q "$expected"; then
        echo -e "${GREEN}PASS${NC}"; ((PASS++))
    else
        echo -e "${RED}FAIL${NC}"; echo "    Expected: $expected"; echo "    Got:      ${result:0:200}"; ((FAIL++))
    fi
}

echo "============================================"
echo " DEMO-1 End-to-End Verification"
echo " Container: $CONTAINER"
echo " Timeout:   ${TIMEOUT}s"
echo "============================================"

echo ""; echo "[1/2] Waiting for SIL+L3 nodes to initialize (${TIMEOUT}s max)..."
ELAPSED=0
while [ $ELAPSED -lt $TIMEOUT ]; do
    NODE_COUNT=$(docker compose exec -T "$CONTAINER" bash -c "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && ros2 node list 2>/dev/null | wc -l" 2>/dev/null || echo "0")
    if [ "$NODE_COUNT" -ge 16 ]; then
        echo -e "  ${GREEN}$NODE_COUNT nodes detected — system ready${NC}"; break
    fi
    echo "  $NODE_COUNT nodes (need >=16) ... waiting 5s"
    sleep 5; ELAPSED=$((ELAPSED + 5))
done
if [ "$NODE_COUNT" -lt 16 ]; then
    echo -e "${RED}FAIL: Only $NODE_COUNT nodes after ${TIMEOUT}s${NC}"
    docker compose exec -T "$CONTAINER" bash -c "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && ros2 node list" 2>/dev/null || true
    exit 1
fi

echo ""; echo "[2/2] Running verification checks..."

# Check 1: 7 L3 nodes present
for node in m1_odd_manager m2_world_model m3_mission_manager m4_behavior_arbiter m5_tactical_planner m6_colregs_reasoner m7_safety_supervisor m8_hmi_bridge; do
    check "L3 $node" "ros2 node list | grep $node" "$node"
done

# Check 2: SIL sim topics alive
check "/sil/own_ship_state publishing" "ros2 topic hz /sil/own_ship_state --window 3 2>&1 | grep 'average rate'" "average rate"

# Check 3: L3 internal topics alive
check "/l3/m2/world_state publishing" "ros2 topic hz /l3/m2/world_state --window 3 2>&1 | grep 'average rate'" "average rate"
check "/l3/m4/behavior_plan publishing" "ros2 topic hz /l3/m4/behavior_plan --window 3 2>&1 | grep 'average rate'" "average rate"
check "/l3/m5/avoidance_plan publishing" "ros2 topic hz /l3/m5/avoidance_plan --window 3 2>&1 | grep 'average rate'" "average rate"

# Check 4: Bridge topics alive
check "/sil/actuator_cmd publishing (bridge)" "ros2 topic hz /sil/actuator_cmd --window 3 2>&1 | grep 'average rate'" "average rate"
check "/sil/module_pulse publishing (bridge)" "ros2 topic hz /sil/module_pulse --window 3 2>&1 | grep 'average rate'" "average rate"
check "/sil/m8_ui_state publishing (bridge)" "ros2 topic hz /sil/m8_ui_state --window 3 2>&1 | grep 'average rate'" "average rate"

# Summary
echo ""; echo "============================================"
echo -e " Results: ${GREEN}$PASS passed${NC}, ${RED}$FAIL failed${NC}"
echo "============================================"

if [ "$FAIL" -gt 0 ]; then
    echo ""; echo "=== Diagnostic: node list ==="
    docker compose exec -T "$CONTAINER" bash -c "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && ros2 node list" 2>/dev/null || true
    echo ""; echo "=== Diagnostic: topic list ==="
    docker compose exec -T "$CONTAINER" bash -c "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && ros2 topic list" 2>/dev/null || true
    exit 1
fi
echo -e "${GREEN}All DEMO-1 E2E checks passed.${NC}"
exit 0
