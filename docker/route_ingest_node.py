#!/usr/bin/env python3
"""Route Ingest Node (D1.8) — L3-side consumer of L2 GncRoutePlan.

Subscribes /route_planning/gnc_route_plan, keeps the latest plan
(re-entrant: a newer stamp wholly replaces the old route), and forwards
it to the existing internal /l2/planned_route topic so the frontend route
layer renders it. This is the production seam for the real L2 link (later
merged into M3 Mission Manager).
"""
import math
import signal
import sys

import rclpy
from rclpy.node import Node
from rclpy.qos import (QoSProfile, QoSReliabilityPolicy,
                       QoSDurabilityPolicy, QoSHistoryPolicy)
from std_msgs.msg import Header
from geographic_msgs.msg import GeoPath, GeoPoseStamped, GeoPoint
from ship_interfaces.msg import GncRoutePlan
from l3_external_msgs.msg import PlannedRoute

_LATCHED = QoSProfile(
    reliability=QoSReliabilityPolicy.RELIABLE,
    durability=QoSDurabilityPolicy.TRANSIENT_LOCAL,
    history=QoSHistoryPolicy.KEEP_LAST, depth=5)

EARTH_RADIUS_NM = 3440.065
DEFAULT_SPEED_KN = 10.0


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


class RouteIngestNode(Node):
    def __init__(self):
        super().__init__("route_ingest_node")
        self._last_stamp_ns = -1
        self._route_id = 0
        self.create_subscription(
            GncRoutePlan, "/route_planning/gnc_route_plan",
            self._on_route, _LATCHED)
        self._pub = self.create_publisher(
            PlannedRoute, "/l2/planned_route", _LATCHED)
        self.get_logger().info("RouteIngestNode ready — subscribing GncRoutePlan")

    def _on_route(self, msg: GncRoutePlan):
        stamp_ns = msg.header.stamp.sec * 1_000_000_000 + msg.header.stamp.nanosec
        if stamp_ns < self._last_stamp_ns:
            return  # stale (older than current) — ignore
        replaced = self._last_stamp_ns >= 0 and stamp_ns > self._last_stamp_ns
        self._last_stamp_ns = stamp_ns
        n = msg.total_waypoints
        self.get_logger().info(
            f"{'REPLACED' if replaced else 'RECEIVED'} GncRoutePlan: "
            f"{n} waypoints (stamp={stamp_ns})")
        self._publish_internal(msg)

    def _publish_internal(self, msg: GncRoutePlan):
        lats = list(msg.latitude)
        lons = list(msg.longitude)
        n = len(lats)
        stamp = msg.header.stamp

        self._route_id += 1

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
