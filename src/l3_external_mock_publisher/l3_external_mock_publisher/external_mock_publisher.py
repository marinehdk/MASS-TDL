#!/usr/bin/env python3
"""
Mock publisher node for external interface messages.
Publishes sample messages for all l3_external_msgs topics.

Usage:
    ros2 run l3_external_mock_publisher external_mock_publisher
"""
import rclpy
from rclpy.node import Node
from builtin_interfaces.msg import Time
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
)
from typing import Optional


class ExternalMockPublisher(Node):
    """Publishes mock external messages at specified frequencies."""

    def __init__(self) -> None:
        super().__init__("l3_external_mock_publisher")

        self._counter: int = 0

        # Publishers
        self._pub_voyage = self.create_publisher(VoyageTask, "/l1/voyage_task", 10)
        self._pub_route = self.create_publisher(PlannedRoute, "/l2/planned_route", 10)
        self._pub_speed = self.create_publisher(SpeedProfile, "/l2/speed_profile", 10)
        self._pub_replan = self.create_publisher(ReplanResponse, "/l2/replan_response", 10)
        self._pub_targets = self.create_publisher(TrackedTargetArray, "/fusion/tracked_targets", 10)
        self._pub_ownship = self.create_publisher(FilteredOwnShipState, "/fusion/own_ship_state", 10)
        self._pub_env = self.create_publisher(EnvironmentState, "/fusion/environment_state", 10)
        self._pub_veto = self.create_publisher(CheckerVetoNotification, "/checker/veto_notification", 10)
        self._pub_emergency = self.create_publisher(EmergencyCommand, "/reflex/emergency_command", 10)
        self._pub_reflex = self.create_publisher(ReflexActivationNotification, "/reflex/activation_notification", 10)
        self._pub_override = self.create_publisher(OverrideActiveSignal, "/override/active_signal", 10)

        # Timers
        self._timer_1hz = self.create_timer(1.0, self._publish_1hz)
        self._timer_2hz = self.create_timer(0.5, self._publish_2hz)

    @staticmethod
    def _make_stamp() -> Time:
        """Create a timestamp from the current ROS clock."""
        return Time().from_msg(self.get_clock().now().to_msg())  # type: ignore[arg-type]

    def _publish_voyage_task(self, stamp: Time) -> None:
        msg = VoyageTask()
        msg.stamp = stamp
        msg.task_id = self._counter
        msg.task_type = "transit"
        msg.speed_cmd_kn = 18.0
        msg.confidence = 1.0
        msg.rationale = "Mock voyage task"
        self._pub_voyage.publish(msg)

    def _publish_planned_route(self, stamp: Time) -> None:
        msg = PlannedRoute()
        msg.stamp = stamp
        msg.route_id = self._counter
        msg.total_distance_nm = 50.0
        msg.estimated_duration_s = 10000.0
        msg.confidence = 0.95
        msg.rationale = "Mock planned route"
        self._pub_route.publish(msg)

    def _publish_speed_profile(self, stamp: Time) -> None:
        msg = SpeedProfile()
        msg.stamp = stamp
        msg.profile_id = self._counter
        msg.confidence = 0.95
        msg.rationale = "Mock speed profile"
        self._pub_speed.publish(msg)

    def _publish_replan_response(self, stamp: Time) -> None:
        msg = ReplanResponse()
        msg.stamp = stamp
        msg.status = ReplanResponse.STATUS_SUCCESS
        msg.retry_recommended = False
        msg.confidence = 1.0
        msg.rationale = "Mock replan response"
        self._pub_replan.publish(msg)

    def _publish_environment(self, stamp: Time) -> None:
        msg = EnvironmentState()
        msg.stamp = stamp
        msg.wind_speed_kn = 15.0
        msg.wind_direction_deg = 180.0
        msg.wave_height_m = 1.5
        msg.visibility_range_nm = 5.0
        msg.confidence = 0.8
        msg.rationale = "Mock environment"
        self._pub_env.publish(msg)

    def _publish_veto(self, stamp: Time) -> None:
        msg = CheckerVetoNotification()
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
        msg.stamp = stamp
        msg.is_active = False
        msg.activation_reason = "none"
        msg.estimated_bypass_duration_s = 0.0
        msg.confidence = 1.0
        msg.rationale = "Mock reflex state"
        self._pub_reflex.publish(msg)

    def _publish_override_signal(self, stamp: Time) -> None:
        msg = OverrideActiveSignal()
        msg.stamp = stamp
        msg.override_active = False
        msg.override_source = "none"
        msg.confidence = 1.0
        msg.rationale = "Mock override state"
        self._pub_override.publish(msg)

    def _publish_ownship(self, stamp: Time) -> None:
        msg = FilteredOwnShipState()
        msg.stamp = stamp
        msg.sog_kn = 18.0
        msg.cog_deg = 45.0
        msg.heading_deg = 45.0
        msg.nav_filter_status = "OPTIMAL"
        msg.confidence = 0.98
        msg.rationale = "Mock own ship state"
        self._pub_ownship.publish(msg)

    def _publish_targets(self, stamp: Time) -> None:
        msg = TrackedTargetArray()
        msg.stamp = stamp
        msg.target_ids = [1, 2]
        msg.classifications = ["vessel", "vessel"]
        msg.classification_confidences = [0.95, 0.85]
        msg.confidence = 0.90
        msg.rationale = "Mock tracked targets"
        self._pub_targets.publish(msg)

    def _publish_1hz(self) -> None:
        stamp = self._make_stamp()
        self._counter += 1

        self._publish_voyage_task(stamp)
        self._publish_planned_route(stamp)
        self._publish_speed_profile(stamp)
        self._publish_replan_response(stamp)
        self._publish_environment(stamp)
        self._publish_veto(stamp)
        self._publish_reflex_state(stamp)
        self._publish_override_signal(stamp)

    def _publish_2hz(self) -> None:
        stamp = self._make_stamp()

        self._publish_ownship(stamp)
        self._publish_targets(stamp)

        # EmergencyCommand: not published by default (event-only)


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
