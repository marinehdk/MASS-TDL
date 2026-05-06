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

class ExternalMockPublisher(Node):
    def __init__(self):
        super().__init__("l3_external_mock_publisher")

        # Publishers
        self.pub_voyage = self.create_publisher(VoyageTask, "/l1/voyage_task", 10)
        self.pub_route = self.create_publisher(PlannedRoute, "/l2/planned_route", 10)
        self.pub_speed = self.create_publisher(SpeedProfile, "/l2/speed_profile", 10)
        self.pub_replan = self.create_publisher(ReplanResponse, "/l2/replan_response", 10)
        self.pub_targets = self.create_publisher(TrackedTargetArray, "/fusion/tracked_targets", 10)
        self.pub_ownship = self.create_publisher(FilteredOwnShipState, "/fusion/own_ship_state", 10)
        self.pub_env = self.create_publisher(EnvironmentState, "/fusion/environment_state", 10)
        self.pub_veto = self.create_publisher(CheckerVetoNotification, "/checker/veto_notification", 10)
        self.pub_emergency = self.create_publisher(EmergencyCommand, "/reflex/emergency_command", 10)
        self.pub_reflex = self.create_publisher(ReflexActivationNotification, "/reflex/activation_notification", 10)
        self.pub_override = self.create_publisher(OverrideActiveSignal, "/override/active_signal", 10)

        # Timers
        self.timer_1hz = self.create_timer(1.0, self.publish_1hz)
        self.timer_2hz = self.create_timer(0.5, self.publish_2hz)

        self._counter = 0

    def _make_stamp(self):
        now = self.get_clock().now()
        stamp = Time()
        stamp.sec = int(now.nanoseconds // 1_000_000_000)
        stamp.nanosec = int(now.nanoseconds % 1_000_000_000)
        return stamp

    def publish_1hz(self):
        stamp = self._make_stamp()
        self._counter += 1

        # VoyageTask
        msg = VoyageTask()
        msg.stamp = stamp
        msg.task_id = self._counter
        msg.task_type = "transit"
        msg.speed_cmd_kn = 18.0
        msg.confidence = 1.0
        msg.rationale = "Mock voyage task"
        self.pub_voyage.publish(msg)

        # PlannedRoute
        msg = PlannedRoute()
        msg.stamp = stamp
        msg.route_id = self._counter
        msg.total_distance_nm = 50.0
        msg.estimated_duration_s = 10000.0
        msg.confidence = 0.95
        msg.rationale = "Mock planned route"
        self.pub_route.publish(msg)

        # SpeedProfile
        msg = SpeedProfile()
        msg.stamp = stamp
        msg.profile_id = self._counter
        msg.confidence = 0.95
        msg.rationale = "Mock speed profile"
        self.pub_speed.publish(msg)

        # ReplanResponse
        msg = ReplanResponse()
        msg.stamp = stamp
        msg.status = ReplanResponse.STATUS_SUCCESS
        msg.retry_recommended = False
        msg.confidence = 1.0
        msg.rationale = "Mock replan response"
        self.pub_replan.publish(msg)

        # EnvironmentState
        msg = EnvironmentState()
        msg.stamp = stamp
        msg.wind_speed_kn = 15.0
        msg.wind_direction_deg = 180.0
        msg.wave_height_m = 1.5
        msg.visibility_range_nm = 5.0
        msg.confidence = 0.8
        msg.rationale = "Mock environment"
        self.pub_env.publish(msg)

        # CheckerVetoNotification
        msg = CheckerVetoNotification()
        msg.stamp = stamp
        msg.checker_layer = "L3"
        msg.vetoed_module = "m5_tactical_planner"
        msg.veto_reason_class = CheckerVetoNotification.VETO_REASON_COLREGS_VIOLATION
        msg.veto_reason_detail = "Mock veto"
        msg.fallback_provided = True
        msg.confidence = 1.0
        self.pub_veto.publish(msg)

        # ReflexActivationNotification
        msg = ReflexActivationNotification()
        msg.stamp = stamp
        msg.is_active = False
        msg.activation_reason = "none"
        msg.estimated_bypass_duration_s = 0.0
        msg.confidence = 1.0
        msg.rationale = "Mock reflex state"
        self.pub_reflex.publish(msg)

        # OverrideActiveSignal
        msg = OverrideActiveSignal()
        msg.stamp = stamp
        msg.override_active = False
        msg.override_source = "none"
        msg.confidence = 1.0
        msg.rationale = "Mock override state"
        self.pub_override.publish(msg)

    def publish_2hz(self):
        stamp = self._make_stamp()

        # FilteredOwnShipState - 50 Hz simulation (throttled to 2 Hz for mock)
        msg = FilteredOwnShipState()
        msg.stamp = stamp
        msg.sog_kn = 18.0
        msg.cog_deg = 45.0
        msg.heading_deg = 45.0
        msg.nav_filter_status = "OPTIMAL"
        msg.confidence = 0.98
        msg.rationale = "Mock own ship state"
        self.pub_ownship.publish(msg)

        # TrackedTargetArray
        msg = TrackedTargetArray()
        msg.stamp = stamp
        msg.target_ids = [1, 2]
        msg.classifications = ["vessel", "vessel"]
        msg.classification_confidences = [0.95, 0.85]
        msg.confidence = 0.90
        msg.rationale = "Mock tracked targets"
        self.pub_targets.publish(msg)

        # EmergencyCommand (rare, only if emergency flag set)
        # Not published by default in mock


def main(args=None):
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
