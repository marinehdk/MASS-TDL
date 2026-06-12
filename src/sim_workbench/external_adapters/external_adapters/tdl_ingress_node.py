from __future__ import annotations

import socketserver
import threading
from typing import Any

from external_adapters.ipc import decode_line

try:
    import rclpy
    from builtin_interfaces.msg import Time
    from geographic_msgs.msg import GeoPath, GeoPoint, GeoPoseStamped
    from l3_external_msgs.msg import (
        EnvironmentState,
        FilteredOwnShipState,
        PlannedRoute,
        TrackedTargetArray,
    )
    from l3_msgs.msg import EncounterClassification, TrackedTarget
    from rclpy.node import Node
    from rclpy.qos import QoSDurabilityPolicy, QoSHistoryPolicy, QoSProfile, QoSReliabilityPolicy
    from std_msgs.msg import Header
except ImportError:
    rclpy = None
    Time = None
    GeoPath = None
    GeoPoint = None
    GeoPoseStamped = None
    EnvironmentState = None
    FilteredOwnShipState = None
    PlannedRoute = None
    TrackedTargetArray = None
    EncounterClassification = None
    TrackedTarget = None
    Node = object
    QoSDurabilityPolicy = None
    QoSHistoryPolicy = None
    QoSProfile = None
    QoSReliabilityPolicy = None
    Header = None


class _ThreadingTCPServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


class ExternalTdlIngressNode(Node):
    def __init__(self) -> None:
        super().__init__("external_tdl_ingress")
        self.declare_parameter("host", "127.0.0.1")
        self.declare_parameter("port", 8765)
        host = self.get_parameter("host").value
        port = int(self.get_parameter("port").value)
        qos = _latched_reliable_qos()

        self._targets_pub = self.create_publisher(
            TrackedTargetArray, "/fusion/tracked_targets", qos
        )
        self._ownship_pub = self.create_publisher(
            FilteredOwnShipState, "/fusion/own_ship_state", qos
        )
        self._environment_pub = self.create_publisher(
            EnvironmentState, "/fusion/environment_state", qos
        )
        self._route_pub = self.create_publisher(PlannedRoute, "/l2/planned_route", qos)

        handler = _handler_for(self)
        self._server = _ThreadingTCPServer((host, port), handler)
        self._thread = threading.Thread(target=self._server.serve_forever, daemon=True)
        self._thread.start()
        self.get_logger().info(f"external_tdl_ingress listening on {host}:{port}")

    def _handle_payload(self, payload: dict[str, Any]) -> None:
        kind = payload["kind"]
        if kind == "targets":
            self._targets_pub.publish(_tracked_target_array(payload))
        elif kind == "ownship":
            self._ownship_pub.publish(_ownship(payload))
        elif kind == "environment":
            self._environment_pub.publish(_environment(payload))
        elif kind == "route_in":
            self._route_pub.publish(_planned_route(payload))
        elif kind == "route_out_path":
            self.get_logger().warn("route_out_path payload ignored by TDL ingress")
        else:
            raise ValueError(f"unknown payload kind: {kind}")

    def destroy_node(self) -> None:
        if hasattr(self, "_server"):
            self._server.shutdown()
            self._server.server_close()
        super().destroy_node()


def _handler_for(node: ExternalTdlIngressNode):
    class _PayloadHandler(socketserver.StreamRequestHandler):
        def handle(self) -> None:
            for line in self.rfile:
                try:
                    node._handle_payload(decode_line(line))
                except Exception as exc:  # pragma: no cover - exercised in ROS integration.
                    node.get_logger().warn(f"failed to handle external payload: {exc}")

    return _PayloadHandler


def _latched_reliable_qos():
    return QoSProfile(
        history=QoSHistoryPolicy.KEEP_LAST,
        depth=1,
        reliability=QoSReliabilityPolicy.RELIABLE,
        durability=QoSDurabilityPolicy.TRANSIENT_LOCAL,
    )


def _time(payload: dict[str, Any] | None):
    stamp = payload or {}
    msg = Time()
    msg.sec = int(stamp.get("sec", 0))
    msg.nanosec = int(stamp.get("nanosec", 0))
    return msg


def _header(payload: dict[str, Any] | None):
    header = payload or {}
    msg = Header()
    msg.stamp = _time(header.get("stamp"))
    msg.frame_id = str(header.get("frame_id", ""))
    return msg


def _geo_point(payload: dict[str, Any] | None):
    point = payload or {}
    msg = GeoPoint()
    msg.latitude = float(point.get("latitude", 0.0))
    msg.longitude = float(point.get("longitude", 0.0))
    msg.altitude = float(point.get("altitude", 0.0))
    return msg


def _encounter(payload: dict[str, Any] | None):
    data = payload or {}
    msg = EncounterClassification()
    msg.schema_version = int(data.get("schema_version", 0))
    msg.stamp = _time(data.get("stamp"))
    msg.confidence = float(data.get("confidence", 0.0))
    msg.rationale = str(data.get("rationale", ""))
    msg.encounter_type = int(data.get("encounter_type", 0))
    msg.relative_bearing_deg = float(data.get("relative_bearing_deg", 0.0))
    msg.aspect_angle_deg = float(data.get("aspect_angle_deg", 0.0))
    msg.is_giveway = bool(data.get("is_giveway", False))
    return msg


