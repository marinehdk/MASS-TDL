#!/usr/bin/env bash
# tools/orbstack-verify.sh — D1.3c Docker health + DDS multicast verification
# Usage: bash tools/orbstack-verify.sh
set -euo pipefail

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
pass=0; fail=0

check() {
    local label="$1"; shift
    if "$@"; then
        echo -e "  ${GREEN}PASS${NC} $label"
        ((pass++))
    else
        echo -e "  ${RED}FAIL${NC} $label"
        ((fail++))
    fi
}

echo "============================================"
echo " D1.3c OrbStack Service Verification"
echo " Target: 5 services healthy, zero jazzy refs"
echo "============================================"
echo ""

echo "[0] Prerequisites"
check "docker CLI present" command -v docker >/dev/null

echo ""
echo "[1] Jazzy reference scan"
jazzy_count=$(grep -rI 'jazzy' docker-compose.yml docker/ 2>/dev/null | grep -v '.git' | grep -v 'archive' | wc -l | tr -d ' ')
check "Zero jazzy references (found: $jazzy_count)" [ "$jazzy_count" -eq 0 ]

echo ""
echo "[2] Service health check (wait up to 60s)"
SERVICES=(sil-orchestrator-1 sil-component-container-1 foxglove-bridge-1 web-1 martin-tile-server-1)
for svc in "${SERVICES[@]}"; do
    check "Service $svc running" timeout 5 docker compose ps --format json 2>/dev/null | grep -q "\"Name\":\"$svc\""
done

echo ""
echo "[3] DDS multicast cross-service test"
docker compose exec -T foxglove-bridge timeout 3 bash -c \
  "source /opt/ros/humble/setup.bash && ros2 multicast send" 2>/dev/null &
sleep 1
recv_output=$(docker compose exec -T sil-component-container timeout 5 bash -c \
  "source /opt/ros/humble/setup.bash && ros2 multicast receive" 2>&1 || true)
if echo "$recv_output" | grep -qi "received"; then
    echo -e "  ${GREEN}PASS${NC} DDS multicast: cross-service packets received"
    ((pass++))
else
    echo -e "  ${RED}FAIL${NC} DDS multicast: no packets received across services"
    ((fail++))
fi

echo ""
echo "[4] ROS_DOMAIN_ID verification"
for svc in sil-orchestrator sil-component-container foxglove-bridge; do
    domain=$(docker compose exec -T "$svc" printenv ROS_DOMAIN_ID 2>/dev/null | tr -d '\r\n' || echo "UNSET")
    check "$svc ROS_DOMAIN_ID=$domain" [ "$domain" = "0" ]
done

echo ""
echo "============================================"
echo -e " Results: ${GREEN}$pass passed${NC}, ${RED}$fail failed${NC}"
echo "============================================"
[ "$fail" -eq 0 ] && exit 0 || exit 1
