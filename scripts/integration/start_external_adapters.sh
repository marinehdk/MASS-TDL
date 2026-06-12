#!/usr/bin/env bash
set -euo pipefail

PROFILE="${TDL_INTEGRATION_PROFILE:-default}"
TDL_DOMAIN_ID="${TDL_DOMAIN_ID:-${ROS_DOMAIN_ID:-42}}"
SIM_DOMAIN_ID="${SIM_DOMAIN_ID:-10}"
DRY_RUN=0

if [[ "${1:-}" == "--dry-run" ]]; then
  DRY_RUN=1
fi

if [[ "$PROFILE" == "default" ]]; then
  echo "external adapters disabled for profile=default"
  exit 0
fi

if [[ "$PROFILE" != "a4000_external" && "$PROFILE" != "hybrid_debug" ]]; then
  echo "unsupported integration profile=$PROFILE" >&2
  exit 2
fi

COMMANDS=(
  "ROS_DOMAIN_ID=${TDL_DOMAIN_ID} ros2 run external_adapters external_tdl_ingress --ros-args -p port:=8765"
  "ROS_DOMAIN_ID=${TDL_DOMAIN_ID} ros2 run external_adapters external_route_out_tdl --ros-args -p port:=8766"
  "ROS_DOMAIN_ID=${SIM_DOMAIN_ID} ros2 run external_adapters external_route_out_path --ros-args -p port:=8766"
)

for cmd in "${COMMANDS[@]}"; do
  echo "$cmd"
  if [[ "$DRY_RUN" == "0" ]]; then
    bash -lc "$cmd" &
  fi
done

if [[ "$DRY_RUN" == "0" ]]; then
  wait
fi
