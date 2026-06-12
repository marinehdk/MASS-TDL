from __future__ import annotations

import socket

from external_adapters.converters import avoidance_plan_to_path_payload
from external_adapters.ipc import encode_payload

try:
    import rclpy
    from l3_msgs.msg import AvoidancePlan
    from rclpy.node import Node
except ImportError:
    rclpy = None
    AvoidancePlan = None
    Node = object


class RouteOutTdlNode(Node):
    def __init__(self) -> None:
        super().__init__("external_route_out_tdl")
        self.declare_parameter("host", "127.0.0.1")
        self.declare_parameter("port", 8766)
        self._host = self.get_parameter("host").value
        self._port = int(self.get_parameter("port").value)
        self.create_subscription(
            AvoidancePlan,
            "/l3/m5/avoidance_plan",
            self._on_plan,
            10,
        )

    def _on_plan(self, msg) -> None:
        if not getattr(msg, "waypoints", []):
            self.get_logger().warn("route_out_tdl ignored empty avoidance plan")
            return

        payload = avoidance_plan_to_path_payload(msg)
        try:
            with socket.create_connection((self._host, self._port), timeout=1.0) as sock:
                sock.sendall(encode_payload(payload))
        except OSError as exc:
            self.get_logger().warn(f"route_out_tdl failed to send route_out_path: {exc}")


def main(args=None) -> None:
    if rclpy is None:
        raise RuntimeError("rclpy is required to run external_route_out_tdl")
    rclpy.init(args=args)
    node = RouteOutTdlNode()
    try:
        rclpy.spin(node)
    finally:
        node.destroy_node()
        rclpy.shutdown()
