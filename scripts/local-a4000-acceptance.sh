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
  echo "profiles=${COMPOSE_PROFILES:-}"
  echo "integration_profile=${TDL_INTEGRATION_PROFILE:-}"
  echo "runtime_profile=${TDL_RUNTIME_PROFILE:-}"
  echo "health=${ORCH_URL}/api/v1/health"
  echo "integration=/api/v1/integration/profiles"
  echo "runtime=/api/v1/runtime/summary"
  echo "runtime_probe=/api/v1/runtime/probe"
  echo "reclaim_stale_project=${RECLAIM_STALE_LOCAL_PROJECT:-0}"
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

compose_project="${COMPOSE_PROJECT_NAME:-mass-l3-sil}"
current_root="$(pwd)"
existing_roots="$(
  docker ps -a \
    --filter "label=com.docker.compose.project=${compose_project}" \
    --format '{{.Label "com.docker.compose.project.working_dir"}}' \
    | sort -u \
    | sed '/^$/d'
)"

recreate_project=0
up_args=(up -d --build)
core_services=(sil-orchestrator sil-nodes foxglove-bridge martin-tile-server)
plugin_services=(plugin-hydro-fossen plugin-route-l2-main plugin-fusion-yougc)
if [[ -n "$existing_roots" && "$existing_roots" != "$current_root" ]]; then
  if [[ "${RECLAIM_STALE_LOCAL_PROJECT:-0}" != "1" ]]; then
    echo "ERROR: local compose project ${compose_project} belongs to another checkout: ${existing_roots}" >&2
    echo "Current checkout: ${current_root}" >&2
    echo "Stop that stack or rerun with RECLAIM_STALE_LOCAL_PROJECT=1 to recreate it for this checkout." >&2
    exit 2
  fi
  echo "Recreating local compose project ${compose_project}; existing working_dir=${existing_roots}; current=${current_root}"
  recreate_project=1
  up_args+=(--force-recreate)
fi

if [[ "${TDL_RUNTIME_PROFILE:-}" == internal-* ]]; then
  docker compose "${up_args[@]}" "${core_services[@]}"
  docker compose stop "${plugin_services[@]}" plugin-route-tdl-mock >/dev/null 2>&1 || true
else
  docker compose "${up_args[@]}" "${core_services[@]}" "${plugin_services[@]}"
  if [[ "$recreate_project" == "1" ]]; then
    docker compose stop plugin-route-tdl-mock >/dev/null 2>&1 || true
    docker compose create --force-recreate plugin-route-tdl-mock >/dev/null
  else
    docker compose create --no-recreate plugin-route-tdl-mock >/dev/null
  fi
  docker compose stop plugin-route-tdl-mock >/dev/null 2>&1 || true
fi

for _ in $(seq 1 60); do
  if curl -sk --max-time 2 "${ORCH_URL}/api/v1/health" | grep -q '"status":"ok"'; then
    break
  fi
  sleep 2
done

curl -sk --fail "${ORCH_URL}/api/v1/health" | grep -q '"status":"ok"'
curl -sk --fail "${ORCH_URL}/api/v1/integration/profiles" | grep -q '"active_profile"'

mkdir -p runs
curl -sk --fail "${ORCH_URL}/api/v1/runtime/summary" | grep -q '"active_profile"'
runtime_probe_path="runs/local_runtime_probe_$(date +%Y%m%d_%H%M%S).json"
curl -sk --fail -X POST "${ORCH_URL}/api/v1/runtime/probe" \
  | tee "$runtime_probe_path"
grep -q '"verdict":"GO"' "$runtime_probe_path"

curl -sk --fail -X POST "${ORCH_URL}/api/v1/integration/probe" \
  | tee "runs/local_a4000_container_probe_$(date +%Y%m%d_%H%M%S).json"

docker compose exec -T sil-nodes bash -lc \
  'source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && test "$ROS_DOMAIN_ID" = "42" && ros2 topic list >/tmp/local_a4000_topics.txt'

echo "LOCAL A4000 CONTAINER ACCEPTANCE PASS"
