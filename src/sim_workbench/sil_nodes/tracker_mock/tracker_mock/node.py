"""Tracker Mock — rclpy.lifecycle.LifecycleNode wrapping god/KF tracker core.

Publishes l3_external_msgs/TrackedTargetArray @ 10 Hz on /sil/tracked_targets.
Subscribes to /sil/radar_meas (sil_msgs/RadarMeasurement) and
/sil/ais_msg (sil_msgs/AISMessage) for input data.

Fallback: if l3_external_msgs is not importable (macOS without ROS2 build),
publishes JSON-encoded std_msgs/String for testing.

NOTE: rclpy imports are confined to the ROS2 wrapper section below so that
pure-logic classes (KalmanFilter2D, TrackerMockCore) can be imported on
systems without ROS 2 (e.g. macOS for unit tests).
"""

import json
import math
from typing import Optional

class KalmanFilter2D:
    """Simple 4-state KF: x, y, vx, vy."""

    def __init__(self, dt: float = 0.1, process_noise: float = 0.1):
        self.dt = dt
        self.q = process_noise
        self.x = None  # [x, y, vx, vy]
        self.P = None

    def init(self, x: float, y: float, vx: float = 0.0, vy: float = 0.0):
        self.x = [x, y, vx, vy]
        self.P = [[1, 0, 0, 0],
                  [0, 1, 0, 0],
                  [0, 0, 1, 0],
                  [0, 0, 0, 1]]

    def predict(self):
        if self.x is None:
            return
        dt = self.dt
        F = [[1, 0, dt, 0],
             [0, 1, 0, dt],
             [0, 0, 1, 0],
             [0, 0, 0, 1]]
        self.x = [sum(F[i][j] * self.x[j] for j in range(4)) for i in range(4)]

    def update(self, zx: float, zy: float):
        if self.x is None:
            self.init(zx, zy)
            return
        self.x[0] = zx
        self.x[1] = zy
        # Simplified direct measurement update (full KF in Phase 2)

    def get_state(self) -> dict:
        if self.x is None:
            return {"x": 0, "y": 0, "vx": 0, "vy": 0}
        return {"x": self.x[0], "y": self.x[1],
                "vx": self.x[2], "vy": self.x[3]}


class TrackerMockCore:
    """Tracker mock — 'god' (perfect GT passthrough) or 'kf' (KF smoothed).

    Pure Python; no ROS dependencies.  Used as the doer inside the
    LifecycleNode wrapper.
    """

    def __init__(self, tracker_type: str = "god"):
        self.tracker_type = tracker_type
        self._kfs: dict[int, KalmanFilter2D] = {}

    def track(self, targets: list[dict]) -> list[dict]:
        results = []
        for t in targets:
            mmsi = t["mmsi"]
            if self.tracker_type == "god":
                # God tracker: perfect passthrough
                results.append({
                    "mmsi": mmsi,
                    "lat": t["lat"],
                    "lon": t["lon"],
                    "sog": t["sog"],
                    "cog": t["cog"],
                })
            else:
                # KF tracker
                if mmsi not in self._kfs:
                    self._kfs[mmsi] = KalmanFilter2D()
                kf = self._kfs[mmsi]
                kf.predict()
                kf.update(t["lat"], t["lon"])
                s = kf.get_state()
                results.append({
                    "mmsi": mmsi,
                    "lat": s["x"],
                    "lon": s["y"],
                    "sog": math.sqrt(s["vx"] ** 2 + s["vy"] ** 2),
                    "cog": math.atan2(s["vy"], s["vx"]),
                })
        return results

# ---------------------------------------------------------------------------
# ROS 2 wrapper — only defined when rclpy is importable.
# On macOS without ROS 2, TrackerMockNode and main() will not be defined.
# Tests should guard accordingly (see test file skipif).
# ---------------------------------------------------------------------------

