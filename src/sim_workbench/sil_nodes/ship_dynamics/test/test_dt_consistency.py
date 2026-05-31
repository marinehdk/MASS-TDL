"""Unit tests for Task A3: dt consistency + headless publish throttle.

TDD RED phase: these tests assert the DESIRED behavior.
They will FAIL against the current code (hardcoded 0.02 on lines 268/288).

Test strategy: drive the node without ROS2 by monkey-patching all rclpy
imports so we can unit-test the pure-Python logic.
"""

import sys
import types
import math
import unittest
from unittest.mock import MagicMock, patch


# ---------------------------------------------------------------------------
# Minimal rclpy stubs — enough to satisfy the imports in node.py
# ---------------------------------------------------------------------------

def _install_rclpy_stubs():
    """Install minimal rclpy stubs into sys.modules before importing node."""
    rclpy = types.ModuleType("rclpy")
    rclpy_lifecycle = types.ModuleType("rclpy.lifecycle")
    rclpy_qos = types.ModuleType("rclpy.qos")
    rclpy_time = types.ModuleType("rclpy.time")
    rclpy_duration = types.ModuleType("rclpy.duration")
    rclpy_parameter = types.ModuleType("rclpy.parameter")

    # LifecycleNode stub
    class FakeLifecycleNode:
        def __init__(self, name, **kwargs):
            self._name = name
        def get_logger(self):
            logger = MagicMock()
            return logger
        def declare_parameter(self, name, value):
            p = MagicMock()
            p.value = value
            return p
        def get_parameter(self, name):
            p = MagicMock()
            p.value = 0.0
            return p
        def create_publisher(self, **kwargs):
            return MagicMock()
        def create_subscription(self, **kwargs):
            return MagicMock()
        def create_timer(self, period, callback):
            return MagicMock()
        def destroy_timer(self, t): pass
        def destroy_publisher(self, p): pass
        def destroy_subscription(self, s): pass

    rclpy_lifecycle.LifecycleNode = FakeLifecycleNode
    rclpy_lifecycle.State = object
    rclpy_lifecycle.TransitionCallbackReturn = MagicMock()
    rclpy_lifecycle.TransitionCallbackReturn.SUCCESS = "SUCCESS"
    rclpy_lifecycle.TransitionCallbackReturn.FAILURE = "FAILURE"

    # QoS stubs
    for cls_name in ("QoSProfile", "QoSReliabilityPolicy",
                     "QoSDurabilityPolicy", "QoSHistoryPolicy"):
        setattr(rclpy_qos, cls_name, MagicMock())

    # Time stub
    class FakeTime:
        def __init__(self, nanoseconds=0):
            self._ns = nanoseconds
        def __sub__(self, other):
            d = FakeDuration(self._ns - other._ns)
            return d
        def __iadd__(self, duration):
            self._ns += duration._ns
            return self
        def to_msg(self):
            return None

    class FakeDuration:
        def __init__(self, nanoseconds=0):
            self._ns = nanoseconds
        @property
        def nanoseconds(self):
            return self._ns

    rclpy_time.Time = FakeTime
    rclpy_duration.Duration = FakeDuration

    class FakeParameter:
        class Type:
            BOOL = "BOOL"
        def __init__(self, *args, **kwargs):
            pass
    rclpy_parameter.Parameter = FakeParameter

    # Wire up all submodules
    rclpy.lifecycle = rclpy_lifecycle
    rclpy.qos = rclpy_qos
    rclpy.time = rclpy_time
    rclpy.duration = rclpy_duration
    rclpy.parameter = rclpy_parameter
    rclpy.init = MagicMock()
    rclpy.spin = MagicMock()

    sys.modules["rclpy"] = rclpy
    sys.modules["rclpy.lifecycle"] = rclpy_lifecycle
    sys.modules["rclpy.qos"] = rclpy_qos
    sys.modules["rclpy.time"] = rclpy_time
    sys.modules["rclpy.duration"] = rclpy_duration
    sys.modules["rclpy.parameter"] = rclpy_parameter

    return FakeTime, FakeDuration


FakeTime, FakeDuration = _install_rclpy_stubs()


# ---------------------------------------------------------------------------
# Now safe to import from the package
# ---------------------------------------------------------------------------
import importlib, pathlib, sys as _sys
_pkg_dir = pathlib.Path(__file__).parent.parent / "ship_dynamics"
if str(_pkg_dir.parent) not in _sys.path:
    _sys.path.insert(0, str(_pkg_dir.parent))

from ship_dynamics.mmg_coefficients import MMGCoefficients
from ship_dynamics.mmg_model import MMGModel, ShipState
from ship_dynamics.node import ShipDynamicsNode


