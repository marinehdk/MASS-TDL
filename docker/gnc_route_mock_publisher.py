#!/usr/bin/env python3
"""GNC Route Mock Publisher (D1.8) — stands in for real L2 in Phase 1.

Subscribes /sil/lifecycle_status; on ACTIVE reads the scenario YAML's
ownShip.nominalRoute and publishes it as ship_interfaces/RoutePlan on
/route_planning/gnc_route_plan (latched). Re-publishes the latest plan at
1 Hz so late subscribers receive it.

Track A: migrated from the legacy GncRoutePlan (L3-local ship_interfaces)
to the GNC ship_interfaces/RoutePlan, which is now the single source for the
route contract. RoutePlan has no total_waypoints field (len(latitude) suffices).
"""
import os
import signal
import sys

import rclpy
from rclpy.node import Node
from rclpy.qos import (QoSProfile, QoSReliabilityPolicy,
                       QoSDurabilityPolicy, QoSHistoryPolicy)
from std_msgs.msg import String, Header
from builtin_interfaces.msg import Time as BuiltinTime
from sil_msgs.msg import LifecycleStatus
from ship_interfaces.msg import RoutePlan

SCENARIO_DIR = os.environ.get("SIL_SCENARIO_DIR", "/var/sil/scenarios")
PUBLISH_HZ = 1.0
LC_STATE_ACTIVE = 3


def _now(node: Node) -> BuiltinTime:
    s, ns = node.get_clock().now().seconds_nanoseconds()
    return BuiltinTime(sec=s, nanosec=ns)


class GncRouteMockPublisher(Node):
    def __init__(self):
        super().__init__("gnc_route_mock_publisher")
        self.declare_parameter("scenario_dir", SCENARIO_DIR)
        self._scenario_dir = self.get_parameter("scenario_dir").value
        self._is_active = False
        self._current_scenario_id = ""
        self._waypoints: list[tuple[float, float]] = []

        route_qos = QoSProfile(
            reliability=QoSReliabilityPolicy.RELIABLE,
            durability=QoSDurabilityPolicy.TRANSIENT_LOCAL,
            history=QoSHistoryPolicy.KEEP_LAST, depth=5)
        self._pub = self.create_publisher(
            RoutePlan, "/route_planning/gnc_route_plan", route_qos)

        self.create_subscription(LifecycleStatus, "/sil/lifecycle_status",
                                 self._on_lifecycle, 10)
        self.create_subscription(String, "/sil/scenario_loaded",
                                 self._on_scenario_loaded, 10)
        self.create_timer(1.0 / PUBLISH_HZ, self._on_timer)
        self.get_logger().info(
            f"GncRouteMockPublisher ready (dir={self._scenario_dir})")

    def _on_lifecycle(self, msg: LifecycleStatus):
        if msg.current_state != LC_STATE_ACTIVE:
            self._is_active = False
            return
        sid = msg.scenario_id
        if sid and not (sid == self._current_scenario_id and self._is_active):
            self._current_scenario_id = sid
            self._load(sid)

    def _on_scenario_loaded(self, msg: String):
        if msg.data and not (msg.data == self._current_scenario_id and self._is_active):
            self._current_scenario_id = msg.data
            self._load(msg.data)

    def _load(self, scenario_id: str):
        yaml_path = None
        for root, _, files in os.walk(self._scenario_dir):
            if f"{scenario_id}.yaml" in files:
                yaml_path = os.path.join(root, f"{scenario_id}.yaml")
                break
        if not yaml_path:
            self.get_logger().warn(f"YAML not found for {scenario_id}")
            return
        try:
            import yaml
            with open(yaml_path) as f:
                scenario = yaml.safe_load(f)
        except Exception as exc:
            self.get_logger().error(f"parse {yaml_path}: {exc}")
            return
        nominal = scenario.get("ownShip", {}).get("nominalRoute")
        if not nominal or len(nominal) < 2:
            self.get_logger().warn(f"no nominalRoute in {scenario_id}")
            return
        self._waypoints = [(float(wp["latitude"]), float(wp["longitude"]))
                           for wp in nominal]
        self._is_active = True
        self.get_logger().info(
            f"GNC route ACTIVE — {len(self._waypoints)} wp from {scenario_id}")

    def _on_timer(self):
        if not self._is_active or not self._waypoints:
            return
        msg = RoutePlan()
        msg.header = Header(stamp=_now(self), frame_id="map")
        msg.latitude = [lat for lat, _ in self._waypoints]
        msg.longitude = [lon for _, lon in self._waypoints]
        self._pub.publish(msg)


def main(args=None):
    rclpy.init(args=args)
    node = GncRouteMockPublisher()
    def _sig(_s, _f):
        node.destroy_node(); rclpy.shutdown(); sys.exit(0)
    signal.signal(signal.SIGINT, _sig)
    signal.signal(signal.SIGTERM, _sig)
    try:
        rclpy.spin(node)
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