_HAS_RCLPY: bool = False
try:
    import rclpy  # noqa: F401
    from rclpy.lifecycle import LifecycleNode, LifecycleState, TransitionCallbackReturn
    from rclpy.qos import QoSProfile, ReliabilityPolicy, DurabilityPolicy, HistoryPolicy
    from std_msgs.msg import String

    _HAS_RCLPY = True
except ImportError:
    pass

# Try real msg types; macOS w/o ROS2 build falls back to std_msgs/String.
_USE_REAL_MSGS: bool = False
if _HAS_RCLPY:
    try:
        from l3_external_msgs.msg import TrackedTargetArray  # noqa: F401
        _USE_REAL_MSGS = True
    except ImportError:
        pass


if _HAS_RCLPY:

    class TrackerMockNode(LifecycleNode):
        """LifecycleNode that wraps TrackerMockCore and bridges ROS 2 I/O.

        Lifecycle transitions
        ---------------------
        *on_configure*   — declare parameters
        *on_activate*    — create publisher, timer (10 Hz), subscriptions
        *on_deactivate*  — destroy publisher, timer, subscriptions
        *on_cleanup*     — clear KF state
        """

        def __init__(self):
            super().__init__("tracker_mock_node")
            self._core: Optional[TrackerMockCore] = None

            self._pub = None
            self._timer = None
            self._sub_radar = None
            self._sub_ais = None

            self._latest_radar = None
            self._latest_ais = None

            self._use_real_msgs = _USE_REAL_MSGS

        def on_configure(self, state: LifecycleState) -> TransitionCallbackReturn:
            self.declare_parameter("tracker_type", "god")
            self.get_logger().info("TrackerMockNode configured")
            return TransitionCallbackReturn.SUCCESS

        def on_activate(self, state: LifecycleState) -> TransitionCallbackReturn:
            tracker_type = (
                self.get_parameter("tracker_type").get_parameter_value().string_value
            )
            self._core = TrackerMockCore(tracker_type=tracker_type)

            # RELIABLE + VOLATILE + KEEP_LAST(2) per task spec
            qos = QoSProfile(
                reliability=ReliabilityPolicy.RELIABLE,
                durability=DurabilityPolicy.VOLATILE,
                history=HistoryPolicy.KEEP_LAST,
                depth=2,
            )

            if self._use_real_msgs:
                from l3_external_msgs.msg import TrackedTargetArray as TTA

                self._pub = self.create_publisher(TTA, "/sil/tracked_targets", qos)
                self.get_logger().info(
                    "Publisher [real] /sil/tracked_targets ← "
                    "l3_external_msgs/TrackedTargetArray"
                )
            else:
                self._pub = self.create_publisher(String, "/sil/tracked_targets", qos)
                self.get_logger().warn(
                    "Publisher [json] /sil/tracked_targets ← "
                    "std_msgs/String (l3_external_msgs unavailable)"
                )

            from sil_msgs.msg import AISMessage, RadarMeasurement

            # Radar subscription: BEST_EFFORT to match sensor_mock publisher
            radar_sub_qos = QoSProfile(
                reliability=ReliabilityPolicy.BEST_EFFORT,
                durability=DurabilityPolicy.VOLATILE,
                history=HistoryPolicy.KEEP_LAST,
                depth=2,
            )
            self._sub_radar = self.create_subscription(
                RadarMeasurement,
                "/sil/radar_meas",
                self._radar_callback,
                radar_sub_qos,
            )
            self._sub_ais = self.create_subscription(
                AISMessage,
                "/sil/ais_msg",
                self._ais_callback,
                10,
            )

            self._timer = self.create_timer(0.1, self._track_callback)

            self.get_logger().info(
                f"TrackerMockNode activated  (tracker_type={tracker_type})"
            )
            return TransitionCallbackReturn.SUCCESS

        def on_deactivate(self, state: LifecycleState) -> TransitionCallbackReturn:
            if self._timer is not None:
                self.destroy_timer(self._timer)
                self._timer = None
            if self._pub is not None:
                self.destroy_publisher(self._pub)
                self._pub = None
            if self._sub_radar is not None:
                self.destroy_subscription(self._sub_radar)
                self._sub_radar = None
            if self._sub_ais is not None:
                self.destroy_subscription(self._sub_ais)
                self._sub_ais = None
            self._latest_radar = None
            self._latest_ais = None
            self.get_logger().info("TrackerMockNode deactivated")
            return TransitionCallbackReturn.SUCCESS

        def on_cleanup(self, state: LifecycleState) -> TransitionCallbackReturn:
            if self._core is not None:
                self._core._kfs.clear()
            self._core = None
            self.get_logger().info("TrackerMockNode cleaned up")
            return TransitionCallbackReturn.SUCCESS

        def _radar_callback(self, msg):
            self._latest_radar = msg

        def _ais_callback(self, msg):
            self._latest_ais = msg

        def _track_callback(self):
            if self._core is None:
                return

            targets: list[dict] = []

            if self._latest_ais is not None:
                ais = self._latest_ais
                targets.append({
                    "mmsi": int(ais.mmsi),
                    "lat": ais.lat,
                    "lon": ais.lon,
                    "sog": ais.sog,
                    "cog": ais.cog,
                })

            # TODO(D1.3b): integrate sil_msgs/OwnShipState for range/bearing→lat/lon conversion.
            _ = self._latest_radar

            if not targets:
                return

            tracked: list[dict] = self._core.track(targets)

            if self._use_real_msgs:
                self._publish_real(tracked)
            else:
                self._publish_fallback(tracked)

        def _publish_real(self, tracked: list[dict]):
            """Publish proper l3_external_msgs/TrackedTargetArray message."""
            # pylint: disable=import-outside-toplevel
            from builtin_interfaces.msg import Time as BuiltinTime
            from geographic_msgs.msg import GeoPoint
            from l3_external_msgs.msg import TrackedTargetArray
            from l3_msgs.msg import EncounterClassification, TrackedTarget

            now_msg: BuiltinTime = self.get_clock().now().to_msg()

            msg = TrackedTargetArray()
            msg.schema_version = "v1.1.2"
            msg.stamp = now_msg
            msg.confidence = 1.0
            msg.rationale = "mock_tracker"

            for t in tracked:
                tt = TrackedTarget()
                tt.schema_version = "v1.1.2"
                tt.stamp = now_msg
                tt.target_id = t["mmsi"]
                tt.position = GeoPoint()
                tt.position.latitude = t["lat"]
                tt.position.longitude = t["lon"]
                tt.position.altitude = 0.0
                tt.sog_kn = t["sog"]
                tt.cog_deg = t["cog"]
                tt.heading_deg = t["cog"]
                tt.classification = "vessel"
                tt.classification_confidence = 1.0
                tt.cpa_m = 0.0
                tt.tcpa_s = 0.0
                tt.encounter = EncounterClassification()
                tt.encounter.encounter_type = 0
                tt.encounter.relative_bearing_deg = 0.0
                tt.encounter.aspect_angle_deg = 0.0
                tt.encounter.is_giveway = False
                tt.confidence = 1.0
                tt.source_sensor = "ais"

                msg.targets.append(tt)

            self._pub.publish(msg)

        def _publish_fallback(self, tracked: list[dict]):
            """Publish JSON-encoded String when real msgs are unavailable."""
            now_ns = self.get_clock().now().nanoseconds

            payload = {
                "schema_version": "v1.1.2_fallback",
                "stamp_ns": now_ns,
                "targets": [
                    {
                        "target_id": t["mmsi"],
                        "lat": t["lat"],
                        "lon": t["lon"],
                        "sog_kn": t["sog"],
                        "cog_deg": t["cog"],
                    }
                    for t in tracked
                ],
                "confidence": 1.0,
                "rationale": "mock_tracker_fallback",
            }

            msg = String()
            msg.data = json.dumps(payload)
            self._pub.publish(msg)

    def main(args: list[str] | None = None):
        rclpy.init(args=args)
        node = TrackerMockNode()
        rclpy.spin(node)
        node.destroy_node()
        rclpy.shutdown()

    if __name__ == "__main__":
        main()
