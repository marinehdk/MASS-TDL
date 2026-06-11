#!/usr/bin/env bash
set -euo pipefail

export PYTHONPATH="src/sim_workbench/ais_twin:${PYTHONPATH:-}"
ros2 run ais_twin ais_twin_replay_node --ros-args \
  -p dataset_dir:=data/ais_twin/safe_route \
  -p route_path:=scenarios/集成测试/safe_route.yaml \
  -p top_n:=20 \
  "$@"
