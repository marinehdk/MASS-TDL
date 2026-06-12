from __future__ import annotations

import socketserver
import threading
from typing import Any

from external_adapters.ipc import decode_line

try:
    import rclpy
    from builtin_interfaces.msg import Time
    from geometry_msgs.msg import PoseStamped
    from nav_msgs.msg import Path
    from rclpy.node import Node
    from std_msgs.msg import Header
except ImportError:
    rclpy = None
    Time = None
    PoseStamped = None
    Path = None
    Node = object
    Header = None


class _ThreadingTCPServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


class RouteOutExternalPathNode(Node):
    def __init__(self) -> None:
        super().__init__("external_route_out_path")
        self.declare_parameter("host", "127.0.0.1")
        self.declare_parameter("port", 8766)
        host = self.get_parameter("host").value
        port = int(self.get_parameter("port").value)

        self._path_pub = self.create_publisher(Path, "/ship/waypoints", 5)
        self._server = _ThreadingTCPServer((host, port), _handler_for(self))
        self._thread = threading.Thread(target=self._server.serve_forever, daemon=True)
        self._thread.start()
        self.get_logger().info(f"external_route_out_path listening on {host}:{port}")

    def _handle_payload(self, payload: dict[str, Any]) -> None:
        if payload.get("kind") == "route_out_path":
            self._path_pub.publish(path_payload_to_plain_path(payload))

    def destroy_node(self) -> None:
        if hasattr(self, "_server"):
            self._server.shutdown()
            self._server.server_close()
        super().destroy_node()


def _handler_for(node: RouteOutExternalPathNode):
    class _PayloadHandler(socketserver.StreamRequestHandler):
        def handle(self) -> None:
            for line in self.rfile:
                try:
                    node._handle_payload(decode_line(line))
                except Exception as exc:  # pragma: no cover - exercised in ROS integration.
                    node.get_logger().warn(f"failed to handle route_out payload: {exc}")

    return _PayloadHandler


def path_payload_to_plain_path(payload: dict[str, Any]):
    stamp = _time(payload.get("stamp"))
    msg = Path()
    msg.header = _header(stamp)
    msg.poses = [_pose(point, stamp) for point in payload.get("points", [])]
    return msg


def _time(payload: dict[str, Any] | None):
    stamp = payload or {}
    msg = Time()
    msg.sec = int(stamp.get("sec", 0))
    msg.nanosec = int(stamp.get("nanosec", 0))
    return msg


def _header(stamp):
    msg = Header()
    msg.stamp = stamp
    msg.frame_id = "WGS84"
    return msg


def _pose(point: dict[str, Any], stamp):
    msg = PoseStamped()
    msg.header = _header(stamp)
    msg.pose.position.x = float(point.get("lon", 0.0))
    msg.pose.position.y = float(point.get("lat", 0.0))
    msg.pose.position.z = float(point.get("speed_kn", 0.0))
    msg.pose.orientation.w = 1.0
    return msg


def main(args=None) -> None:
    if rclpy is None:
        raise RuntimeError("rclpy is required to run external_route_out_path")
    rclpy.init(args=args)
    node = RouteOutExternalPathNode()
    try:
        rclpy.spin(node)
    finally:
        node.destroy_node()
        rclpy.shutdown()
