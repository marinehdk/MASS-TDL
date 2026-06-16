#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

source scripts/local-a4000-env.sh

timeout_s="${L2_PROBE_TIMEOUT_S:-20}"
if ! [[ "$timeout_s" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
  echo "L2_PROBE_TIMEOUT_S must be a numeric timeout, got: $timeout_s" >&2
  exit 2
fi

docker compose exec -T sil-nodes \
  bash -lc "ps -eo pid=,args= | grep -F external_tdl_ingress | grep -v 'grep -F external_tdl_ingress' | grep -v 'bash -lc'"

docker compose exec -T plugin-route-l2-main \
  bash -lc "ps -eo pid=,args= | grep -F l2_route_plan_adaptor | grep -v 'grep -F l2_route_plan_adaptor' | grep -v 'bash -lc'"

docker compose exec -T plugin-route-l2-main \
  bash -lc "source /opt/ros/humble/setup.bash && source /opt/l2_ws/install/setup.bash && timeout \"$timeout_s\" ros2 topic echo --once /route_planning/route_plan"

planned_route_output="$(
  docker compose exec -T sil-nodes \
    bash -lc "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && timeout \"$timeout_s\" ros2 topic echo --once /l2/planned_route"
)"
printf '%s\n' "$planned_route_output"
grep -F "external L2 route plan converted" <<<"$planned_route_output" >/dev/null

echo "L2_EXTERNAL_PLUGIN_PROBE_PASS"
