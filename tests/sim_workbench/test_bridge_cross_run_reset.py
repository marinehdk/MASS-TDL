"""Verify sil_topic_bridge wires scenario_loaded to a cross-run reset.

Pure file inspection (no rclpy import) so it runs on the host without ROS.
"""
from pathlib import Path


def _bridge_src() -> str:
    # docker/sil_topic_bridge.py relative to repo root
    for p in ("docker/sil_topic_bridge.py",
              "src/sim_workbench/sil_nodes/docker/sil_topic_bridge.py"):
        if Path(p).exists():
            return Path(p).read_text()
    raise FileNotFoundError("sil_topic_bridge.py not found")


def test_bridge_subscribes_scenario_loaded():
    src = _bridge_src()
    assert "/sil/scenario_loaded" in src, \
        "sil_topic_bridge must subscribe /sil/scenario_loaded"


def test_bridge_has_cross_run_reset_wired():
    src = _bridge_src()
    assert "_reset_cross_run_state" in src, \
        "sil_topic_bridge must define _reset_cross_run_state"
    assert "_on_scenario_loaded" in src, \
        "sil_topic_bridge must have an _on_scenario_loaded callback"
    # The callback must invoke the reset.
    assert "_reset_cross_run_state()" in src, \
        "scenario_loaded callback must call _reset_cross_run_state()"


def test_bridge_clears_plan_and_behavior_residual():
    """Reset must cover the plan/ODD/behavior caches, not just autopilot."""
    src = _bridge_src()
    for field in ("_last_valid_plan_time", "_last_odd_state",
                  "_last_behavior_plan"):
        assert field in src, f"reset must clear {field}"
