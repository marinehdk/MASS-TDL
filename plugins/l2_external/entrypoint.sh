#!/usr/bin/env bash
set -eo pipefail

source /opt/ros/humble/setup.bash
source /opt/l2_ws/install/setup.bash
set -u

: "${ROS_DOMAIN_ID:=42}"
: "${RMW_IMPLEMENTATION:=rmw_cyclonedds_cpp}"
: "${GNC_ROUTE_PLANNING_DIR:=/var/lib/l2_route}"
: "${SHIP_FEEDBACK_LOG_DIR:=/var/lib/l2_route/logs}"
: "${L2_ROUTE_OUTPUT_PATH:=${GNC_ROUTE_PLANNING_DIR}/gnc_bridge_route.json}"
: "${L2_SCENARIO_YAML:=/var/sil/scenarios/集成测试/safe_route.yaml}"
: "${TDL_INGRESS_HOST:=127.0.0.1}"
: "${TDL_INGRESS_PORT:=8765}"
: "${L2_ROUTE_STRICT_ACTIVE:=1}"
: "${L2_ROUTE_REMOVE_ON_START:=1}"

export ROS_DOMAIN_ID
export RMW_IMPLEMENTATION
export GNC_ROUTE_PLANNING_DIR
export SHIP_FEEDBACK_LOG_DIR
export L2_ROUTE_OUTPUT_PATH
export L2_SCENARIO_YAML
export TDL_INGRESS_HOST
export TDL_INGRESS_PORT
export L2_ROUTE_STRICT_ACTIVE
export L2_ROUTE_REMOVE_ON_START

mkdir -p "$GNC_ROUTE_PLANNING_DIR" "$SHIP_FEEDBACK_LOG_DIR"

if [ "$L2_ROUTE_REMOVE_ON_START" = "1" ]; then
  rm -f "$L2_ROUTE_OUTPUT_PATH" "${L2_ROUTE_OUTPUT_PATH}.tmp"
fi

children=()

terminate_children() {
  trap - INT TERM EXIT
  if [ "${#children[@]}" -gt 0 ]; then
    kill "${children[@]}" 2>/dev/null || true
    wait "${children[@]}" 2>/dev/null || true
  fi
}

trap terminate_children INT TERM EXIT

python3 -m external_adapters.l2_route_seed &
children+=("$!")

ros2 run route_planning_ros2 gnc_sim_node &
children+=("$!")

python3 -m external_adapters.l2_route_plan_adaptor &
children+=("$!")

set +e
wait -n "${children[@]}"
status="$?"
set -e

terminate_children
exit "$status"
