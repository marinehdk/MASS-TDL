#!/usr/bin/env python3
"""Route Ingest Node (D1.8) — L3-side consumer of L2 GncRoutePlan.

Subscribes /route_planning/gnc_route_plan, keeps the latest plan
(re-entrant: a newer stamp wholly replaces the old route), and forwards
it to the existing internal /l2/planned_route topic so the frontend route
layer renders it. This is the production seam for the real L2 link (later
merged into M3 Mission Manager).
"""
import math
import os
import signal
import sys
import hashlib

import rclpy
from rclpy.node import Node
from rclpy.qos import (QoSProfile, QoSReliabilityPolicy,
                       QoSDurabilityPolicy, QoSHistoryPolicy)
from std_msgs.msg import Header, String
from geographic_msgs.msg import GeoPath, GeoPoseStamped, GeoPoint
from ship_interfaces.msg import RoutePlan
from l3_external_msgs.msg import PlannedRoute
from sil_msgs.msg import LifecycleStatus

_LATCHED = QoSProfile(
    reliability=QoSReliabilityPolicy.RELIABLE,
    durability=QoSDurabilityPolicy.TRANSIENT_LOCAL,
    history=QoSHistoryPolicy.KEEP_LAST, depth=5)

EARTH_RADIUS_NM = 3440.065
DEFAULT_SPEED_KN = 10.0
LC_STATE_ACTIVE = 3
SCENARIO_DIR = os.environ.get("SIL_SCENARIO_DIR", "/var/sil/scenarios")


