#!/usr/bin/env python3
"""SIL Mock L2 Publisher — unblocks M3 AWAITING_ROUTE for DEMO-1.

Publishes three topics that real L1/L2 would provide:
  /l1/voyage_task    (TRANSIENT_LOCAL, ~0.5 Hz)
  /l2/planned_route  (RELIABLE + TRANSIENT_LOCAL, 1 Hz)
  /l2/speed_profile  (RELIABLE + TRANSIENT_LOCAL, 1 Hz)

Route source:
  1. Scenario YAML ``ownShip.nominalRoute`` (v3.1 schema extension)
  2. Fallback: straight-line projection from current ownship position

Lifecycle:
  Primary: subscribes to /sil/lifecycle_status (sil_msgs/LifecycleStatus)
           which carries scenario_id when state=ACTIVE (3).
  Fallback: subscribes to /sil/scenario_loaded (std_msgs/String).
  Reads the corresponding YAML from /var/sil/scenarios/, extracts
  nominalRoute (or generates default), then starts publishing.

Ownship tracking:
  Subscribes to /sil/own_ship_state (sil_msgs/OwnShipState) at 50 Hz.
  Uses current position as VoyageTask departure (M3 validates departure
  is within 2 km of ownship).  Route waypoints are offset from the
  ownship position using relative offsets from the YAML nominalRoute.
"""

import hashlib
import math
import os
import signal
import sys
import time

import rclpy
from rclpy.node import Node
from rclpy.qos import (
    QoSProfile,
    QoSReliabilityPolicy,
    QoSDurabilityPolicy,
    QoSHistoryPolicy,
)
from std_msgs.msg import String, Header
from builtin_interfaces.msg import Time as BuiltinTime
from geographic_msgs.msg import GeoPoint, GeoPose, GeoPoseStamped, GeoPath
from sil_msgs.msg import LifecycleStatus, OwnShipState

from l3_external_msgs.msg import (
    VoyageTask,
    PlannedRoute,
    SpeedProfile,
    TimeWindow,
    ReplanResponse,
)
from l3_msgs.msg import RouteReplanRequest

SCENARIO_DIR = os.environ.get("SIL_SCENARIO_DIR", "/var/sil/scenarios")
EARTH_RADIUS_NM = 3440.065
EARTH_RADIUS_KM = 6371.0
DEFAULT_ROUTE_DISTANCE_NM = 10.0
DEFAULT_TRANSIT_SPEED_KN = 10.0
MIN_STEERAGE_SPEED_KN = 2.0
SPEED_UPPER_FACTOR = 1.2
PUBLISH_HZ = 1.0
VOYAGE_TASK_HZ = 0.5
LC_STATE_ACTIVE = 3


def _now(node: Node) -> BuiltinTime:
    t = node.get_clock().now()
    return BuiltinTime(sec=t.seconds_nanoseconds()[0], nanosec=t.seconds_nanoseconds()[1])


