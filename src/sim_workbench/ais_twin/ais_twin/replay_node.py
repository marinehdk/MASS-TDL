from __future__ import annotations


from pathlib import Path
from typing import Any

from ais_twin.replay_engine import load_track_segments_csv, replay_payloads_at
from ais_twin.route import load_route_points


def payloads_to_msg(payloads: list[dict[str, Any]], stamp):
    from geographic_msgs.msg import GeoPoint
    from l3_external_msgs.msg import TrackedTargetArray
    from l3_msgs.msg import EncounterClassification, TrackedTarget

    out = TrackedTargetArray()
    out.schema_version = 112
    out.stamp = stamp
    out.confidence = 0.85
    out.rationale = "ais_twin_replay"
    for payload in payloads:
        target = TrackedTarget()
        target.schema_version = 112
        target.stamp = stamp
        target.target_id = int(payload["target_id"])
        target.position = GeoPoint(latitude=float(payload["lat"]), longitude=float(payload["lon"]), altitude=0.0)
        target.sog_kn = float(payload["sog_kn"])
        target.cog_deg = float(payload["cog_deg"])
        target.heading_deg = float(payload["heading_deg"])
        for i in range(3):
            target.covariance[i * 3 + i] = 1.0
        target.classification = "vessel"
        target.classification_confidence = 0.85
        target.cpa_m = float(payload["cpa_m"])
        target.tcpa_s = float(payload["tcpa_s"])
        target.encounter = EncounterClassification()
        target.confidence = 0.85
        target.rationale = str(payload["rationale"])
        target.source_sensor = "ais"
        out.targets.append(target)
    return out


class AisTwinReplayNode:
    def __init__(self):
        from l3_external_msgs.msg import TrackedTargetArray
        from rclpy.node import Node

        class _Node(Node):
            def __init__(self):
                super().__init__("ais_twin_replay_node")
                self.declare_parameter("dataset_dir", "data/ais_twin/safe_route")
                self.declare_parameter("route_path", "scenarios/集成测试/safe_route.yaml")
                self.declare_parameter("top_n", 20)
                self.declare_parameter("publish_hz", 2.0)

                dataset_dir = str(self.get_parameter("dataset_dir").value)
                route_path = str(self.get_parameter("route_path").value)
                self._top_n = int(self.get_parameter("top_n").value)
                self._publish_hz = float(self.get_parameter("publish_hz").value)
                self._dt_s = 1.0 / self._publish_hz
                self._sim_elapsed_s = 0.0
                self._segments = load_track_segments_csv(Path(dataset_dir) / "tracks.csv")
                self._own_route = load_route_points(Path(route_path))
                self.publisher = self.create_publisher(TrackedTargetArray, "/fusion/tracked_targets", 10)
                self.timer = self.create_timer(self._dt_s, self._publish_replay_targets)

            def _publish_replay_targets(self):
                if not self._segments:
                    return
                payloads = replay_payloads_at(
                    self._own_route,
                    self._segments,
                    self._sim_elapsed_s,
                    self._top_n,
                )
                stamp = self.get_clock().now().to_msg()
                self.publisher.publish(payloads_to_msg(payloads, stamp))
                self._sim_elapsed_s += self._dt_s

        self.ros_node = _Node()


def main(args=None):
    import rclpy

    rclpy.init(args=args)
    wrapper = AisTwinReplayNode()
    try:
        rclpy.spin(wrapper.ros_node)
    finally:
        wrapper.ros_node.destroy_node()
        rclpy.shutdown()
