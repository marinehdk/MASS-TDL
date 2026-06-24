"""Verify L4 guidance adapter wires scenario_loaded to its existing _reset_state.

Pure file inspection (no rclpy import) so it runs on the host without ROS.
"""
from pathlib import Path


def _node_src() -> str:
    return Path(
        "src/sim_workbench/sil_nodes/l4_guidance_adapter/"
        "l4_guidance_adapter/node.py").read_text()


def test_l4_subscribes_scenario_loaded():
    src = _node_src()
    assert "/sil/scenario_loaded" in src, \
        "L4 guidance adapter must subscribe /sil/scenario_loaded"


def test_l4_reset_uses_clear_route_false():
    """Route is injected separately; reset must not clear it."""
    src = _node_src()
    assert "_reset_state(clear_route=False)" in src, \
        "scenario_loaded reset must use clear_route=False"


def test_l4_has_scenario_loaded_callback():
    src = _node_src()
    assert "_on_scenario_loaded" in src, \
        "L4 guidance adapter must have an _on_scenario_loaded callback"
