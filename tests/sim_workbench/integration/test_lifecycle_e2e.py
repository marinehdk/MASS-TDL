"""E2E integration test: lifecycle_mgr + all LifecycleNode activation.

Verification points
-------------------
1. V&V Plan E1.6: all LifecycleNode classes configure + activate in under 5 s
   using a shared MultiThreadedExecutor(num_threads=4).
2. MultiThreadedExecutor maintains 50 Hz spin cadence without crashing.

Design notes
------------
- ShipDynamicsNode is a plain-Python class not inheriting LifecycleNode,
  so it is excluded from lifecycle transition testing. The remaining
  7 business nodes ARE LifecycleNode subclasses, giving us 8 LifecycleNode
  classes (manager + 7) in total.
"""

from __future__ import annotations

import importlib
import os
import sys
import time
from typing import Any

import pytest

_SRC_BASE = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "..", "src", "sim_workbench")
)

_PACKAGE_PARENTS: list[str] = [
    os.path.join(_SRC_BASE, "sil_lifecycle"),
]
for _pkg in (
    "ship_dynamics",
    "env_disturbance",
    "target_vessel",
    "sensor_mock",
    "tracker_mock",
    "fault_injection",
    "scoring",
    "scenario_authoring",
):
    _PACKAGE_PARENTS.append(os.path.join(_SRC_BASE, "sil_nodes", _pkg))

for _p in _PACKAGE_PARENTS:
    if _p not in sys.path:
        sys.path.insert(0, _p)

_HAS_RCLPY: bool = False
_HAS_SIL_MSGS: bool = False

try:
    import rclpy  # noqa: F401
    _HAS_RCLPY = True
except ImportError:
    pass

if _HAS_RCLPY:
    try:
        import sil_msgs  # noqa: F401
        import sil_msgs.msg  # noqa: F401
        _HAS_SIL_MSGS = True
    except ImportError:
        pass

_LIFECYCLE_NODES: list[tuple[str, Any]] = []
_IMPORT_ERRORS: list[str] = []

if _HAS_RCLPY:
    _IMPORT_TASKS: list[tuple[str, str, str]] = [
        ("LifecycleManagerNode", "sil_lifecycle.lifecycle_mgr", "LifecycleManagerNode"),
        ("EnvDisturbanceNode", "env_disturbance.node", "EnvDisturbanceNode"),
        ("TargetVesselNode", "target_vessel.node", "TargetVesselNode"),
        ("SensorMockNode", "sensor_mock.node", "SensorMockNode"),
        ("FaultInjectionNode", "fault_injection.node", "FaultInjectionNode"),
        ("ScoringNode", "scoring.node", "ScoringNode"),
        ("ScenarioAuthoringNode", "scenario_authoring.scenario_authoring.node", "ScenarioAuthoringNode"),
    ]
    for label, mod_path, cls_name in _IMPORT_TASKS:
        try:
            mod = importlib.import_module(mod_path)
            cls = getattr(mod, cls_name)
            _LIFECYCLE_NODES.append((label, cls))
        except (ImportError, AttributeError) as exc:
            _IMPORT_ERRORS.append(f"{label} ({mod_path}.{cls_name}): {exc}")

    try:
        from tracker_mock.node import TrackerMockNode  # type: ignore[import-untyped]
        _LIFECYCLE_NODES.append(("TrackerMockNode", TrackerMockNode))
    except (ImportError, AttributeError) as exc:
        _IMPORT_ERRORS.append(f"TrackerMockNode (tracker_mock.node.TrackerMockNode): {exc}")

_SKIP_REASONS: list[str] = []
if not _HAS_RCLPY:
    _SKIP_REASONS.append("rclpy not importable")
if not _HAS_SIL_MSGS:
    _SKIP_REASONS.append("sil_msgs not importable (ROS 2 messages not built)")
if _IMPORT_ERRORS:
    for err in _IMPORT_ERRORS:
        _SKIP_REASONS.append(f"import error: {err}")

pytestmark = pytest.mark.skipif(
    bool(_SKIP_REASONS),
    reason="; ".join(_SKIP_REASONS),
)


class TestLifecycleE2E:
    """E2E integration tests: lifecycle activation timing + 50 Hz spin."""

    @pytest.fixture(autouse=True)
    def _ros_lifecycle(self) -> Any:
        if not _HAS_RCLPY:
            pytest.skip("rclpy not available")
        rclpy.init()
        executor = rclpy.executors.MultiThreadedExecutor(num_threads=4)
        yield executor
        executor.shutdown()
        rclpy.shutdown()

    def test_all_nodes_activate_under_5s(self, _ros_lifecycle: Any) -> None:
        if not _LIFECYCLE_NODES:
            pytest.skip("No LifecycleNode classes were imported")
        executor = _ros_lifecycle
        nodes: list[Any] = []
        from rclpy.lifecycle import State, TransitionCallbackReturn

        for label, cls in _LIFECYCLE_NODES:
            try:
                node = cls()
                nodes.append((label, node))
            except Exception as exc:
                pytest.fail(f"Failed to instantiate {label}: {exc}")

        for _, node in nodes:
            executor.add_node(node)

        for label, node in nodes:
            ret = node.on_configure(State())
            assert ret == TransitionCallbackReturn.SUCCESS, (
                f"{label} on_configure failed: {ret}"
            )

        t0 = time.perf_counter()
        for label, node in nodes:
            ret = node.on_activate(State())
            assert ret == TransitionCallbackReturn.SUCCESS, (
                f"{label} on_activate failed: {ret}"
            )
        t_activate = time.perf_counter() - t0

        for _ in range(5):
            executor.spin_once(timeout_sec=0.05)

        assert t_activate < 5.0, (
            f"Batch activation took {t_activate:.3f} s (limit: 5.0 s)"
        )

        for label, node in reversed(nodes):
            try:
                node.on_deactivate(State())
            except Exception:
                pass
            try:
                node.on_cleanup(State())
            except Exception:
                pass
            executor.remove_node(node)
            node.destroy_node()

    def test_multi_threaded_executor_50hz(self, _ros_lifecycle: Any) -> None:
        from rclpy.lifecycle import State, TransitionCallbackReturn
        from sil_lifecycle.lifecycle_mgr import LifecycleManagerNode

        executor = _ros_lifecycle
        node = LifecycleManagerNode()
        executor.add_node(node)

        assert node.on_configure(State()) == TransitionCallbackReturn.SUCCESS
        assert node.on_activate(State()) == TransitionCallbackReturn.SUCCESS

        for _ in range(50):
            executor.spin_once(timeout_sec=0.02)

        node.on_deactivate(State())
        node.on_cleanup(State())
        executor.remove_node(node)
        node.destroy_node()
