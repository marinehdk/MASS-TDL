#!/bin/bash
# Load all 9 SIL nodes into component_container_mt
# Usage: ./scripts/orb-sil-manager.sh [start|stop|status]

set -e

CONTAINER="sil_container"

case "${1:-start}" in
  start)
    echo "Loading SIL nodes into $CONTAINER..."
    ros2 component load /$CONTAINER sil_lifecycle scenario_lifecycle_mgr::ScenarioLifecycleMgr
    ros2 component load /$CONTAINER scenario_authoring ScenarioAuthoringNode
    ros2 component load /$CONTAINER ship_dynamics ShipDynamicsNode
    ros2 component load /$CONTAINER env_disturbance EnvDisturbanceNode
    ros2 component load /$CONTAINER target_vessel TargetVesselNode
    ros2 component load /$CONTAINER sensor_mock SensorMockNode
    ros2 component load /$CONTAINER tracker_mock TrackerMockNode
    ros2 component load /$CONTAINER fault_injection FaultInjectionNode
    ros2 component load /$CONTAINER scoring ScoringNode
    echo "All 9 nodes loaded."
    ;;

  stop)
    echo "Stopping $CONTAINER..."
    ros2 component unload /$CONTAINER '*' 2>/dev/null || true
    ;;

  status)
    ros2 component list 2>/dev/null || echo "ROS2 not running"
    ;;

  *)
    echo "Usage: $0 {start|stop|status}"
    exit 1
    ;;
esac
