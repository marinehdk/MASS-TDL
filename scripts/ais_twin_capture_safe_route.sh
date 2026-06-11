#!/usr/bin/env bash
set -euo pipefail

CONFIG="src/sim_workbench/ais_twin/config/safe_route_aisstream.yaml"
export PYTHONPATH="src/sim_workbench/ais_twin:${PYTHONPATH:-}"

if [[ -z "${AISSTREAM_API_KEY:-}" ]]; then
  echo "AISSTREAM_API_KEY is required" >&2
  exit 2
fi

python3 -m ais_twin.capture_cli --config "${CONFIG}" "$@"
