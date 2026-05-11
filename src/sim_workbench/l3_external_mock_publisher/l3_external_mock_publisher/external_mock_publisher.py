#!/usr/bin/env python3
"""Mock publisher node for external interface messages.

Publishes sample messages for all l3_external_msgs topics at frequencies aligned
with v1.1.2 §15.2 interface matrix.

Wave 0 fixes:
- F-CRIT-B-001: removed @staticmethod misuse on _make_stamp (would NameError on `self`)
- F-IMP-B-001: corrected publishing frequencies (own_ship 50 Hz, environment 0.2 Hz)
- F-IMP-B-002: added EmergencyCommand event publisher trigger via timer (testing only)
- F-CRIT-B-006: OverrideActiveSignal field renamed override_source → activation_source
- F-CRIT-B-005: ReflexActivationNotification updated to spec (l3_freeze_required)
- F-CRIT-B-004: EmergencyCommand updated to spec (action enum)
- F-CRIT-B-002: VoyageTask updated to spec (departure/destination/eta_window/...)

Usage:
    ros2 run l3_external_mock_publisher external_mock_publisher
"""
import rclpy
from rclpy.node import Node
from builtin_interfaces.msg import Time
from geographic_msgs.msg import GeoPoint, GeoPath
from typing import Optional, List

from l3_external_msgs.msg import (
    VoyageTask,
    PlannedRoute,
    SpeedProfile,
    ReplanResponse,
    TrackedTargetArray,
    FilteredOwnShipState,
    EnvironmentState,
    CheckerVetoNotification,
    EmergencyCommand,
    ReflexActivationNotification,
    OverrideActiveSignal,
    TimeWindow,
)
from l3_msgs.msg import TrackedTarget, EncounterClassification


