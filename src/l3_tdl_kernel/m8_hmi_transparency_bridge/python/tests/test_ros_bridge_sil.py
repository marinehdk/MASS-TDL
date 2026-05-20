"""Tests for ros_bridge SIL extensions (subscriptions to SAT + ODD topics)."""
from __future__ import annotations

from unittest.mock import MagicMock

from web_server.ros_bridge import RosBridge


def test_ros_bridge_has_latest_sat_attribute() -> None:
    bridge = RosBridge()
    assert hasattr(bridge, "latest_sat")
    assert bridge.latest_sat is None


def test_ros_bridge_has_latest_odd_attribute() -> None:
    bridge = RosBridge()
    assert hasattr(bridge, "latest_odd")
    assert bridge.latest_odd is None


def test_on_sat_data_updates_latest() -> None:
    bridge = RosBridge()
    fake_msg = MagicMock()
    fake_msg.schema_version = "v1.1.2"
    bridge._on_sat_data(fake_msg)
    assert bridge.latest_sat is fake_msg


def test_on_odd_state_updates_latest() -> None:
    bridge = RosBridge()
    fake_msg = MagicMock()
    fake_msg.envelope_state = 0  # ENVELOPE_IN
    bridge._on_odd_state(fake_msg)
    assert bridge.latest_odd is fake_msg


def test_existing_is_ready_unchanged() -> None:
    bridge = RosBridge()
    assert bridge.is_ready() is False
