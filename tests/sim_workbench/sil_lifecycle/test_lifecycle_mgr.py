"""Tests for sil_lifecycle LifecycleManagerNode.

Mock ROS2 infrastructure injected inline when rclpy is unavailable (macOS dev).
On Ubuntu with ROS2 Jazzy, these tests run with real rclpy.
"""

import os
import sys
import types
from unittest.mock import MagicMock

import pytest

# ── Path setup ──────────────────────────────────────────────────────────────

_SRC_SIL_LIFECYCLE = os.path.abspath(os.path.join(
    os.path.dirname(__file__),
    "..", "..", "..", "src", "sim_workbench", "sil_lifecycle",
))
if _SRC_SIL_LIFECYCLE not in sys.path:
    sys.path.insert(0, _SRC_SIL_LIFECYCLE)


# ── Mock ROS2 if rclpy unavailable ──────────────────────────────────────────

_ROS2_AVAILABLE = False
try:
    import rclpy  # noqa: F401
    _ROS2_AVAILABLE = True
except ImportError:
    # --- rclpy (module) ---
    rclpy_mod = types.ModuleType("rclpy")
    rclpy_mod.init = MagicMock()
    rclpy_mod.shutdown = MagicMock()
    rclpy_mod.spin = MagicMock()
    sys.modules["rclpy"] = rclpy_mod

    # --- rclpy.lifecycle (module with LifecycleNode base) ---
    lifecycle_mod = types.ModuleType("rclpy.lifecycle")

    class _MockLifecycleNode:
        def __init__(self, node_name: str = "") -> None:
            self._node_name = node_name
            self._params: dict = {}
            self._timers: list = []
            self._publishers: list = []
            self._logger = MagicMock()

        def declare_parameter(self, name: str, value) -> None:
            self._params[name] = value

        def get_parameter(self, name: str) -> MagicMock:
            mock_val = MagicMock()
            mock_val.value = self._params.get(name)
            return mock_val

        def create_publisher(self, msg_type, topic, qos_profile):
            pub = MagicMock()
            self._publishers.append(pub)
            return pub

        def create_timer(self, timer_period_sec, callback):
            timer = MagicMock()
            timer.timer_period_sec = timer_period_sec
            timer.callback = callback
            self._timers.append(timer)
            return timer

        def destroy_timer(self, timer) -> None:
            if timer in self._timers:
                self._timers.remove(timer)

        def destroy_publisher(self, pub) -> None:
            if pub in self._publishers:
                self._publishers.remove(pub)

        def get_clock(self):
            clock = MagicMock()
            clock.now.return_value.to_msg.return_value = MagicMock()
            return clock

        def get_logger(self):
            return self._logger

        def destroy_node(self) -> None:
            pass

    lifecycle_mod.LifecycleNode = _MockLifecycleNode

    # TransitionCallbackReturn with identity-checkable values
    _TCR = type("_TCR", (), {})()
    _TCR.SUCCESS = object()
    _TCR.FAILURE = object()
    _TCR.ERROR = object()
    lifecycle_mod.TransitionCallbackReturn = _TCR

    rclpy_mod.lifecycle = lifecycle_mod
    sys.modules["rclpy.lifecycle"] = lifecycle_mod

    # --- rclpy.qos (module) ---
    qos_mod = types.ModuleType("rclpy.qos")
    for enum_name in (
        "DurabilityPolicy", "HistoryPolicy", "QoSProfile", "ReliabilityPolicy",
    ):
        enum_cls = MagicMock()
        for attr in (
            "RELIABLE", "BEST_EFFORT", "TRANSIENT_LOCAL",
            "VOLATILE", "KEEP_LAST", "KEEP_ALL",
        ):
            setattr(enum_cls, attr, attr)
        setattr(qos_mod, enum_name, enum_cls)
    rclpy_mod.qos = qos_mod
    sys.modules["rclpy.qos"] = qos_mod

    # --- builtin_interfaces.msg (module) ---
    bi_mod = types.ModuleType("builtin_interfaces")
    bi_msg_mod = types.ModuleType("builtin_interfaces.msg")
    bi_msg_mod.Time = MagicMock(name="builtin_interfaces.msg.Time")
    bi_mod.msg = bi_msg_mod
    sys.modules["builtin_interfaces"] = bi_mod
    sys.modules["builtin_interfaces.msg"] = bi_msg_mod

    # --- sil_msgs.msg (module) ---
    sil_mod = types.ModuleType("sil_msgs")
    sil_msg_mod = types.ModuleType("sil_msgs.msg")
    sil_msg_mod.LifecycleStatus = MagicMock(name="sil_msgs.msg.LifecycleStatus")
    sil_mod.msg = sil_msg_mod
    sys.modules["sil_msgs"] = sil_mod
    sys.modules["sil_msgs.msg"] = sil_msg_mod

    # --- lifecycle_msgs (module) ---
    sys.modules["lifecycle_msgs"] = types.ModuleType("lifecycle_msgs")