def _haversine_nm(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    dlat = math.radians(lat2 - lat1)
    dlon = math.radians(lon2 - lon1)
    a = (math.sin(dlat / 2) ** 2
         + math.cos(math.radians(lat1)) * math.cos(math.radians(lat2))
         * math.sin(dlon / 2) ** 2)
    return EARTH_RADIUS_NM * 2 * math.asin(math.sqrt(a))


def _haversine_km(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    dlat = math.radians(lat2 - lat1)
    dlon = math.radians(lon2 - lon1)
    a = (math.sin(dlat / 2) ** 2
         + math.cos(math.radians(lat1)) * math.cos(math.radians(lat2))
         * math.sin(dlon / 2) ** 2)
    return EARTH_RADIUS_KM * 2 * math.asin(math.sqrt(a))


def _project_point(lat: float, lon: float, bearing_deg: float,
                   distance_nm: float) -> tuple:
    bearing = math.radians(bearing_deg)
    d = distance_nm / EARTH_RADIUS_NM
    lat_r = math.radians(lat)
    lon_r = math.radians(lon)
    lat2 = math.asin(
        math.sin(lat_r) * math.cos(d)
        + math.cos(lat_r) * math.sin(d) * math.cos(bearing))
    lon2 = lon_r + math.atan2(
        math.sin(bearing) * math.sin(d) * math.cos(lat_r),
        math.cos(d) - math.sin(lat_r) * math.sin(lat2))
    return math.degrees(lat2), math.degrees(lon2)


def _make_geo_point(lat: float, lon: float, alt: float = 0.0) -> GeoPoint:
    p = GeoPoint()
    p.latitude = lat
    p.longitude = lon
    p.altitude = alt
    return p


def _make_geo_pose_stamped(lat: float, lon: float, alt: float = 0.0,
                           heading_deg: float = 0.0) -> GeoPoseStamped:
    gps = GeoPoseStamped()
    gps.pose.position = _make_geo_point(lat, lon, alt)
    heading_rad = math.radians(heading_deg)
    gps.pose.orientation.z = math.sin(heading_rad / 2)
    gps.pose.orientation.w = math.cos(heading_rad / 2)
    return gps


def _bearing_between(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    dlon = math.radians(lon2 - lon1)
    y = math.sin(dlon) * math.cos(math.radians(lat2))
    x = (math.cos(math.radians(lat1)) * math.sin(math.radians(lat2))
         - math.sin(math.radians(lat1)) * math.cos(math.radians(lat2))
         * math.cos(dlon))
    return math.degrees(math.atan2(y, x))


class MockL2Publisher(Node):
    def __init__(self):
        super().__init__("mock_l2_publisher")

        self.declare_parameter("scenario_dir", SCENARIO_DIR)
        self.declare_parameter("default_route_distance_nm", DEFAULT_ROUTE_DISTANCE_NM)
        self.declare_parameter("default_transit_speed_kn", DEFAULT_TRANSIT_SPEED_KN)

        self._scenario_dir = (self.get_parameter("scenario_dir")
                              .get_parameter_value().string_value)
        self._default_dist = (self.get_parameter("default_route_distance_nm")
                              .get_parameter_value().double_value)
        self._default_speed = (self.get_parameter("default_transit_speed_kn")
                               .get_parameter_value().double_value)

        # Load mock_l2 config from scenario YAML via SIL_SCENARIO_YAML env var
        self._mock_l2_config = {}
        self._load_mock_l2_config()

        self._yaml_waypoints = []
        self._yaml_speeds_kn = []
        self._route_source = "none"
        self._is_active = False
        self._route_id = 0
        self._task_id = 0
        self._ownship_initial = {}
        self._current_scenario_id = ""

        self._ownship_lat = 0.0
        self._ownship_lon = 0.0
        self._ownship_heading = 0.0
        self._ownship_sog = 0.0
        self._ownship_received = False

        tl_qos = QoSProfile(
            reliability=QoSReliabilityPolicy.RELIABLE,
            durability=QoSDurabilityPolicy.TRANSIENT_LOCAL,
            history=QoSHistoryPolicy.KEEP_LAST,
            depth=50,
        )
        route_qos = QoSProfile(
            reliability=QoSReliabilityPolicy.RELIABLE,
            durability=QoSDurabilityPolicy.TRANSIENT_LOCAL,
            history=QoSHistoryPolicy.KEEP_LAST,
            depth=5,
        )
        sensor_qos = QoSProfile(
            reliability=QoSReliabilityPolicy.BEST_EFFORT,
            durability=QoSDurabilityPolicy.VOLATILE,
            history=QoSHistoryPolicy.KEEP_LAST,
            depth=5,
        )

        self._pub_voyage_task = self.create_publisher(
            VoyageTask, "/l1/voyage_task", tl_qos)
        self._pub_planned_route = self.create_publisher(
            PlannedRoute, "/l2/planned_route", route_qos)
        self._pub_speed_profile = self.create_publisher(
            SpeedProfile, "/l2/speed_profile", route_qos)
        # F1a: Replan response publisher — unblocks M3 from REPLAN_WAIT.
        # M3 sends RouteReplanRequest when M7 alerts MRC_REQUIRED; without
        # a response within RFC-006 §2.3 SLA (30 s for MRC_REQUIRED), M3
        # never publishes mission_state/mission_goal → M4 stays in fallback.
        self._pub_replan_response = self.create_publisher(
            ReplanResponse, "/l2/replan_response", tl_qos)

        self._sub_ownship = self.create_subscription(
            OwnShipState, "/sil/own_ship_state",
            self._on_own_ship_state, sensor_qos)
        self._sub_lifecycle = self.create_subscription(
            LifecycleStatus, "/sil/lifecycle_status",
            self._on_lifecycle_status, 10)
        self._sub_scenario = self.create_subscription(
            String, "/sil/scenario_loaded",
            self._on_scenario_loaded, 10)
        # F1a: Subscribe to M3's replan request and respond with SUCCESS.
        # QoS: RELIABLE keep_last(10) — match M3's publisher contract.
        replan_sub_qos = QoSProfile(
            reliability=QoSReliabilityPolicy.RELIABLE,
            durability=QoSDurabilityPolicy.VOLATILE,
            history=QoSHistoryPolicy.KEEP_LAST,
            depth=10,
        )
        self._sub_replan_request = self.create_subscription(
            RouteReplanRequest, "/l3/m3/route_replan_request",
            self._on_replan_request, replan_sub_qos)
        self._replan_response_count = 0

        self._route_timer = self.create_timer(1.0 / PUBLISH_HZ, self._on_route_timer)
        self._vt_timer = self.create_timer(1.0 / VOYAGE_TASK_HZ, self._on_vt_timer)

        self.get_logger().info(
            f"MockL2Publisher ready — watching /sil/lifecycle_status "
            f"+ /sil/own_ship_state (dir={self._scenario_dir})")

    def _load_mock_l2_config(self):
        """Load mock_l2 config section from scenario YAML via SIL_SCENARIO_YAML env var."""
        scenario_yaml_path = os.environ.get('SIL_SCENARIO_YAML', '')
        if not scenario_yaml_path or not os.path.exists(scenario_yaml_path):
            self.get_logger().info("No SIL_SCENARIO_YAML env var set; using default route config")
            return

        try:
            import yaml
            with open(scenario_yaml_path, 'r') as f:
                scenario_data = yaml.safe_load(f)
            self._mock_l2_config = scenario_data.get('mock_l2', {})
            self.get_logger().info(
                f"Loaded mock_l2 config from {scenario_yaml_path}: "
                f"autonomy_level={self._mock_l2_config.get('voyage_task', {}).get('autonomy_level', 'N/A')}, "
                f"mission_id={self._mock_l2_config.get('voyage_task', {}).get('mission_id', 'N/A')}")
        except Exception as exc:  # noqa: BLE001
            self.get_logger().warn(
                f"Failed to load mock_l2 config from {scenario_yaml_path}: {exc}")

    def _on_own_ship_state(self, msg: OwnShipState):
        self._ownship_lat = msg.lat
        self._ownship_lon = msg.lon
        self._ownship_heading = msg.heading
        self._ownship_sog = msg.sog
        if not self._ownship_received:
            self._ownship_received = True
            self.get_logger().info(
                f"OwnShip position: ({self._ownship_lat:.4f}, "
                f"{self._ownship_lon:.4f}) heading={self._ownship_heading:.1f}")

    def _on_lifecycle_status(self, msg: LifecycleStatus):
        if msg.current_state != LC_STATE_ACTIVE:
            if self._is_active:
                self.get_logger().info(
                    f"Lifecycle state={msg.current_state} (not ACTIVE) — deactivating")
                self._is_active = False
            return

        sid = msg.scenario_id
        if sid and sid == self._current_scenario_id and self._is_active:
            return

        if sid:
            self.get_logger().info(
                f"Lifecycle ACTIVE — scenario_id={sid}")
            self._current_scenario_id = sid
            self._load_scenario(sid)
        elif not self._is_active:
            self.get_logger().info(
                "Lifecycle ACTIVE (no scenario_id) — auto-detecting scenario")
            self._auto_detect_scenario()

    def _on_scenario_loaded(self, msg: String):
        sid = msg.data
        if sid == self._current_scenario_id and self._is_active:
            return
        self.get_logger().info(f"Scenario loaded event: {sid}")
        self._current_scenario_id = sid
        self._load_scenario(sid)

    def _on_replan_request(self, msg: RouteReplanRequest):
        """F1a: Respond to M3 RouteReplanRequest with SUCCESS.

        Per RFC-006 §2.3 SLA targets:
          REASON_MRC_REQUIRED      = 30 s
          REASON_ODD_EXIT          = 60 s
          REASON_MISSION_INFEASIBLE = 120 s
          REASON_CONGESTION        = 300 s

        Mock policy: always SUCCESS within <100 ms. Real L2 would invoke
        the planner and may return FAILED_* per RFC-006 enum. For DEMO-1
        we acknowledge the request and re-emit the existing route which
        is sufficient to advance M3 from REPLAN_WAIT → ACTIVE.
        """
        reason_str = {
            RouteReplanRequest.REASON_ODD_EXIT: "ODD_EXIT",
            RouteReplanRequest.REASON_MISSION_INFEASIBLE: "MISSION_INFEASIBLE",
            RouteReplanRequest.REASON_MRC_REQUIRED: "MRC_REQUIRED",
            RouteReplanRequest.REASON_CONGESTION: "CONGESTION",
        }.get(msg.reason, f"UNKNOWN({msg.reason})")

        resp = ReplanResponse()
        resp.schema_version = 112
        resp.stamp = _now(self)
        resp.status = ReplanResponse.STATUS_SUCCESS
        resp.failure_reason = ""
        resp.retry_recommended = False
        resp.confidence = 1.0
        resp.rationale = (
            f"SIL_MOCK: ack replan reason={reason_str} "
            f"deadline={msg.deadline_s:.1f}s — route unchanged, M3 may resume"
        )
        self._pub_replan_response.publish(resp)
        self._replan_response_count += 1

        # Also force-republish the current route so M3 sees fresh L2 data
        # on the next planned_route callback. This avoids any stale-route
        # rejection on M3's side.
        if self._is_active:
            try:
                self._on_route_timer()
            except Exception as exc:  # noqa: BLE001
                self.get_logger().warn(f"Route refresh after replan failed: {exc}")

        if self._replan_response_count <= 3 or self._replan_response_count % 20 == 0:
            self.get_logger().info(
                f"Replan response #{self._replan_response_count} "
                f"sent (reason={reason_str}, status=SUCCESS)")

    def _auto_detect_scenario(self):
        import glob as _glob
        yaml_files = sorted(_glob.glob(os.path.join(self._scenario_dir, "**/*.yaml"), recursive=True))
        if not yaml_files:
            self.get_logger().warn(
                f"No YAML files in {self._scenario_dir} — using default route")
            self._ownship_initial = {}
            self._generate_default_route()
            return

        first_yaml = os.path.basename(yaml_files[0])
        scenario_id = os.path.splitext(first_yaml)[0]
        self.get_logger().info(
            f"Auto-detected scenario: {scenario_id} "
            f"(first of {len(yaml_files)} YAML files)")
        self._current_scenario_id = scenario_id
        self._load_scenario(scenario_id)

    def _load_scenario(self, scenario_id: str):
        self._ownship_received = False
        self._ownship_lat = 0.0
        self._ownship_lon = 0.0
        yaml_path = None
        for root, _, files in os.walk(self._scenario_dir):
            if f"{scenario_id}.yaml" in files:
                yaml_path = os.path.join(root, f"{scenario_id}.yaml")
                break

        if not yaml_path or not os.path.exists(yaml_path):
            self.get_logger().warn(
                f"YAML not found for {scenario_id} in {self._scenario_dir} — using default route")
            self._ownship_initial = {}
            self._generate_default_route()
            return

        try:
            import yaml
            with open(yaml_path, "r") as f:
                scenario = yaml.safe_load(f)
        except Exception as exc:
            self.get_logger().error(f"Failed to parse YAML {yaml_path}: {exc}")
            self._ownship_initial = {}
            self._generate_default_route()
            return

        own = scenario.get("ownShip", {})
        initial = own.get("initial", {})
        pos = initial.get("position", {})
        self._ownship_initial = {
            "latitude": pos.get("latitude", 0.0),
            "longitude": pos.get("longitude", 0.0),
            "heading": initial.get("heading", 0.0),
            "sog": initial.get("sog", self._default_speed),
        }

        nominal = own.get("nominalRoute")
        if nominal and len(nominal) >= 2:
            self._yaml_waypoints = []
            self._yaml_speeds_kn = []
            for wp in nominal:
                self._yaml_waypoints.append((
                    float(wp["latitude"]),
                    float(wp["longitude"]),
                ))
                self._yaml_speeds_kn.append(
                    float(wp.get("target_sog_kn", self._default_speed)))
            self._route_source = "YAML nominalRoute"
            self.get_logger().info(
                f"Route from YAML: {len(self._yaml_waypoints)} waypoints")
        else:
            self._generate_default_route()
            return

        self._activate()

    def _generate_default_route(self):
        init = self._ownship_initial
        lat = init.get("latitude", 63.44)
        lon = init.get("longitude", 10.38)
        heading = init.get("heading", 0.0)
        sog = init.get("sog", self._default_speed)

        lat2, lon2 = _project_point(lat, lon, heading, self._default_dist)
        self._yaml_waypoints = [(lat, lon), (lat2, lon2)]
        self._yaml_speeds_kn = [sog, sog]
        self._route_source = "default_generation"
        self.get_logger().info(
            f"Default route: ({lat:.4f},{lon:.4f}) -> ({lat2:.4f},{lon2:.4f}) "
            f"bearing={heading:.1f} deg dist={self._default_dist:.1f}nm")
        self._activate()

    def _activate(self):
        self._route_id += 1
        self._task_id = int(hashlib.md5(
            str(time.time()).encode()).hexdigest()[:8], 16)
        self._is_active = True
        self.get_logger().info(
            f"Mock L2 ACTIVE — route_id={self._route_id}, "
            f"task_id={self._task_id}, source={self._route_source}")

    def _get_effective_waypoints(self):
        if not self._ownship_received or not self._yaml_waypoints:
            return self._yaml_waypoints, self._yaml_speeds_kn

        if len(self._yaml_waypoints) < 2:
            return self._yaml_waypoints, self._yaml_speeds_kn

        # Bind departure to current ownship lat/lon to pass M3 departure check (<2km)
        shifted = [(self._ownship_lat, self._ownship_lon)]
        # Keep all subsequent nominal waypoints absolutely static in GEO WGS84
        shifted.extend(self._yaml_waypoints[1:])
        return shifted, self._yaml_speeds_kn

    def _on_route_timer(self):
        if not self._is_active:
            return
        self._publish_planned_route()
        self._publish_speed_profile()

    def _on_vt_timer(self):
        if not self._is_active:
            return
        self._publish_voyage_task()

    def _publish_voyage_task(self):
        msg = VoyageTask()
        msg.schema_version = 112
        msg.stamp = _now(self)
        msg.task_id = self._task_id

        # Extract config values if present
        task_cfg = self._mock_l2_config.get('voyage_task', {})
        autonomy_level = task_cfg.get('autonomy_level', 'D3_SUPERVISED')
        mission_id = task_cfg.get('mission_id', f'mock-mission-{self._task_id}')

        waypoints, speeds = self._get_effective_waypoints()

        if self._ownship_received:
            msg.departure = _make_geo_point(
                self._ownship_lat, self._ownship_lon)
        elif waypoints:
            msg.departure = _make_geo_point(waypoints[0][0], waypoints[0][1])
        else:
            msg.departure = _make_geo_point(0.0, 0.0)

        if waypoints:
            msg.destination = _make_geo_point(
                waypoints[-1][0], waypoints[-1][1])
        else:
            msg.destination = _make_geo_point(0.0, 0.0)

        total_nm = self._compute_total_distance(waypoints)
        avg_speed = (speeds[0] if speeds else self._default_speed)
        est_duration = (total_nm / avg_speed * 3600) if avg_speed > 0 else 3600

        now_sec = msg.stamp.sec
        msg.eta_window.schema_version = 112
        msg.eta_window.stamp = msg.stamp
        msg.eta_window.earliest = BuiltinTime(sec=now_sec + int(est_duration))
        msg.eta_window.latest = BuiltinTime(
            sec=now_sec + int(est_duration) + 60)
        msg.eta_window.confidence = 1.0
        msg.eta_window.rationale = "SIL_MOCK: synthetic ETA window"

        msg.optimization_priority = "balanced"
        msg.mandatory_waypoints = [
            _make_geo_point(wp[0], wp[1]) for wp in waypoints[1:]
        ]
        msg.exclusion_zones = []
        msg.special_restrictions = ""
        msg.confidence = 1.0
        msg.rationale = (
            f"SIL_MOCK: voyage task {mission_id} (autonomy={autonomy_level}) "
            f"from {self._route_source}"
        )

        self._pub_voyage_task.publish(msg)

    def _publish_planned_route(self):
        msg = PlannedRoute()
        msg.schema_version = 112
        msg.stamp = _now(self)
        msg.route_id = self._route_id

        # Use mock_l2 config route if available, otherwise fall back to loaded YAML
        route_cfg = self._mock_l2_config.get('planned_route', {})
        if route_cfg.get('waypoints'):
            # Override with config waypoints
            config_waypoints = route_cfg.get('waypoints', [])
            self._yaml_waypoints = [(wp.get('latitude'), wp.get('longitude'))
                                     for wp in config_waypoints]
            config_speeds = [route_cfg.get('cruise_speed_kn', self._default_speed)] * len(self._yaml_waypoints)
            self._yaml_speeds_kn = config_speeds
            self._route_source = f"mock_l2 config ({len(self._yaml_waypoints)} waypoints)"

        waypoints, speeds = self._yaml_waypoints, self._yaml_speeds_kn

        path = GeoPath()
        path.header = Header(stamp=msg.stamp, frame_id="WGS84")
        for i, (lat, lon) in enumerate(waypoints):
            heading = 0.0
            if i < len(waypoints) - 1:
                nlat, nlon = waypoints[i + 1]
                heading = _bearing_between(lat, lon, nlat, nlon)
            gps = _make_geo_pose_stamped(lat, lon, 0.0, heading)
            gps.header = Header(stamp=msg.stamp, frame_id="WGS84")
            path.poses.append(gps)
        msg.route = path

        total_nm = self._compute_total_distance(waypoints)
        msg.total_distance_nm = total_nm

        avg_speed = (speeds[0] if speeds else self._default_speed)
        msg.estimated_duration_s = (total_nm / avg_speed * 3600) if avg_speed > 0 else 3600

        msg.speed_profile_kn = [float(s) for s in speeds]
        msg.safety_zone = "500m_cpa_corridor"
        msg.confidence = 1.0
        msg.rationale = f"SIL_MOCK: {self._route_source}"

        self._pub_planned_route.publish(msg)

    def _publish_speed_profile(self):
        msg = SpeedProfile()
        msg.schema_version = 112
        msg.stamp = _now(self)
        msg.profile_id = self._route_id

        waypoints, speeds = self._yaml_waypoints, self._yaml_speeds_kn
        n_segments = max(len(waypoints) - 1, 1)
        total_nm = self._compute_total_distance(waypoints)
        segment_nm = total_nm / n_segments if n_segments > 0 else 0
        segment_m = segment_nm * 1852.0

        cum_m = 0.0
        for i in range(n_segments):
            msg.segment_start_distances_m.append(cum_m)
            cum_m += segment_m
            msg.segment_end_distances_m.append(cum_m)

            speed = (speeds[i]
                     if i < len(speeds)
                     else self._default_speed)
            msg.target_speeds_kn.append(float(speed))
            msg.max_speeds_kn.append(float(speed * SPEED_UPPER_FACTOR))
            msg.min_speeds_kn.append(float(MIN_STEERAGE_SPEED_KN))
            msg.segment_types.append("transit")

        msg.confidence = 1.0
        msg.rationale = f"SIL_MOCK: {self._route_source}"

        self._pub_speed_profile.publish(msg)

    def _compute_total_distance(self, waypoints) -> float:
        if len(waypoints) < 2:
            return 0.0
        total = 0.0
        for i in range(len(waypoints) - 1):
            lat1, lon1 = waypoints[i]
            lat2, lon2 = waypoints[i + 1]
            total += _haversine_nm(lat1, lon1, lat2, lon2)
        return total


def main(args=None):
    rclpy.init(args=args)
    node = MockL2Publisher()

    def _sig_handler(sig, frame):
        node.get_logger().info("MockL2Publisher shutting down")
        node.destroy_node()
        rclpy.shutdown()
        sys.exit(0)

    signal.signal(signal.SIGINT, _sig_handler)
    signal.signal(signal.SIGTERM, _sig_handler)

    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
