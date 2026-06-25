"""Verify sil_topic_bridge wires scenario_loaded to a DEFERRED cross-run reset.

Pure file inspection (no rclpy import) so it runs on the host without ROS.

Design: same deferred pattern as the L4 adapter — the scenario_loaded callback
must NOT call the reset directly. The bridge runs under a MultiThreadedExecutor
and TRANSIENT_LOCAL fires during __init__ before latch fields exist, racing the
autopilot timer. The callback only sets a boolean flag; the autopilot timer
checks the flag and runs the reset. See commit d6723266 revert rationale.
"""
import re
from pathlib import Path


def _bridge_src() -> str:
    for p in ("docker/sil_topic_bridge.py",
              "src/sim_workbench/sil_nodes/docker/sil_topic_bridge.py"):
        if Path(p).exists():
            return Path(p).read_text()
    raise FileNotFoundError("sil_topic_bridge.py not found")


def test_bridge_subscribes_scenario_loaded():
    src = _bridge_src()
    assert "/sil/scenario_loaded" in src, \
        "sil_topic_bridge must subscribe /sil/scenario_loaded"


def test_bridge_has_scenario_loaded_callback():
    src = _bridge_src()
    assert "_on_scenario_loaded" in src, \
        "sil_topic_bridge must have an _on_scenario_loaded callback"


def test_bridge_callback_sets_flag_not_resets_directly():
    """The callback must set a pending flag, not call the reset directly."""
    src = _bridge_src()
    m = re.search(
        r"def _on_scenario_loaded\(self[^)]*\)[^:]*:(.*?)(?=\n    def |\nclass |\Z)",
        src, re.S)
    assert m, "_on_scenario_loaded method not found"
    body = m.group(1)
    # Must NOT directly call any reset method from the callback.
    assert "_reset_autopilot_avoidance_state()" not in body, \
        "_on_scenario_loaded must NOT call _reset_autopilot_avoidance_state directly (deferred pattern)"
    assert "_reset_cross_run_state()" not in body, \
        "_on_scenario_loaded must NOT call _reset_cross_run_state directly (deferred pattern)"
    assert "pending" in body.lower() or "flag" in body.lower(), \
        "_on_scenario_loaded must set a pending/flag boolean"


def test_bridge_deferred_reset_runs_in_autopilot_step():
    """The autopilot timer must check the pending flag and run the reset."""
    src = _bridge_src()
    m = re.search(
        r"def _autopilot_step\(self[^)]*\)[^:]*:(.*?)(?=\n    def |\nclass |\Z)",
        src, re.S)
    assert m, "_autopilot_step method not found"
    body = m.group(1)
    assert "pending" in body.lower() or "flag" in body.lower(), \
        "_autopilot_step must check the scenario-reset pending flag"
    assert "_reset_autopilot_avoidance_state()" in body, \
        "_autopilot_step must call _reset_autopilot_avoidance_state when the flag is set"


def test_bridge_clears_plan_and_behavior_residual():
    """Reset must cover the plan/ODD/behavior caches, not just autopilot."""
    src = _bridge_src()
    for field in ("_last_valid_plan_time", "_last_odd_state",
                  "_last_behavior_plan"):
        assert field in src, f"reset must clear {field}"


def test_bridge_pending_flag_initialized_before_subscription():
    """The flag must be initialized to False BEFORE the subscription is created."""
    src = _bridge_src()
    flag_init_pos = src.find("_scenario_reset_pending = False")
    sub_pos = src.find('"/sil/scenario_loaded"')
    assert flag_init_pos != -1, \
        "_scenario_reset_pending = False initialization not found"
    assert sub_pos != -1, "scenario_loaded subscription not found"
    assert flag_init_pos < sub_pos, \
        "flag must be initialized before the subscription is created"
