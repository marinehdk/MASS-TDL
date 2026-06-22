#!/usr/bin/env bash
set -euo pipefail

MODE="${TDL_RUNTIME_MODE:-}"

if [[ -z "$MODE" && -t 0 && -t 1 ]]; then
  printf 'Runtime mode [internal/external] (default: internal, no plugins): '
  read -r MODE || MODE=""
fi

case "${MODE:-internal}" in
  ""|internal|i|I)
    MODE="internal"
    export COMPOSE_PROFILES=""
    export TDL_RUNTIME_PROFILE="internal-local"
    export TDL_INTEGRATION_PROFILE="default"
    ;;
  external|plugin|plugins|e|E)
    MODE="external"
    export COMPOSE_PROFILES="plugins"
    export TDL_RUNTIME_PROFILE="integration-a4000"
    export TDL_INTEGRATION_PROFILE="a4000_external"
    ;;
  *)
    echo "Unknown TDL_RUNTIME_MODE '$MODE' (use internal or external)" >&2
    exit 2
    ;;
esac

echo "runtime_mode=$MODE"
echo "COMPOSE_FILE=${COMPOSE_FILE:-docker-compose.yml}"
echo "COMPOSE_PROFILES=${COMPOSE_PROFILES:-}"
echo "TDL_RUNTIME_PROFILE=$TDL_RUNTIME_PROFILE"
echo "TDL_INTEGRATION_PROFILE=$TDL_INTEGRATION_PROFILE"

if [[ "$MODE" == "internal" ]]; then
  mapfile -t plugin_services < <(docker compose config --services 2>/dev/null | grep '^plugin-' || true)
  if (( ${#plugin_services[@]} > 0 )); then
    docker compose stop "${plugin_services[@]}" >/dev/null 2>&1 || true
  fi
  docker compose up -d sil-orchestrator sil-nodes foxglove-bridge martin-tile-server
else
  docker compose up -d
fi

pm2 start ecosystem.config.cjs