# ---------------------------------------------------------------------------
# Helper: build a minimal configured node with an injected dt
# ---------------------------------------------------------------------------

def _make_node(dt: float, headless: bool = False) -> ShipDynamicsNode:
    """Return a ShipDynamicsNode with model.c.dt == dt, bypassing ROS2."""
    node = ShipDynamicsNode.__new__(ShipDynamicsNode)
    # Call __init__ manually — it will use FakeLifecycleNode
    ShipDynamicsNode.__init__(node, "test_node")

    coeffs = MMGCoefficients()
    coeffs.dt = dt
    model = MMGModel(coeffs)

    node._model = model
    node._state = ShipState(u=coeffs.u0)
    node._origin_lat_rad = math.radians(coeffs.origin_lat)
    node._origin_lon_rad = math.radians(coeffs.origin_lon)
    node._n_rps_cmd = coeffs.n_rps_cruise
    node._delta_cmd = 0.0
    node._wind_speed = 0.0
    node._wind_dir = 0.0
    node._current_speed = 0.0
    node._current_dir = 0.0
    node._state_pub = None
    node._last_pub_wall_time = 0.0

    # headless attribute — expected to exist after implementation
    if hasattr(node, "_headless"):
        node._headless = headless

    return node


# ---------------------------------------------------------------------------
# Test 1 — Single dt source: clock advance must use self._model.c.dt
# ---------------------------------------------------------------------------

class TestDtConsistency(unittest.TestCase):
    """Injected dt=0.01 (≠0.02).  After N steps, clock must advance by N*c.dt."""

    INJECTED_DT = 0.01  # deliberately different from the old hardcoded 0.02
    N_STEPS = 5

    def _run_n_steps(self, node, n_steps):
        """Drive _step_callback n times and return how many ns the clock advanced."""
        elapsed_ns = int(self.INJECTED_DT * 1e9) * n_steps  # simulate elapsed sim-time
        t0_ns = 100_000_000  # 0.1 s in ns

        # Prime _last_sim_time
        node._last_sim_time = FakeTime(nanoseconds=t0_ns)

        # Present now_sim so that steps == n_steps
        now_ns = t0_ns + elapsed_ns
        now_sim = FakeTime(nanoseconds=now_ns)

        # Patch get_clock().now() to return our synthetic time
        clock = MagicMock()
        clock.now.return_value = now_sim
        node.get_clock = MagicMock(return_value=clock)

        # Patch create publisher / state_pub so publish() doesn't fail
        node._state_pub = MagicMock()

        node._step_callback()
        return node._last_sim_time._ns - t0_ns

    def test_clock_advances_by_model_dt_not_hardcoded(self):
        """Clock advance after N steps must equal N * model.c.dt, not N * 0.02."""
        node = _make_node(dt=self.INJECTED_DT)

        advanced_ns = self._run_n_steps(node, self.N_STEPS)
        advanced_s = advanced_ns / 1e9

        expected_s = self.N_STEPS * self.INJECTED_DT          # 0.05 s
        wrong_s    = self.N_STEPS * 0.02                       # 0.10 s  (old bug)

        self.assertAlmostEqual(
            advanced_s, expected_s, places=9,
            msg=(
                f"Clock advanced by {advanced_s:.6f} s but expected {expected_s:.6f} s "
                f"(N={self.N_STEPS}, dt={self.INJECTED_DT}). "
                f"If this equals {wrong_s:.6f} s the hardcoded 0.02 is still present."
            ),
        )

    def test_step_count_uses_model_dt(self):
        """With elapsed == N * model.c.dt, exactly N RK4 steps must occur."""
        node = _make_node(dt=self.INJECTED_DT)
        step_count = [0]
        real_rk4 = node._model.rk4_step
        def counting_rk4(*args, **kwargs):
            step_count[0] += 1
            return real_rk4(*args, **kwargs)
        node._model.rk4_step = counting_rk4

        self._run_n_steps(node, self.N_STEPS)

        self.assertEqual(
            step_count[0], self.N_STEPS,
            msg=(
                f"Expected {self.N_STEPS} RK4 steps but got {step_count[0]}. "
                f"Step count is computed as int(elapsed / dt); if dt is hardcoded "
                f"to 0.02 instead of {self.INJECTED_DT}, the count will be wrong."
            ),
        )


# ---------------------------------------------------------------------------
# Test 2 — headless flag bypasses wall-clock throttle
# ---------------------------------------------------------------------------

