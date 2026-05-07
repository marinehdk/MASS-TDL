"""
conftest.py — pytest fixtures for the MASS L3 HIL end-to-end test suite.

Fixtures:
  ros2_node          — initialises rclpy, yields a generic Node, shuts down
  scenario_db        — loads scenarios_100.json as a list of dicts
  target_injector_node — creates a TargetInjectorNode, tears it down after test
"""

from __future__ import annotations

import json
from pathlib import Path
import pytest

_SCENARIOS_PATH = Path(__file__).parent / "scenarios" / "scenarios_100.json"


# ---------------------------------------------------------------------------
# ROS2 context + node
# ---------------------------------------------------------------------------

@pytest.fixture(scope="session")
def ros2_node():
    """
    Session-scoped fixture: initialise rclpy, create a generic test node,
    yield it, then shut down rclpy.

    Gracefully skips if rclpy is not installed (non-HIL CI environments).
    """
    rclpy = pytest.importorskip("rclpy")
    node = None
    try:
        if not rclpy.ok():
            rclpy.init()
        node = rclpy.create_node("hil_test_node")
        yield node
    finally:
        if node is not None:
            try:
                node.destroy_node()
            except Exception:  # noqa: BLE001
                pass
        try:
            if rclpy.ok():
                rclpy.shutdown()
        except Exception:  # noqa: BLE001
            pass


# ---------------------------------------------------------------------------
# Scenario database
# ---------------------------------------------------------------------------

@pytest.fixture(scope="session")
def scenario_db() -> list:
    """Load and return the 100-scenario curated list as plain dicts."""
    with open(_SCENARIOS_PATH, encoding="utf-8") as fh:
        return json.load(fh)


# ---------------------------------------------------------------------------
# TargetInjectorNode
# ---------------------------------------------------------------------------

@pytest.fixture
def target_injector_node(ros2_node):
    """
    Function-scoped fixture: create a TargetInjectorNode attached to
    ros2_node, yield it, then stop and destroy it on teardown.
    """
    from target_injector import TargetInjectorNode
    injector = TargetInjectorNode(ros2_node)
    yield injector
    injector.stop()
