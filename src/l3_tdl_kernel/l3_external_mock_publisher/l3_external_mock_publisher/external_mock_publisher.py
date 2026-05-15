#!/usr/bin/env python3
"""External mock publisher node for DEMO-1 Stream B.

Publishes mock messages on all 11 cross-layer topics consumed by L3 TDL,
with scenario-driven data loaded from maritime-schema v2.0 YAML files.

Usage:
    ros2 run l3_external_mock_publisher external_mock_publisher
    ros2 run l3_external_mock_publisher external_mock_publisher \
        --ros-args -p scenario_path:=/path/to/scenario.yaml
"""

import rclpy
import yaml
from builtin_interfaces.msg import Time
from geographic_msgs.msg import GeoPath, GeoPoint, GeoPose, GeoPoseStamped
from geometry_msgs.msg import Quaternion
from l3_external_msgs.msg import (
    CheckerVetoNotification,
    EmergencyCommand,
    EnvironmentState,
    FilteredOwnShipState,
    OverrideActiveSignal,
    PlannedRoute,
    ReflexActivationNotification,
    ReplanResponse,
    SpeedProfile,
    TimeWindow,
    TrackedTargetArray,
    VoyageTask,
)
from l3_msgs.msg import EncounterClassification, TrackedTarget
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy
from std_msgs.msg import Header