def _tracked_target(payload: dict[str, Any]):
    msg = TrackedTarget()
    msg.schema_version = int(payload.get("schema_version", 0))
    msg.stamp = _time(payload.get("stamp"))
    msg.target_id = int(payload.get("target_id", 0))
    msg.position = _geo_point(payload.get("position"))
    msg.sog_kn = float(payload.get("sog_kn", 0.0))
    msg.cog_deg = float(payload.get("cog_deg", 0.0))
    msg.heading_deg = float(payload.get("heading_deg", 0.0))
    msg.covariance = list(payload.get("covariance", [0.0] * 9))
    msg.classification = str(payload.get("classification", ""))
    msg.classification_confidence = float(payload.get("classification_confidence", 0.0))
    msg.cpa_m = float(payload.get("cpa_m", 0.0))
    msg.tcpa_s = float(payload.get("tcpa_s", 0.0))
    msg.encounter = _encounter(payload.get("encounter"))
    msg.confidence = float(payload.get("confidence", 0.0))
    msg.rationale = str(payload.get("rationale", ""))
    msg.source_sensor = str(payload.get("source_sensor", ""))
    msg.cpa_covariance_m2 = float(payload.get("cpa_covariance_m2", 0.0))
    msg.tcpa_covariance_s2 = float(payload.get("tcpa_covariance_s2", 0.0))
    msg.intent_confidence = float(payload.get("intent_confidence", 0.0))
    msg.brg_deg = float(payload.get("brg_deg", 0.0))
    msg.rng_m = float(payload.get("rng_m", 0.0))
    return msg


def _tracked_target_array(payload: dict[str, Any]):
    msg = TrackedTargetArray()
    msg.schema_version = int(payload.get("schema_version", 0))
    msg.stamp = _time(payload.get("stamp"))
    msg.targets = [_tracked_target(target) for target in payload.get("targets", [])]
    msg.confidence = float(payload.get("confidence", 0.0))
    msg.rationale = str(payload.get("rationale", ""))
    return msg


def _ownship(payload: dict[str, Any]):
    msg = FilteredOwnShipState()
    msg.schema_version = int(payload.get("schema_version", 0))
    msg.stamp = _time(payload.get("stamp"))
    msg.position = _geo_point(payload.get("position"))
    msg.sog_kn = float(payload.get("sog_kn", 0.0))
    msg.cog_deg = float(payload.get("cog_deg", 0.0))
    msg.heading_deg = float(payload.get("heading_deg", 0.0))
    msg.u_water = float(payload.get("u_water", 0.0))
    msg.v_water = float(payload.get("v_water", 0.0))
    msg.r_dot_deg_s = float(payload.get("r_dot_deg_s", 0.0))
    msg.current_speed_kn = float(payload.get("current_speed_kn", 0.0))
    msg.current_direction_deg = float(payload.get("current_direction_deg", 0.0))
    msg.roll_deg = float(payload.get("roll_deg", 0.0))
    msg.pitch_deg = float(payload.get("pitch_deg", 0.0))
    msg.covariance = list(payload.get("covariance", [0.0] * 36))
    msg.nav_mode = str(payload.get("nav_mode", ""))
    msg.confidence = float(payload.get("confidence", 0.0))
    msg.rationale = str(payload.get("rationale", ""))
    return msg


def _environment(payload: dict[str, Any]):
    msg = EnvironmentState()
    msg.schema_version = int(payload.get("schema_version", 0))
    msg.stamp = _time(payload.get("stamp"))
    msg.wind_speed_kn = float(payload.get("wind_speed_kn", 0.0))
    msg.wind_direction_deg = float(payload.get("wind_direction_deg", 0.0))
    msg.wave_height_m = float(payload.get("wave_height_m", 0.0))
    msg.wave_direction_deg = float(payload.get("wave_direction_deg", 0.0))
    msg.current_speed_kn = float(payload.get("current_speed_kn", 0.0))
    msg.current_direction_deg = float(payload.get("current_direction_deg", 0.0))
    msg.visibility_range_nm = float(payload.get("visibility_range_nm", 0.0))
    msg.weather_source = str(payload.get("weather_source", ""))
    msg.confidence = float(payload.get("confidence", 0.0))
    msg.rationale = str(payload.get("rationale", ""))
    return msg


def _geo_pose_stamped(payload: dict[str, Any]):
    msg = GeoPoseStamped()
    msg.header = _header(payload.get("header"))
    pose = payload.get("pose", {})
    msg.pose.position = _geo_point(pose.get("position"))
    orientation = pose.get("orientation", {})
    msg.pose.orientation.x = float(orientation.get("x", 0.0))
    msg.pose.orientation.y = float(orientation.get("y", 0.0))
    msg.pose.orientation.z = float(orientation.get("z", 0.0))
    msg.pose.orientation.w = float(orientation.get("w", 1.0))
    return msg


def _geo_path(payload: dict[str, Any] | None):
    data = payload or {}
    msg = GeoPath()
    msg.header = _header(data.get("header"))
    msg.poses = [_geo_pose_stamped(pose) for pose in data.get("poses", [])]
    return msg


def _planned_route(payload: dict[str, Any]):
    msg = PlannedRoute()
    msg.schema_version = int(payload.get("schema_version", 0))
    msg.stamp = _time(payload.get("stamp"))
    msg.route_id = int(payload.get("route_id", 0))
    msg.route = _geo_path(payload.get("route"))
    msg.total_distance_nm = float(payload.get("total_distance_nm", 0.0))
    msg.estimated_duration_s = float(payload.get("estimated_duration_s", 0.0))
    msg.speed_profile_kn = list(payload.get("speed_profile_kn", []))
    msg.safety_zone = str(payload.get("safety_zone", ""))
    msg.confidence = float(payload.get("confidence", 0.0))
    msg.rationale = str(payload.get("rationale", ""))
    return msg


def main(args=None) -> None:
    if rclpy is None:
        raise RuntimeError("rclpy is required to run external_tdl_ingress")
    rclpy.init(args=args)
    node = ExternalTdlIngressNode()
    try:
        rclpy.spin(node)
    finally:
        node.destroy_node()
        rclpy.shutdown()