def _haversine_nm(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    dlat = math.radians(lat2 - lat1)
    dlon = math.radians(lon2 - lon1)
    a = (math.sin(dlat / 2) ** 2
         + math.cos(math.radians(lat1)) * math.cos(math.radians(lat2))
         * math.sin(dlon / 2) ** 2)
    return EARTH_RADIUS_NM * 2 * math.asin(math.sqrt(a))


def _bearing_between(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    dlon = math.radians(lon2 - lon1)
    y = math.sin(dlon) * math.cos(math.radians(lat2))
    x = (math.cos(math.radians(lat1)) * math.sin(math.radians(lat2))
         - math.sin(math.radians(lat1)) * math.cos(math.radians(lat2))
         * math.cos(dlon))
    return math.degrees(math.atan2(y, x))


def _make_geo_pose_stamped(lat: float, lon: float, stamp,
                            heading_deg: float = 0.0) -> GeoPoseStamped:
    gps = GeoPoseStamped()
    gps.header = Header(stamp=stamp, frame_id="WGS84")
    gps.pose.position = GeoPoint(latitude=lat, longitude=lon, altitude=0.0)
    heading_rad = math.radians(heading_deg)
    gps.pose.orientation.z = math.sin(heading_rad / 2)
    gps.pose.orientation.w = math.cos(heading_rad / 2)
    return gps


def _route_signature(lats: list[float], lons: list[float]) -> tuple[tuple[float, ...], tuple[float, ...]]:
    return (tuple(lats), tuple(lons))


def _stable_route_id(lats: list[float], lons: list[float]) -> int:
    payload = ";".join(
        f"{lat:.9f},{lon:.9f}" for lat, lon in zip(lats, lons))
    return int(hashlib.md5(payload.encode()).hexdigest()[:8], 16) or 1


class RouteIngestNode(Node):
    def __init__(self):
        super().__init__("route_ingest_node")
        self._last_stamp_ns = -1
        self._route_id = 0
        self._last_route_signature = None
        self._expected_route_signature = None
        self._current_scenario_id = ""
        self._scenario_dir = SCENARIO_DIR
        self.create_subscription(
            RoutePlan, "/route_planning/gnc_route_plan",
            self._on_route, _LATCHED)
        self.create_subscription(LifecycleStatus, "/sil/lifecycle_status",
                                 self._on_lifecycle, 10)
        self.create_subscription(String, "/sil/scenario_loaded",
                                 self._on_scenario_loaded, 10)
        self._pub = self.create_publisher(
            PlannedRoute, "/l2/planned_route", _LATCHED)
        self.get_logger().info("RouteIngestNode ready — subscribing GncRoutePlan")

    def _on_lifecycle(self, msg: LifecycleStatus):
        if msg.current_state != LC_STATE_ACTIVE:
            return
        sid = msg.scenario_id
        if sid and sid != self._current_scenario_id:
            self._load_expected_route(sid)

    def _on_scenario_loaded(self, msg: String):
        if msg.data and msg.data != self._current_scenario_id:
            self._load_expected_route(msg.data)

    def _load_expected_route(self, scenario_id: str):
        self._current_scenario_id = scenario_id
        yaml_path = None
        for root, _, files in os.walk(self._scenario_dir):
            if f"{scenario_id}.yaml" in files:
                yaml_path = os.path.join(root, f"{scenario_id}.yaml")
                break
        if not yaml_path:
            self._expected_route_signature = None
            self.get_logger().warn(
                f"YAML not found for {scenario_id}; accepting any GncRoutePlan")
            return
        try:
            import yaml
            with open(yaml_path) as f:
                scenario = yaml.safe_load(f)
            nominal = scenario.get("ownShip", {}).get("nominalRoute")
            if not nominal or len(nominal) < 2:
                self._expected_route_signature = None
                self.get_logger().info(
                    f"No nominalRoute for {scenario_id}; accepting any GncRoutePlan")
                return
            lats = [float(wp["latitude"]) for wp in nominal]
            lons = [float(wp["longitude"]) for wp in nominal]
            self._expected_route_signature = _route_signature(lats, lons)
            self.get_logger().info(
                f"RouteIngest expected route set: scenario={scenario_id} "
                f"waypoints={len(lats)}")
        except Exception as exc:
            self._expected_route_signature = None
            self.get_logger().warn(
                f"Could not load expected route for {scenario_id}: {exc}; "
                "accepting any GncRoutePlan")

    def _on_route(self, msg: RoutePlan):
        lats = list(msg.latitude)
        lons = list(msg.longitude)
        route_signature = _route_signature(lats, lons)
        if (self._expected_route_signature is not None
                and route_signature != self._expected_route_signature):
            self.get_logger().warn(
                f"Ignoring RoutePlan not matching active scenario "
                f"{self._current_scenario_id}: got {len(lats)} waypoints")
            return

        stamp_ns = msg.header.stamp.sec * 1_000_000_000 + msg.header.stamp.nanosec
        if stamp_ns < self._last_stamp_ns:
            return  # stale (older than current) — ignore
        replaced = self._last_stamp_ns >= 0 and stamp_ns > self._last_stamp_ns
        self._last_stamp_ns = stamp_ns
        n = len(msg.latitude)
        self.get_logger().info(
            f"{'REPLACED' if replaced else 'RECEIVED'} RoutePlan: "
            f"{n} waypoints (stamp={stamp_ns})")
        self._publish_internal(msg)

    def _publish_internal(self, msg: RoutePlan):
        lats = list(msg.latitude)
        lons = list(msg.longitude)
        n = len(lats)
        stamp = msg.header.stamp

        route_signature = _route_signature(lats, lons)
        if route_signature != self._last_route_signature:
            self._route_id = _stable_route_id(lats, lons)
            self._last_route_signature = route_signature

        # Build GeoPath with GeoPoseStamped poses (mirrors mock_l2_publisher
        # _publish_planned_route: one pose per waypoint, heading toward next wp)
        path = GeoPath()
        path.header = Header(stamp=stamp, frame_id="WGS84")
        for i in range(n):
            heading = 0.0
            if i < n - 1:
                heading = _bearing_between(lats[i], lons[i], lats[i + 1], lons[i + 1])
            gps = _make_geo_pose_stamped(lats[i], lons[i], stamp, heading)
            path.poses.append(gps)

        # Compute total distance and estimated duration
        total_nm = 0.0
        for i in range(n - 1):
            total_nm += _haversine_nm(lats[i], lons[i], lats[i + 1], lons[i + 1])
        est_duration_s = (total_nm / DEFAULT_SPEED_KN * 3600.0) if total_nm > 0 else 0.0

        # speed_profile_kn: one entry per segment (n-1 segments), constant default
        speed_profile = [DEFAULT_SPEED_KN] * max(n - 1, 1)

        out = PlannedRoute()
        out.schema_version = 112
        out.stamp = stamp
        out.route_id = self._route_id
        out.route = path
        out.total_distance_nm = total_nm
        out.estimated_duration_s = est_duration_s
        out.speed_profile_kn = speed_profile
        out.safety_zone = "500m_cpa_corridor"
        out.confidence = 1.0
        out.rationale = f"D1.8_INGEST: route_id={self._route_id} {n}wp from GncRoutePlan"

        self._pub.publish(out)


def main(args=None):
    rclpy.init(args=args)
    node = RouteIngestNode()
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