class ExternalMockPublisher(Node):
    """Publishes mock external messages at v1.1.2 §15.2 specified frequencies."""

    def __init__(self) -> None:
        super().__init__("l3_external_mock_publisher")

        self._counter: int = 0
        self._emergency_demo_counter: int = 0  # for periodic event-style emergency

        # Publishers (11 cross-layer topics, aligned with §15.2)
        self._pub_voyage = self.create_publisher(VoyageTask, "/l1/voyage_task", 10)
        self._pub_route = self.create_publisher(PlannedRoute, "/l2/planned_route", 10)
        self._pub_speed = self.create_publisher(SpeedProfile, "/l2/speed_profile", 10)
        self._pub_replan = self.create_publisher(ReplanResponse, "/l2/replan_response", 10)
        self._pub_targets = self.create_publisher(TrackedTargetArray, "/fusion/tracked_targets", 10)
        self._pub_ownship = self.create_publisher(FilteredOwnShipState, "/fusion/own_ship_state", 50)
        self._pub_env = self.create_publisher(EnvironmentState, "/fusion/environment_state", 10)
        self._pub_veto = self.create_publisher(CheckerVetoNotification, "/checker/veto_notification", 10)
        self._pub_emergency = self.create_publisher(EmergencyCommand, "/reflex/emergency_command", 10)
        self._pub_reflex = self.create_publisher(ReflexActivationNotification, "/reflex/activation_notification", 10)
        self._pub_override = self.create_publisher(OverrideActiveSignal, "/override/active_signal", 10)

        # Timers (frequency aligned with v1.1.2 §15.2):
        # - 50 Hz: FilteredOwnShipState
        # - 2 Hz:  TrackedTargetArray
        # - 1 Hz:  PlannedRoute / SpeedProfile / VoyageTask (event-driven, but periodic for mock)
        # - 0.2 Hz (every 5 s): EnvironmentState
        # - 0.033 Hz (every 30 s): event-style demos (CheckerVetoNotification / Emergency / Reflex / Override / ReplanResponse)
        self._timer_50hz = self.create_timer(0.02, self._publish_50hz)
        self._timer_2hz = self.create_timer(0.5, self._publish_2hz)
        self._timer_1hz = self.create_timer(1.0, self._publish_1hz)
        self._timer_5s = self.create_timer(5.0, self._publish_environment_tick)
        self._timer_30s = self.create_timer(30.0, self._publish_event_demos)

    def _make_stamp(self) -> Time:
        """Create a timestamp from the current ROS clock."""
        return self.get_clock().now().to_msg()

    # --- 1 Hz publishers (VoyageTask / PlannedRoute / SpeedProfile) ---
    def _publish_voyage_task(self, stamp: Time) -> None:
        msg = VoyageTask()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp
        msg.task_id = self._counter

        msg.departure = GeoPoint(latitude=22.5, longitude=114.0, altitude=0.0)
        msg.destination = GeoPoint(latitude=22.7, longitude=114.3, altitude=0.0)

        eta_window = TimeWindow()
        eta_window.earliest = stamp
        # latest = now + 2h (rough mock)
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

    def _publish_planned_route(self, stamp: Time) -> None:
        msg = PlannedRoute()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp
        msg.route_id = self._counter
        msg.total_distance_nm = 50.0
        msg.estimated_duration_s = 10000.0
        msg.confidence = 0.95
        msg.rationale = "Mock planned route"
        self._pub_route.publish(msg)

    def _publish_speed_profile(self, stamp: Time) -> None:
        msg = SpeedProfile()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp
        msg.profile_id = self._counter
        msg.confidence = 0.95
        msg.rationale = "Mock speed profile"
        self._pub_speed.publish(msg)

    # --- 2 Hz: TrackedTargetArray (AoS — uses l3_msgs/TrackedTarget[]) ---
    def _publish_targets(self, stamp: Time) -> None:
        msg = TrackedTargetArray()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp

        targets: List[TrackedTarget] = []
        for tid in (1, 2):
            t = TrackedTarget()
            t.stamp = stamp
            t.target_id = tid
            t.position = GeoPoint(latitude=22.5 + 0.001 * tid, longitude=114.0 + 0.001 * tid, altitude=0.0)
            t.sog_kn = 12.0
            t.cog_deg = 45.0
            t.heading_deg = 45.0
            t.covariance = [0.0] * 9
            t.classification = "vessel"
            t.classification_confidence = 0.9
            t.cpa_m = 500.0
            t.tcpa_s = 60.0
            ec = EncounterClassification()
            ec.encounter_type = EncounterClassification.ENCOUNTER_TYPE_NONE
            ec.relative_bearing_deg = 0.0
            ec.aspect_angle_deg = 0.0
            ec.is_giveway = False
            t.encounter = ec
            t.confidence = 0.9
            t.source_sensor = "fused"
            targets.append(t)
        msg.targets = targets

        msg.confidence = 0.90
        msg.rationale = "Mock tracked targets"
        self._pub_targets.publish(msg)

    # --- 50 Hz: FilteredOwnShipState ---
    def _publish_ownship(self, stamp: Time) -> None:
        msg = FilteredOwnShipState()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp
        msg.position = GeoPoint(latitude=22.5, longitude=114.0, altitude=0.0)
        msg.sog_kn = 18.0
        msg.cog_deg = 45.0
        msg.heading_deg = 45.0
        msg.u_water = 9.0
        msg.v_water = 0.0
        msg.r_dot_deg_s = 0.0
        msg.current_speed_kn = 0.5
        msg.current_direction_deg = 90.0
        msg.covariance = [0.0] * 36
        msg.nav_mode = "OPTIMAL"  # F-IMP-B-008: per RFC-002 (OPTIMAL/DR_SHORT/DR_LONG/DEGRADED)
        msg.confidence = 0.98
        msg.rationale = "Mock own ship state"
        self._pub_ownship.publish(msg)

    # --- 0.2 Hz: EnvironmentState (every 5 s) ---
    def _publish_environment(self, stamp: Time) -> None:
        msg = EnvironmentState()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp
        msg.wind_speed_kn = 15.0
        msg.wind_direction_deg = 180.0
        msg.wave_height_m = 1.5
        msg.visibility_range_nm = 5.0
        msg.confidence = 0.8
        msg.rationale = "Mock environment"
        self._pub_env.publish(msg)

    # --- Event-style (every 30 s for testing): default-state placeholders ---
    def _publish_replan_response(self, stamp: Time) -> None:
        msg = ReplanResponse()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp
        msg.status = ReplanResponse.STATUS_SUCCESS
        msg.failure_reason = ""
        msg.retry_recommended = False
        self._pub_replan.publish(msg)

    def _publish_veto(self, stamp: Time) -> None:
        msg = CheckerVetoNotification()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp
        msg.checker_layer = "L3"
        msg.vetoed_module = "m5_tactical_planner"
        msg.veto_reason_class = CheckerVetoNotification.VETO_REASON_COLREGS_VIOLATION
        msg.veto_reason_detail = "Mock veto"
        msg.fallback_provided = True
        msg.confidence = 1.0
        msg.rationale = "Mock veto notification"
        self._pub_veto.publish(msg)

    def _publish_reflex_state(self, stamp: Time) -> None:
        msg = ReflexActivationNotification()
        msg.schema_version = "v1.1.2"
        msg.activation_time = stamp
        msg.reason = "none"
        msg.l3_freeze_required = False  # F-CRIT-B-005: spec required field
        self._pub_reflex.publish(msg)

    def _publish_override_signal(self, stamp: Time) -> None:
        msg = OverrideActiveSignal()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp
        msg.override_active = False
        msg.activation_source = "none"  # F-CRIT-B-006: renamed from override_source
        self._pub_override.publish(msg)

    def _publish_emergency_command(self, stamp: Time) -> None:
        msg = EmergencyCommand()
        msg.schema_version = "v1.1.2"
        msg.trigger_time = stamp
        msg.cpa_at_trigger_m = 100.0
        msg.range_at_trigger_m = 200.0
        msg.sensor_source = "fusion"
        msg.action = EmergencyCommand.ACTION_STOP  # F-CRIT-B-004: spec action enum
        msg.confidence = 0.95
        self._pub_emergency.publish(msg)

    # --- Timer callbacks ---
    def _publish_50hz(self) -> None:
        stamp = self._make_stamp()
        self._publish_ownship(stamp)

    def _publish_2hz(self) -> None:
        stamp = self._make_stamp()
        self._publish_targets(stamp)

    def _publish_1hz(self) -> None:
        stamp = self._make_stamp()
        self._counter += 1
        self._publish_voyage_task(stamp)
        self._publish_planned_route(stamp)
        self._publish_speed_profile(stamp)

    def _publish_environment_tick(self) -> None:
        self._publish_environment(self._make_stamp())

    def _publish_event_demos(self) -> None:
        """Periodic event-style demos for testing event-driven topics.

        Real event-driven topics (CheckerVetoNotification / EmergencyCommand /
        ReflexActivationNotification / OverrideActiveSignal / ReplanResponse) only
        fire on triggers in production. This 30 s tick exercises them for
        integration tests; downstream nodes should not assume periodicity.
        """
        stamp = self._make_stamp()
        self._publish_replan_response(stamp)
        self._publish_veto(stamp)
        self._publish_reflex_state(stamp)
        self._publish_override_signal(stamp)
        # Emergency only fires every 5th tick (~2.5 min) to avoid spam
        self._emergency_demo_counter += 1
        if self._emergency_demo_counter % 5 == 0:
            self._publish_emergency_command(stamp)


def main(args: Optional[list[str]] = None) -> None:
    rclpy.init(args=args)
    node = ExternalMockPublisher()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