class ExternalMockPublisher(Node):
    """Publishes mock external messages on all 11 cross-layer topics.

    Frequencies align with v1.1.2 §15.2 interface matrix:
    - 50 Hz (0.02 s):   FilteredOwnShipState              → /fusion/own_ship_state
    - 2 Hz  (0.5 s):    TrackedTargetArray                 → /fusion/tracked_targets
                        EnvironmentState                   → /fusion/environment_state
                        PlannedRoute                       → /l2/planned_route
                        SpeedProfile                       → /l2/speed_profile
    - 1 Hz  (1.0 s):    VoyageTask                         → /l1/voyage_task
    - 0.2 Hz (5 s):     ReplanResponse                     → /l2/replan_response
    - 0.033 Hz (30 s):  CheckerVetoNotification            → /checker/veto_notification
                        ReflexActivationNotification        → /reflex/activation_notification
                        OverrideActiveSignal                → /override/active_signal
                        EmergencyCommand                    → /reflex/emergency_command
    """

    def __init__(self) -> None:
        super().__init__("l3_external_mock_publisher")

        self._counter: int = 0
        self._scenario: dict | None = None

        # QoS profiles
        qos_best_effort = QoSProfile(
            depth=10, reliability=ReliabilityPolicy.BEST_EFFORT
        )
        qos_reliable = QoSProfile(
            depth=10, reliability=ReliabilityPolicy.RELIABLE
        )

        # --- 50 Hz: BEST_EFFORT ---
        self._pub_ownship = self.create_publisher(
            FilteredOwnShipState, "/fusion/own_ship_state", qos_best_effort
        )

        # --- 2 Hz: RELIABLE ---
        self._pub_targets = self.create_publisher(
            TrackedTargetArray, "/fusion/tracked_targets", qos_reliable
        )
        self._pub_env = self.create_publisher(
            EnvironmentState, "/fusion/environment_state", qos_reliable
        )
        self._pub_route = self.create_publisher(
            PlannedRoute, "/l2/planned_route", qos_reliable
        )
        self._pub_speed = self.create_publisher(
            SpeedProfile, "/l2/speed_profile", qos_reliable
        )

        # --- 1 Hz: RELIABLE ---
        self._pub_voyage = self.create_publisher(
            VoyageTask, "/l1/voyage_task", qos_reliable
        )

        # --- Event-driven / slow: RELIABLE ---
        self._pub_replan = self.create_publisher(
            ReplanResponse, "/l2/replan_response", qos_reliable
        )
        self._pub_veto = self.create_publisher(
            CheckerVetoNotification, "/checker/veto_notification", qos_reliable
        )
        self._pub_reflex = self.create_publisher(
            ReflexActivationNotification,
            "/reflex/activation_notification",
            qos_reliable,
        )
        self._pub_override = self.create_publisher(
            OverrideActiveSignal, "/override/active_signal", qos_reliable
        )
        self._pub_emergency = self.create_publisher(
            EmergencyCommand, "/reflex/emergency_command", qos_reliable
        )

        # --- Timers ---
        self._timer_50hz = self.create_timer(0.02, self._publish_50hz)
        self._timer_2hz = self.create_timer(0.5, self._publish_2hz)
        self._timer_1hz = self.create_timer(1.0, self._publish_1hz)
        self._timer_5s = self.create_timer(5.0, self._publish_5s)
        self._timer_30s = self.create_timer(30.0, self._publish_30s)

    # ------------------------------------------------------------------
    # Scenario loading (maritime-schema v2.0 YAML)
    # ------------------------------------------------------------------
    def load_scenario(self, scenario_path: str) -> None:
        """Load a maritime-schema v2.0 YAML scenario file.

        Extracts own_ship initial position/COG/SOG/heading and target_ships list
        for use by the publish callbacks.
        """
        with open(scenario_path, "r") as f:
            data = yaml.safe_load(f)

        self._scenario = data
        own = data.get("own_ship", {})
        initial = own.get("initial", {})
        pos = initial.get("position", {})
        self._own_lat = float(pos.get("latitude", 63.435))
        self._own_lon = float(pos.get("longitude", 10.395))
        self._own_cog = float(initial.get("cog", 0.0))
        self._own_sog = float(initial.get("sog", 18.0))
        self._own_heading = float(initial.get("heading", 0.0))

        self._targets = data.get("target_ships", [])
        self.get_logger().info(
            f"Loaded scenario: {len(self._targets)} target(s), "
            f"own ship @ ({self._own_lat:.4f}, {self._own_lon:.4f}) "
            f"COG={self._own_cog}° SOG={self._own_sog}kn"
        )

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------
    def _make_stamp(self) -> Time:
        """Create a timestamp from the current ROS clock."""
        return self.get_clock().now().to_msg()

    # ------------------------------------------------------------------
    # 50 Hz: FilteredOwnShipState (§15.2)
    # ------------------------------------------------------------------
    def _publish_own_ship_state(self, stamp: Time) -> None:
        """Publish mock own-ship state with scenario data or Trondheim Fjord default."""
        msg = FilteredOwnShipState()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp

        if self._scenario is not None:
            msg.position = GeoPoint(
                latitude=self._own_lat,
                longitude=self._own_lon,
                altitude=0.0,
            )
            msg.cog_deg = self._own_cog
            msg.sog_kn = self._own_sog
            msg.heading_deg = self._own_heading
        else:
            # Default: Trondheim Fjord
            msg.position = GeoPoint(latitude=63.435, longitude=10.395, altitude=0.0)
            msg.cog_deg = 0.0
            msg.sog_kn = 18.0
            msg.heading_deg = 0.0

        msg.u_water = msg.sog_kn * 0.514444  # kn → m/s
        msg.v_water = 0.0
        msg.r_dot_deg_s = 0.0
        msg.current_speed_kn = 0.5
        msg.current_direction_deg = 90.0
        msg.roll_deg = 0.0
        msg.pitch_deg = 0.0
        msg.covariance = [0.0] * 36
        msg.nav_mode = "OPTIMAL"
        msg.confidence = 0.95
        msg.rationale = (
            "Mock own ship state from scenario"
            if self._scenario is not None
            else "Mock own ship state (Trondheim Fjord default)"
        )
        self._pub_ownship.publish(msg)

    # ------------------------------------------------------------------
    # 2 Hz: TrackedTargetArray + EnvironmentState + PlannedRoute + SpeedProfile
    # ------------------------------------------------------------------
    def _publish_tracked_targets(self, stamp: Time) -> None:
        """Publish tracked targets from scenario YAML target_ships, or a default."""
        msg = TrackedTargetArray()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp

        targets: list[TrackedTarget] = []

        if self._scenario is not None and self._targets:
            for ship in self._targets:
                init = ship.get("initial", {})
                pos = init.get("position", {})
                t = TrackedTarget()
                t.schema_version = "v1.1.2"
                t.stamp = stamp
                t.target_id = int(ship.get("mmsi", 0))
                t.position = GeoPoint(
                    latitude=float(pos.get("latitude", 63.5)),
                    longitude=float(pos.get("longitude", 10.4)),
                    altitude=0.0,
                )
                t.sog_kn = float(init.get("sog", 12.0))
                t.cog_deg = float(init.get("cog", 180.0))
                t.heading_deg = float(init.get("heading", 180.0))
                t.covariance = [0.0] * 9
                t.classification = "cargo"
                t.classification_confidence = 0.9
                t.cpa_m = 500.0
                t.tcpa_s = 300.0

                ec = EncounterClassification()
                ec.encounter_type = EncounterClassification.ENCOUNTER_TYPE_HEAD_ON
                ec.relative_bearing_deg = 0.0
                ec.aspect_angle_deg = 0.0
                ec.is_giveway = True
                t.encounter = ec

                t.confidence = 0.9
                t.source_sensor = "fused"
                targets.append(t)
        else:
            # Default single target: oncoming vessel
            t = TrackedTarget()
            t.schema_version = "v1.1.2"
            t.stamp = stamp
            t.target_id = 1
            t.position = GeoPoint(latitude=63.5, longitude=10.4, altitude=0.0)
            t.sog_kn = 12.0
            t.cog_deg = 180.0
            t.heading_deg = 180.0
            t.covariance = [0.0] * 9
            t.classification = "cargo"
            t.classification_confidence = 0.9
            t.cpa_m = 500.0
            t.tcpa_s = 300.0
            ec = EncounterClassification()
            ec.encounter_type = EncounterClassification.ENCOUNTER_TYPE_HEAD_ON
            ec.relative_bearing_deg = 0.0
            ec.aspect_angle_deg = 0.0
            ec.is_giveway = True
            t.encounter = ec
            t.confidence = 0.9
            t.source_sensor = "fused"
            targets.append(t)

        msg.targets = targets
        msg.confidence = 0.9
        msg.rationale = f"Mock tracked targets ({len(targets)} ship(s))"
        self._pub_targets.publish(msg)

    def _publish_environment(self, stamp: Time) -> None:
        """Publish mock environment state."""
        msg = EnvironmentState()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp
        msg.wind_speed_kn = 15.0
        msg.wind_direction_deg = 180.0
        msg.wave_height_m = 1.5
        msg.wave_direction_deg = 180.0
        msg.current_speed_kn = 0.5
        msg.current_direction_deg = 90.0
        msg.visibility_range_nm = 5.0
        msg.weather_source = "forecast"
        msg.confidence = 0.8
        msg.rationale = "Mock environment state"
        self._pub_env.publish(msg)

    def _publish_planned_route(self, stamp: Time) -> None:
        """Publish planned route as GeoPath: own_ship position → ~10nm north."""
        msg = PlannedRoute()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp
        msg.route_id = 1
        msg.total_distance_nm = 10.0
        msg.estimated_duration_s = 2000.0
        msg.safety_zone = "standard"

        # 2 waypoints: own ship → north ~10nm (latitude + 0.09 ≈ 10nm)
        if self._scenario is not None:
            lat = self._own_lat
            lon = self._own_lon
        else:
            lat = 63.435
            lon = 10.395

        h = Header()
        h.stamp = stamp
        h.frame_id = "map"

        wp1 = GeoPoseStamped()
        wp1.header = h
        wp1.pose.position = GeoPoint(latitude=lat, longitude=lon, altitude=0.0)
        wp1.pose.orientation = Quaternion(x=0.0, y=0.0, z=0.0, w=1.0)

        wp2 = GeoPoseStamped()
        wp2.header = h
        wp2.pose.position = GeoPoint(
            latitude=lat + 0.09, longitude=lon, altitude=0.0
        )
        wp2.pose.orientation = Quaternion(x=0.0, y=0.0, z=0.0, w=1.0)

        route = GeoPath()
        route.header = h
        route.poses = [wp1, wp2]

        msg.route = route
        msg.speed_profile_kn = [18.0]
        msg.confidence = 0.95
        msg.rationale = "Mock planned route (own_ship → 10nm north)"
        self._pub_route.publish(msg)

    def _publish_speed_profile(self, stamp: Time) -> None:
        """Publish constant 18kn speed profile for entire route."""
        msg = SpeedProfile()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp
        msg.profile_id = 1
        msg.segment_start_distances_m = [0.0]
        msg.segment_end_distances_m = [18520.0]  # ~10nm in meters
        msg.target_speeds_kn = [18.0]
        msg.max_speeds_kn = [20.0]
        msg.min_speeds_kn = [5.0]
        msg.segment_types = ["transit"]
        msg.confidence = 0.95
        msg.rationale = "Mock speed profile (constant 18kn)"
        self._pub_speed.publish(msg)

    # ------------------------------------------------------------------
    # 1 Hz: VoyageTask
    # ------------------------------------------------------------------
    def _publish_voyage_task(self, stamp: Time) -> None:
        """Publish mock voyage task with Trondheim Fjord departure/destination."""
        msg = VoyageTask()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp
        msg.task_id = self._counter

        msg.departure = GeoPoint(latitude=63.435, longitude=10.395, altitude=0.0)
        msg.destination = GeoPoint(latitude=63.525, longitude=10.395, altitude=0.0)

        eta_window = TimeWindow()
        eta_window.earliest = stamp
        latest = Time()
        latest.sec = stamp.sec + 7200
        latest.nanosec = stamp.nanosec
        eta_window.latest = latest
        msg.eta_window = eta_window

        msg.optimization_priority = "balanced"
        msg.mandatory_waypoints = []
        msg.exclusion_zones = []
        msg.special_restrictions = ""
        msg.confidence = 1.0
        msg.rationale = "Mock voyage task"
        self._pub_voyage.publish(msg)

    # ------------------------------------------------------------------
    # 5 s: ReplanResponse (event-driven in production)
    # ------------------------------------------------------------------
    def _publish_replan_response(self, stamp: Time) -> None:
        """Publish idle replan response (success, no action needed)."""
        msg = ReplanResponse()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp
        msg.status = ReplanResponse.STATUS_SUCCESS
        msg.failure_reason = ""
        msg.retry_recommended = False
        msg.confidence = 1.0
        msg.rationale = "Mock replan response (idle)"
        self._pub_replan.publish(msg)

    # ------------------------------------------------------------------
    # 30 s: event-style topics (idle state placeholders)
    # ------------------------------------------------------------------
    def _publish_checker_veto(self, stamp: Time) -> None:
        """Publish idle veto notification."""
        msg = CheckerVetoNotification()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp
        msg.checker_layer = "L3"
        msg.vetoed_module = "m5_tactical_planner"
        msg.veto_reason_class = CheckerVetoNotification.VETO_REASON_COLREGS_VIOLATION
        msg.veto_reason_detail = "Mock veto (idle)"
        msg.fallback_provided = True
        msg.confidence = 1.0
        msg.rationale = "Mock checker veto notification"
        self._pub_veto.publish(msg)

    def _publish_reflex_activation(self, stamp: Time) -> None:
        """Publish idle reflex activation (not active)."""
        msg = ReflexActivationNotification()
        msg.schema_version = "v1.1.2"
        msg.activation_time = stamp
        msg.reason = "none"
        msg.l3_freeze_required = False
        self._pub_reflex.publish(msg)

    def _publish_override_signal(self, stamp: Time) -> None:
        """Publish idle override signal (not active)."""
        msg = OverrideActiveSignal()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp
        msg.override_active = False
        msg.activation_source = "none"
        self._pub_override.publish(msg)

    def _publish_emergency_command(self, stamp: Time) -> None:
        """Publish idle emergency command (no emergency)."""
        msg = EmergencyCommand()
        msg.schema_version = "v1.1.2"
        msg.trigger_time = stamp
        msg.cpa_at_trigger_m = 0.0
        msg.range_at_trigger_m = 0.0
        msg.sensor_source = "none"
        msg.action = EmergencyCommand.ACTION_STOP
        msg.confidence = 0.95
        self._pub_emergency.publish(msg)

    # ------------------------------------------------------------------
    # Timer callbacks
    # ------------------------------------------------------------------
    def _publish_50hz(self) -> None:
        stamp = self._make_stamp()
        self._publish_own_ship_state(stamp)

    def _publish_2hz(self) -> None:
        stamp = self._make_stamp()
        self._publish_tracked_targets(stamp)
        self._publish_environment(stamp)
        self._publish_planned_route(stamp)
        self._publish_speed_profile(stamp)

    def _publish_1hz(self) -> None:
        stamp = self._make_stamp()
        self._counter += 1
        self._publish_voyage_task(stamp)

    def _publish_5s(self) -> None:
        self._publish_replan_response(self._make_stamp())

    def _publish_30s(self) -> None:
        stamp = self._make_stamp()
        self._publish_checker_veto(stamp)
        self._publish_reflex_activation(stamp)
        self._publish_override_signal(stamp)
        self._publish_emergency_command(stamp)


def main(args: list[str] | None = None) -> None:
    rclpy.init(args=args)
    node = ExternalMockPublisher()

    # Declare and evaluate optional scenario_path parameter
    node.declare_parameter("scenario_path", "")
    scenario_path = node.get_parameter("scenario_path").value
    if scenario_path:
        node.load_scenario(str(scenario_path))

    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    except rclpy.executors.ExternalShutdownException:
        pass
    finally:
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == "__main__":
    main()
