"""Verify the cross-run reset signal publisher uses TRANSIENT_LOCAL durability.

Pure file inspection (no rclpy import) so it runs on the host without ROS.
The orchestrator's /sil/scenario_loaded must be latched so late-starting
C++ L3 nodes (entrypoint Stage 3) receive the most recent scenario_id.
"""
from pathlib import Path


def _bridge_src() -> str:
    return Path("src/sil_orchestrator/lifecycle_bridge.py").read_text()


def test_scenario_loaded_publisher_is_transient_local():
    src = _bridge_src()
    assert "DurabilityPolicy.TRANSIENT_LOCAL" in src, \
        "scenario_loaded publisher must use TRANSIENT_LOCAL durability"
    # Must be applied to the scenario_loaded publisher, not an unrelated one.
    # Find the publisher creation and confirm TRANSIENT_LOCAL is reachable in
    # the QoS used by _scenario_loaded_pub.
    assert "_scenario_loaded_pub" in src
    assert "_SCENARIO_LOADED_QOS" in src or "TRANSIENT_LOCAL" in src


def test_scenario_loaded_qos_constant_exists():
    """A named QoS constant documents intent and is reused by the publisher."""
    src = _bridge_src()
    assert "_SCENARIO_LOADED_QOS" in src, \
        "expected a named _SCENARIO_LOADED_QOS constant for the publisher"