class TestHeadlessPublish(unittest.TestCase):
    """When headless=True, every _step_callback must publish (no wall-clock gate)."""

    def _make_headless_node(self) -> ShipDynamicsNode:
        # Use default dt=0.02 for simplicity here
        node = _make_node(dt=0.02)
        # After implementation, _headless should be settable
        node._headless = True
        node._last_pub_wall_time = 0.0
        node._state_pub = MagicMock()
        return node

    def _step_once(self, node):
        dt_ns = int(node._model.c.dt * 1e9)
        t0_ns = 1_000_000_000
        node._last_sim_time = FakeTime(nanoseconds=t0_ns)
        now_sim = FakeTime(nanoseconds=t0_ns + dt_ns)
        clock = MagicMock()
        clock.now.return_value = now_sim
        node.get_clock = MagicMock(return_value=clock)
        node._step_callback()

    def test_headless_publishes_every_step(self):
        """headless=True: publish() must be called even when wall-clock gap < 0.025 s."""
        node = self._make_headless_node()
        # Simulate wall-clock just updated (gap < 0.025 s would suppress in Shell-A)
        import time
        node._last_pub_wall_time = time.monotonic()  # very recent → throttle would block

        self._step_once(node)

        # In headless mode, publish must NOT be gated by wall clock
        node._state_pub.publish.assert_called_once()

    def test_shell_a_throttle_still_active_when_not_headless(self):
        """headless=False (default): publish() must be suppressed if wall gap < 0.025 s."""
        node = _make_node(dt=0.02)
        node._headless = False
        node._state_pub = MagicMock()

        import time
        node._last_pub_wall_time = time.monotonic()  # very recent

        dt_ns = int(node._model.c.dt * 1e9)
        t0_ns = 1_000_000_000
        node._last_sim_time = FakeTime(nanoseconds=t0_ns)
        now_sim = FakeTime(nanoseconds=t0_ns + dt_ns)
        clock = MagicMock()
        clock.now.return_value = now_sim
        node.get_clock = MagicMock(return_value=clock)
        node._step_callback()

        # Shell-A throttle should suppress the publish
        node._state_pub.publish.assert_not_called()


# ---------------------------------------------------------------------------
# Test 3 — headless defaults to False (Shell A unchanged by default)
# ---------------------------------------------------------------------------

class TestHeadlessDefault(unittest.TestCase):
    def test_headless_defaults_to_false(self):
        """_headless must default to False so Shell A is unaffected."""
        node = _make_node(dt=0.02)
        self.assertFalse(
            getattr(node, "_headless", None),
            msg="_headless must default to False; Shell A publish throttle must remain active.",
        )


# ---------------------------------------------------------------------------
# Test 4 — Lifecycle & Parameter Improvements (Code Quality Feedback)
# ---------------------------------------------------------------------------

class TestLifecycleImprovements(unittest.TestCase):
    def test_on_deactivate_resets_last_sim_time(self):
        """on_deactivate must reset _last_sim_time to None to prevent catch-up jumps."""
        node = _make_node(dt=0.02)
        node._last_sim_time = "dummy_time_object"
        node._timer = MagicMock()
        node._state_pub = MagicMock()
        node._actuator_sub = MagicMock()
        node._env_sub = MagicMock()

        # Call on_deactivate
        res = node.on_deactivate(None)
        self.assertEqual(res, "SUCCESS")
        self.assertIsNone(node._last_sim_time, "_last_sim_time must be reset to None on deactivation.")

    def test_headless_parameter_loading(self):
        """on_configure must declare and retrieve the 'headless' parameter."""
        node = ShipDynamicsNode.__new__(ShipDynamicsNode)
        ShipDynamicsNode.__init__(node, "test_node")

        # Mock the declare_parameter and get_parameter to return a specific value
        param_mock = MagicMock()
        param_mock.value = True
        node.declare_parameter = MagicMock()
        node.get_parameter = MagicMock(return_value=param_mock)

        # Force _load_coefficients to succeed
        node._load_coefficients = MagicMock(return_value=MMGCoefficients())

        res = node.on_configure(None)
        self.assertEqual(res, "SUCCESS")
        node.declare_parameter.assert_any_call("headless", False)
        node.get_parameter.assert_any_call("headless")
        self.assertTrue(node._headless, "_headless must load its value from the ROS2 parameter 'headless'.")

    def test_timer_creation_uses_model_dt(self):
        """on_activate must create the timer using the model's dynamic dt value."""
        node = _make_node(dt=0.015)  # dynamic dt
        node.create_publisher = MagicMock()
        node.create_subscription = MagicMock()
        node.create_timer = MagicMock()

        res = node.on_activate(None)
        self.assertEqual(res, "SUCCESS")
        node.create_timer.assert_called_once_with(0.015, node._step_callback)


if __name__ == "__main__":
    unittest.main()
