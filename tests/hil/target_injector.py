"""
target_injector.py — ROS2 Python node that publishes scenario targets and
environment state for HIL end-to-end testing of the MASS L3 Tactical Layer.

Publishes:
  /fusion/tracked_targets  (l3_external_msgs/TrackedTargetArray)  @ 1 Hz
  /fusion/environment_state (l3_external_msgs/EnvironmentState)   @ 1 Hz

Per v1.1.2 §15.1 message contracts + RFC-002 (AoS TrackedTargetArray).
"""

from __future__ import annotations

import math
import threading
from typing import Optional

try:
    import rclpy
    from rclpy.node import Node
    from rclpy.qos import QoSProfile, ReliabilityPolicy, HistoryPolicy
    _RCLPY_AVAILABLE = True
except ImportError:
    _RCLPY_AVAILABLE = False
    Node = object  # type: ignore

try:
    from l3_external_msgs.msg import TrackedTargetArray, EnvironmentState
    from l3_msgs.msg import TrackedTarget, EncounterClassification
    from builtin_interfaces.msg import Time as RosTime
    from geographic_msgs.msg import GeoPoint
    _MSGS_AVAILABLE = True
except ImportError:
    _MSGS_AVAILABLE = False

# Geographic origin matching fcb_dynamics.yaml defaults
_GEO_ORIGIN_LAT = 30.5    # degrees North
_GEO_ORIGIN_LON = 122.0   # degrees East
_EARTH_M_PER_DEG = 111320.0


def _enu_to_geopoint(x_m: float, y_m: float) -> "GeoPoint":
    """Convert ENU metres (x=East, y=North from origin) to absolute GeoPoint."""
    pt = GeoPoint()
    pt.latitude = _GEO_ORIGIN_LAT + y_m / _EARTH_M_PER_DEG
    pt.longitude = _GEO_ORIGIN_LON + x_m / (
        _EARTH_M_PER_DEG * math.cos(math.radians(_GEO_ORIGIN_LAT))
    )
    pt.altitude = 0.0
    return pt

from scenario_schema import Scenario


def _ros_time_now(node: "Node") -> "RosTime":
    """Return current ROS time as builtin_interfaces/Time."""
    t = node.get_clock().now()
    sec, nanosec = t.seconds_nanoseconds()
    msg = RosTime()
    msg.sec = sec
    msg.nanosec = nanosec
    return msg


class TargetInjectorNode:
    """
    ROS2 publisher node for HIL scenario target injection.

    Usage::

        node = TargetInjectorNode(ros_node)
        node.load_scenario(scenario)
        # ... let the test run ...
        node.stop()
    """

    def __init__(self, ros_node: "Node") -> None:
        self._node = ros_node
        self._scenario: Optional[Scenario] = None
        self._lock = threading.Lock()
        self._running = False
        self._timer = None

        if not (_RCLPY_AVAILABLE and _MSGS_AVAILABLE):
            return

        qos = QoSProfile(
            history=HistoryPolicy.KEEP_LAST,
            depth=10,
            reliability=ReliabilityPolicy.RELIABLE,
        )

        self._targets_pub = ros_node.create_publisher(
            TrackedTargetArray,
            "/fusion/tracked_targets",
            qos,
        )
        self._env_pub = ros_node.create_publisher(
            EnvironmentState,
            "/fusion/environment_state",
            qos,
        )

        # 1 Hz timer
        self._timer = ros_node.create_timer(1.0, self._publish_callback)
        self._running = True

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    def load_scenario(self, scenario: Optional[Scenario]) -> None:
        """Update the active scenario; takes effect on the next timer tick."""
        with self._lock:
            self._scenario = scenario

    def stop(self) -> None:
        """Cancel timer and mark stopped."""
        self._running = False
        if self._timer is not None:
            try:
                self._timer.cancel()
            except Exception:
                pass
            self._timer = None

    # ------------------------------------------------------------------
    # Internal
    # ------------------------------------------------------------------

    def _publish_callback(self) -> None:
        if not self._running:
            return
        with self._lock:
            scenario = self._scenario
        if scenario is None:
            return

        try:
            self._publish_targets(scenario)
            self._publish_environment(scenario)
        except Exception as exc:  # noqa: BLE001
            self._node.get_logger().warn(
                f"target_injector publish error: {exc}"
            )

    def _publish_targets(self, scenario: Scenario) -> None:
        msg = TrackedTargetArray()
        msg.stamp = _ros_time_now(self._node)
        msg.confidence = 0.95
        msg.rationale = "hil_target_injector"

        targets = []
        for t in scenario.targets:
            tt = TrackedTarget()
            tt.stamp = _ros_time_now(self._node)
            tt.target_id = t.mmsi

            # Position: convert absolute ENU metres to absolute WGS-84 GeoPoint.
            # tgt.x_m / tgt.y_m are absolute ENU from the geographic origin
            # (matching fcb_dynamics.yaml origin_lat/lon); no own-ship subtraction.
            tt.position = _enu_to_geopoint(t.x_m, t.y_m)

            tt.sog_kn = float(t.sog_kn)
            tt.cog_deg = float(t.cog_deg)
            tt.heading_deg = float(t.cog_deg)   # best available; no separate heading
            tt.covariance = [0.0] * 9           # zero covariance for HIL

            tt.classification = "vessel"
            tt.classification_confidence = 0.95

            # CPA/TCPA: populate from expected outcome if single target,
            # else leave as 0 — M2 will recompute from tracked state.
            if len(scenario.targets) == 1:
                tt.cpa_m = float(scenario.expected.cpa_m)
                tt.tcpa_s = float(scenario.expected.tcpa_s)
            else:
                tt.cpa_m = 0.0
                tt.tcpa_s = 0.0

            # EncounterClassification stub — M2 will recompute; we provide
            # a best-effort pre-classification so M6 can start reasoning.
            enc = EncounterClassification()
            enc.encounter_type = EncounterClassification.ENCOUNTER_TYPE_NONE
            enc.relative_bearing_deg = 0.0
            enc.aspect_angle_deg = 0.0
            enc.is_giveway = (scenario.expected.own_role in ("give_way", "both_give_way"))
            tt.encounter = enc

            tt.confidence = 0.95
            tt.source_sensor = "fused"

            targets.append(tt)

        msg.targets = targets
        self._targets_pub.publish(msg)

    def _publish_environment(self, scenario: Scenario) -> None:
        msg = EnvironmentState()
        msg.stamp = _ros_time_now(self._node)
        msg.visibility_range_nm = float(scenario.odd_visibility_nm)
        msg.weather_source = "sensor"

        # Encode degraded/critical ODD mode via elevated wind speed so
        # M1 ODD/Envelope Manager transitions to the correct mode.
        if scenario.odd_mode in ("DEGRADED", "CRITICAL"):
            msg.wind_speed_kn = 35.0
        else:
            msg.wind_speed_kn = 10.0

        msg.wind_direction_deg = 0.0
        msg.wave_height_m = 0.5
        msg.wave_direction_deg = 0.0
        msg.current_speed_kn = 0.0
        msg.current_direction_deg = 0.0
        msg.confidence = 0.95
        msg.rationale = "hil_target_injector"

        self._env_pub.publish(msg)
