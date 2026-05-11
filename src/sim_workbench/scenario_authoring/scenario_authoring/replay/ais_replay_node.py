# scenario_authoring/replay/ais_replay_node.py
"""ROS2 node: replay AIS-derived target trajectories at 50 Hz."""
from __future__ import annotations

import importlib.util

import numpy as np


def _interpolate_trajectory_50hz(traj_2hz: np.ndarray) -> np.ndarray:
    """Resample a 2Hz trajectory (N,5) to 50Hz (M,5). COG unwrapped for interpolation.

    traj_2hz columns: [t, lat, lon, sog, cog]
    Returns: [t, lat, lon, sog, cog] at 50 Hz (dt=0.02s)
    """
    t_orig = traj_2hz[:, 0]
    t_new = np.arange(t_orig[0], t_orig[-1], 0.02)

    lat_new = np.interp(t_new, t_orig, traj_2hz[:, 1])
    lon_new = np.interp(t_new, t_orig, traj_2hz[:, 2])
    sog_new = np.interp(t_new, t_orig, traj_2hz[:, 3])

    cog_rad = np.radians(traj_2hz[:, 4])
    cog_unwrapped = np.unwrap(cog_rad)
    cog_interp_rad = np.interp(t_new, t_orig, cog_unwrapped)
    cog_new = np.degrees(cog_interp_rad) % 360.0

    return np.column_stack([t_new, lat_new, lon_new, sog_new, cog_new])


class AisReplayNode:
    """ROS2 node replaying AIS target ship trajectories at 50 Hz onto /world_model/tracks.

    Note: This class requires ROS2 rclpy at runtime. The _interpolate_trajectory_50hz
    function is pure numpy and can be tested without ROS2.
    """

    def __init__(self) -> None:
        self._rclpy_available = importlib.util.find_spec("rclpy") is not None

    def _init_ros2(self, yaml_path: str | None = None) -> None:
        """Initialize ROS2 node (called after constructor if ROS2 available)."""
        from rclpy.node import Node  # noqa: F401 — used as base class below

        class _Node(Node):
            def __init__(self):
                super().__init__("ais_replay_node")
                from l3_external_msgs.msg import TrackedTargetArray
                self._pub = self.create_publisher(TrackedTargetArray, "/world_model/tracks", 10)
                self._replayer = None
                self._t_elapsed = 0.0
                self._dt = 1.0 / 50.0

                if yaml_path:
                    self._load_scenario(yaml_path)

                self._timer = self.create_timer(self._dt, self._publish_tick)
                self.get_logger().info("ais_replay_node started at 50 Hz")

            def _load_scenario(self, yaml_path: str) -> None:
                import yaml
                with open(yaml_path) as f:
                    data = yaml.safe_load(f)
                for ts_data in data.get("target_ships", []):
                    ts_init = ts_data["initial"]
                    ts_pos = ts_init["position"]
                    t = np.arange(0.0, 600.0, 0.5)
                    lat = np.full(len(t), ts_pos["latitude"])
                    lon = np.full(len(t), ts_pos["longitude"])
                    sog = np.full(len(t), ts_init.get("sog", 10.0))
                    cog = np.full(len(t), ts_init.get("cog", 0.0))
                    traj_2hz = np.column_stack([t, lat, lon, sog, cog])
                    traj_50hz = _interpolate_trajectory_50hz(traj_2hz)
                    from scenario_authoring.replay.target_modes import AisReplayVessel
                    self._replayer = AisReplayVessel(traj_50hz)
                    break

            def _publish_tick(self) -> None:
                if self._replayer is None:
                    return
                state = self._replayer.get_targets_at(self._t_elapsed)
                if state is None:
                    return
                from geographic_msgs.msg import GeoPoint
                from l3_external_msgs.msg import TrackedTargetArray
                from l3_msgs.msg import EncounterClassification, TrackedTarget
                msg = TrackedTargetArray()
                msg.schema_version = "v1.1.2"
                msg.confidence = 0.7
                msg.rationale = "ais_replay_node"
                t = TrackedTarget()
                t.schema_version = "v1.1.2"
                t.target_id = 0
                pos = GeoPoint()
                pos.latitude = state["lat"]
                pos.longitude = state["lon"]
                pos.altitude = 0.0
                t.position = pos
                t.sog_kn = state["sog_kn"]
                t.cog_deg = state["cog_deg"]
                t.heading_deg = state["cog_deg"]
                t.classification = "vessel"
                t.classification_confidence = 0.7
                t.source_sensor = "ais"
                t.cpa_m = 0.0
                t.tcpa_s = 0.0
                t.encounter = EncounterClassification()
                t.confidence = 0.7
                msg.targets = [t]
                self._pub.publish(msg)
                self._t_elapsed += self._dt

        self._ros_node = _Node()


def main(args: list[str] | None = None) -> None:
    import rclpy
    rclpy.init(args=args)
    node = AisReplayNode()
    try:
        rclpy.spin(node._ros_node)
    except KeyboardInterrupt:
        pass
    finally:
        node._ros_node.destroy_node()
        rclpy.try_shutdown()


if __name__ == "__main__":
    main()
