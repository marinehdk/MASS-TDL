"""Verify L4 guidance adapter wires scenario_loaded to a DEFERRED cross-run reset.

Pure file inspection (no rclpy import) so it runs on the host without ROS.

Design: the scenario_loaded callback must NOT call _reset_state directly. Both
nodes are lock-free under a MultiThreadedExecutor, and a TRANSIENT_LOCAL
subscription fires during __init__ before latch fields are initialized, and
races the autopilot timer. The deferred pattern: the callback only sets a
boolean flag; the autopilot timer (single-threaded periodic step) checks the
flag and runs _reset_state. This avoids the construction-period race entirely.
"""
import re
from pathlib import Path


def _node_src() -> str:
    return Path(
        "src/sim_workbench/sil_nodes/l4_guidance_adapter/"
        "l4_guidance_adapter/node.py").read_text()


def test_l4_subscribes_scenario_loaded():
    src = _node_src()
    assert "/sil/scenario_loaded" in src, \
        "L4 guidance adapter must subscribe /sil/scenario_loaded"


def test_l4_has_scenario_loaded_callback():
    src = _node_src()
    assert "_on_scenario_loaded" in src, \
        "L4 guidance adapter must have an _on_scenario_loaded callback"


def test_l4_callback_sets_flag_not_resets_directly():
    """The callback must set a pending flag, not call _reset_state directly.

    Calling _reset_state from the subscription callback races the autopilot
    timer and crashes during __init__ (TRANSIENT_LOCAL fires before latch
    fields are initialized). See commit d6723266 revert rationale.
    """
    src = _node_src()
    # Extract the _on_scenario_loaded method body.
    m = re.search(
        r"def _on_scenario_loaded\(self[^)]*\)[^:]*:(.*?)(?=\n    def |\nclass |\Z)",
        src, re.S)
    assert m, "_on_scenario_loaded method not found"
    body = m.group(1)
    # Strip docstrings/comments so the check targets actual call statements,
    # not text that merely mentions the method name.
    code_lines = []
    for line in body.splitlines():
        stripped = line.strip()
        if stripped.startswith('"""') or stripped.startswith("#"):
            continue
        code_lines.append(line)
    code_only = "\n".join(code_lines)
    assert "self._reset_state(" not in code_only, \
        "_on_scenario_loaded must NOT call self._reset_state() directly (deferred pattern)"
    assert "pending" in code_only.lower() or "flag" in code_only.lower(), \
        "_on_scenario_loaded must set a pending/flag boolean"


def test_l4_deferred_reset_runs_in_autopilot_step():
    """The autopilot timer must check the pending flag and run _reset_state."""
    src = _node_src()
    m = re.search(
        r"def _autopilot_step\(self[^)]*\)[^:]*:(.*?)(?=\n    def |\nclass |\Z)",
        src, re.S)
    assert m, "_autopilot_step method not found"
    body = m.group(1)
    assert "pending" in body.lower() or "flag" in body.lower(), \
        "_autopilot_step must check the scenario-reset pending flag"
    assert "_reset_state" in body, \
        "_autopilot_step must call _reset_state when the flag is set"


def test_l4_reset_uses_clear_route_false():
    """Route is injected separately; reset must not clear it."""
    src = _node_src()
    assert "_reset_state(clear_route=False)" in src, \
        "scenario_loaded reset must use clear_route=False"


def test_l4_pending_flag_initialized_before_subscription():
    """The flag must be initialized to False BEFORE the subscription is created.

    Otherwise the TRANSIENT_LOCAL callback (which fires during create_subscription)
    reads an uninitialized attribute.
    """
    src = _node_src()
    flag_init_pos = src.find("_scenario_reset_pending = False")
    sub_pos = src.find('"/sil/scenario_loaded"')
    assert flag_init_pos != -1, \
        "_scenario_reset_pending = False initialization not found"
    assert sub_pos != -1, "scenario_loaded subscription not found"
    assert flag_init_pos < sub_pos, \
        "flag must be initialized before the subscription is created"
