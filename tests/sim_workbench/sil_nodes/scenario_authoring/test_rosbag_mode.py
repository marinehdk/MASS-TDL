"""rosbag mode smoke test."""
from __future__ import annotations

import sys
from pathlib import Path

import pytest

# ensure the package is importable when running tests directly (without colcon)
_pkg = (
    Path(__file__).resolve().parents[4]
    / "src"
    / "sim_workbench"
    / "sil_nodes"
    / "scenario_authoring"
)
sys.path.insert(0, str(_pkg))

try:
    import rclpy
    from scenario_authoring.node import ScenarioAuthoringNode

    HAS_ROS2 = True
except ImportError:
    HAS_ROS2 = False

_ROS2_SKIP_REASON = "rclpy not available (ROS2 not installed on this platform)"


def test_rosbag_mode_detection_no_bag(tmp_path, monkeypatch) -> None:
    if not HAS_ROS2:
        pytest.skip(_ROS2_SKIP_REASON)
    rclpy.init()
    try:
        node = ScenarioAuthoringNode(scenario_dir=str(tmp_path))
        scenario = {
            "simulation_settings": {
                "dynamics_mode": "rosbag",
                "rosbag_path": "/nonexistent/path.bag",
            }
        }
        warnings: list[str] = []
        monkeypatch.setattr(
            node,
            "get_logger",
            lambda: type("L", (), {
                "warn": lambda s, m: warnings.append(m),
                "info": lambda s, m: None,
            })(),
        )
        node._start_rosbag_playback(scenario)
        assert any("rosbag" in w.lower() for w in warnings)
        assert getattr(node, "_rosbag_proc", None) is None
        node.destroy_node()
    finally:
        rclpy.shutdown()