# ── Import module under test ────────────────────────────────────────────────

from sil_lifecycle.lifecycle_mgr import (  # noqa: E402
    LifecycleManagerNode,
    LifecycleState,
    ScenarioLifecycleMgr,
    Transition,
)

MockLifecycleNode = sys.modules["rclpy.lifecycle"].LifecycleNode


# ── Fixture ─────────────────────────────────────────────────────────────────


@pytest.fixture
def node() -> LifecycleManagerNode:
    return LifecycleManagerNode()


# ── Required tests (6) ─────────────────────────────────────────────────────


def test_lifecycle_node_inherits_lifecycle_node(node: LifecycleManagerNode) -> None:
    assert isinstance(node, MockLifecycleNode)


def test_on_configure_returns_success(node: LifecycleManagerNode) -> None:
    from rclpy.lifecycle import TransitionCallbackReturn
    result = node.on_configure(None)
    assert result is TransitionCallbackReturn.SUCCESS
    assert node._fsm.current_state == LifecycleState.INACTIVE


def test_on_activate_returns_success(node: LifecycleManagerNode) -> None:
    from rclpy.lifecycle import TransitionCallbackReturn
    node.on_configure(None)
    result = node.on_activate(None)
    assert result is TransitionCallbackReturn.SUCCESS
    assert node._fsm.current_state == LifecycleState.ACTIVE


def test_on_deactivate_returns_success(node: LifecycleManagerNode) -> None:
    from rclpy.lifecycle import TransitionCallbackReturn
    node.on_configure(None)
    node.on_activate(None)
    result = node.on_deactivate(None)
    assert result is TransitionCallbackReturn.SUCCESS
    assert node._fsm.current_state == LifecycleState.INACTIVE


def test_on_cleanup_returns_success(node: LifecycleManagerNode) -> None:
    from rclpy.lifecycle import TransitionCallbackReturn
    node.on_configure(None)
    node.on_activate(None)
    node.on_deactivate(None)
    result = node.on_cleanup(None)
    assert result is TransitionCallbackReturn.SUCCESS
    assert node._fsm.current_state == LifecycleState.UNCONFIGURED


def test_has_two_timers(node: LifecycleManagerNode) -> None:
    node.on_configure(None)
    node.on_activate(None)
    assert len(node._timers) >= 2


# ── Bonus tests: enum/FSM preservation ─────────────────────────────────────


def test_lifecycle_state_enum_values() -> None:
    assert LifecycleState.UNCONFIGURED == 0
    assert LifecycleState.INACTIVE == 1
    assert LifecycleState.ACTIVE == 2
    assert LifecycleState.DEACTIVATING == 3
    assert LifecycleState.FINALIZED == 4


def test_transition_enum_values() -> None:
    assert Transition.CONFIGURE == 1
    assert Transition.ACTIVATE == 3
    assert Transition.DEACTIVATE == 4
    assert Transition.CLEANUP == 6


def test_fsm_preserved() -> None:
    fsm = ScenarioLifecycleMgr(tick_hz=1000.0)
    assert fsm.current_state == LifecycleState.UNCONFIGURED
    assert fsm.configure("test_scenario", "hash123")
    assert fsm.scenario_id == "test_scenario"
    assert fsm.scenario_hash == "hash123"
    assert fsm.current_state == LifecycleState.INACTIVE
    assert fsm.activate()
    assert fsm.current_state == LifecycleState.ACTIVE
    assert fsm.deactivate()
    assert fsm.current_state == LifecycleState.INACTIVE
    assert fsm.cleanup()
    assert fsm.current_state == LifecycleState.UNCONFIGURED
    assert fsm.scenario_id == ""


def test_fsm_tick_advances_sim_time() -> None:
    fsm = ScenarioLifecycleMgr(tick_hz=1000.0)
    fsm.configure("s", "")
    fsm.activate()
    initial = fsm.sim_time
    for _ in range(100):
        fsm.tick()
    assert fsm.sim_time == pytest.approx(initial + 0.1)
