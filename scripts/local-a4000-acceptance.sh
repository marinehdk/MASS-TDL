#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

DRY_RUN=0
if [[ "${1:-}" == "--dry-run" ]]; then
  DRY_RUN=1
fi

source scripts/local-a4000-env.sh

if [[ "$DRY_RUN" == "1" ]]; then
  echo "compose=$COMPOSE_FILE"
  echo "health=${ORCH_URL}/api/v1/health"
  echo "integration=/api/v1/integration/profiles"
  echo "domain=$ROS_DOMAIN_ID"
  echo "certs=certs/sil.crt certs/sil.key"
  exit 0
fi

command -v docker >/dev/null
if [[ ! -s certs/sil.crt || ! -s certs/sil.key ]]; then
  command -v openssl >/dev/null
  mkdir -p certs
  openssl req -x509 -nodes -newkey rsa:2048 \
    -keyout certs/sil.key \
    -out certs/sil.crt \
    -days 365 \
    -subj "/CN=localhost" \
    -addext "subjectAltName=DNS:localhost,IP:127.0.0.1" \
    >/dev/null 2>&1
fi
docker compose config -q
docker compose up -d --build sil-orchestrator sil-nodes foxglove-bridge

for _ in $(seq 1 60); do
  if curl -sk --max-time 2 "${ORCH_URL}/api/v1/health" | grep -q '"status":"ok"'; then
    break
  fi
  sleep 2
done

curl -sk --fail "${ORCH_URL}/api/v1/health" | grep -q '"status":"ok"'
curl -sk --fail "${ORCH_URL}/api/v1/integration/profiles" | grep -q '"active_profile"'

mkdir -p runs
curl -sk --fail -X POST "${ORCH_URL}/api/v1/integration/probe" \
  | tee "runs/local_a4000_container_probe_$(date +%Y%m%d_%H%M%S).json"

docker compose exec -T sil-nodes bash -lc \
  'source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && test "$ROS_DOMAIN_ID" = "42" && ros2 topic list >/tmp/local_a4000_topics.txt'

echo "LOCAL A4000 CONTAINER ACCEPTANCE PASS"
