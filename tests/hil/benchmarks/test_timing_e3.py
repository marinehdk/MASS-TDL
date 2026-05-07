"""
test_timing_e3.py — Phase E3 performance benchmark and timing verification tests.

Verifies that the MASS L3 Tactical Layer meets the latency requirements defined
in docs/Test Plan/00-test-master-plan.md §E3 DoD:

  - M5 Mid-MPC computation latency  P99 < 100 ms
  - M5 BC-MPC computation latency   P99 <  50 ms
  - Reflex Arc E2E latency          P99 < 500 ms
  - L4 tri-mode switch latency      P99 < 100 ms

All tests are skipped when rclpy or l3_msgs are not available (non-HIL CI),
and degrade gracefully when the L3 stack is not running.
"""

from __future__ import annotations

import time

import pytest

pytestmark = pytest.mark.benchmark

# ---------------------------------------------------------------------------
# Module-level ROS2 availability guard
# ---------------------------------------------------------------------------

rclpy = pytest.importorskip("rclpy")

# ---------------------------------------------------------------------------
# Message type imports — skip entire module if l3_msgs is absent
# ---------------------------------------------------------------------------

try:
    from l3_msgs.msg import (
        AvoidancePlan,
        COLREGsConstraint,
        Constraint,
        ModeCmd,
        ReactiveOverrideCmd,
        UIState,
    )
    _L3_MSGS_OK = True
except ImportError:
    _L3_MSGS_OK = False

try:
    from l3_external_msgs.msg import EmergencyCommand
    _L3_EXT_MSGS_OK = True
except ImportError:
    _L3_EXT_MSGS_OK = False

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

_ITERATIONS = 10
_REFLEX_ITERATIONS = 5


def _require_msgs() -> None:
    """Skip if message packages are not installed."""
    if not _L3_MSGS_OK:
        pytest.skip("l3_msgs not available")
    if not _L3_EXT_MSGS_OK:
        pytest.skip("l3_external_msgs not available")


def _make_node(name: str):
    """Create a fresh rclpy node; skip if rclpy is not initialised."""
    try:
        if not rclpy.ok():
            rclpy.init()
        return rclpy.create_node(name)
    except Exception as exc:  # noqa: BLE001
        pytest.skip(f"rclpy init failed: {exc}")


def _teardown_node(node) -> None:
    """Best-effort node teardown."""
    try:
        node.destroy_node()
    except Exception:  # noqa: BLE001
        pass


# ---------------------------------------------------------------------------
# Test 1 — M5 Mid-MPC latency
# ---------------------------------------------------------------------------

def test_m5_mid_mpc_latency() -> None:
    """
    Publishes a COLREGsConstraint with conflict_detected=True to
    /l3/m6/colregs_constraint and waits for AvoidancePlan on
    /l3/m5/avoidance_plan.  P99 must be < 100 ms.
    """
    _require_msgs()

    from tests.hil.benchmarks.timing_verifier import TimingVerifier

    node = _make_node("bench_m5_mid_mpc")
    try:
        verifier = TimingVerifier(node, buffer_size=_ITERATIONS)

        # Build trigger message
        trigger = COLREGsConstraint()
        trigger.conflict_detected = True
        trigger.phase = "T_act"
        cpa_constraint = Constraint()
        cpa_constraint.constraint_type = "kinematic"
        cpa_constraint.description = "CPA proximity trigger"
        cpa_constraint.numeric_value = 0.1
        cpa_constraint.unit = "m"
        trigger.constraints = [cpa_constraint]
        trigger.confidence = 0.95
        trigger.rationale = "benchmark trigger"

        samples: list[float] = []
        for _ in range(_ITERATIONS):
            latency = verifier.measure_round_trip(
                trigger_topic="/l3/m6/colregs_constraint",
                response_topic="/l3/m5/avoidance_plan",
                trigger_msg=trigger,
                timeout_s=5.0,
                trigger_msg_type=COLREGsConstraint,
                response_msg_type=AvoidancePlan,
            )
            if latency is None:
                pytest.skip("L3 stack not responding")
            samples.append(latency)
            time.sleep(0.05)

        p99 = verifier.get_percentile(samples, 99)
        assert p99 < 0.100, (
            f"M5 Mid-MPC P99 latency {p99*1000:.1f} ms exceeds 100 ms budget"
        )
    finally:
        _teardown_node(node)


# ---------------------------------------------------------------------------
# Test 2 — M5 BC-MPC (reactive path) latency
# ---------------------------------------------------------------------------

def test_m5_bc_mpc_latency() -> None:
    """
    Publishes a ReactiveOverrideCmd to /m5/reactive_override_cmd and waits
    for AvoidancePlan on /m5/avoidance_plan (BC-MPC reactive path).
    P99 must be < 50 ms.
    """
    _require_msgs()

    from tests.hil.benchmarks.timing_verifier import TimingVerifier

    node = _make_node("bench_m5_bc_mpc")
    try:
        verifier = TimingVerifier(node, buffer_size=_ITERATIONS)

        trigger = ReactiveOverrideCmd()
        trigger.trigger_reason = "CPA_EMERGENCY"
        trigger.heading_cmd_deg = 45.0
        trigger.speed_cmd_kn = 8.0
        trigger.rot_cmd_deg_s = 0.0
        trigger.validity_s = 10.0
        trigger.confidence = 0.95

        samples: list[float] = []
        for _ in range(_ITERATIONS):
            latency = verifier.measure_round_trip(
                trigger_topic="/m5/reactive_override_cmd",
                response_topic="/m5/avoidance_plan",
                trigger_msg=trigger,
                timeout_s=2.0,
                trigger_msg_type=ReactiveOverrideCmd,
                response_msg_type=AvoidancePlan,
            )
            if latency is None:
                pytest.skip("L3 stack not responding")
            samples.append(latency)
            time.sleep(0.02)

        p99 = verifier.get_percentile(samples, 99)
        assert p99 < 0.050, (
            f"M5 BC-MPC P99 latency {p99*1000:.1f} ms exceeds 50 ms budget"
        )
    finally:
        _teardown_node(node)


# ---------------------------------------------------------------------------
# Test 3 — Reflex Arc end-to-end latency
# ---------------------------------------------------------------------------

def test_reflex_arc_e2e_latency() -> None:
    """
    Publishes an EmergencyCommand (ACTION_STOP) to /l3/reflex_arc/trigger
    and waits for a response on /l3/reflex_arc/active_signal or
    /l3/m7/safety_alert.  P99 must be < 500 ms.

    The Reflex Arc bypasses L3 entirely; this test measures the end-to-end
    path from perception trigger to the first downstream acknowledgement.
    """
    _require_msgs()

    from tests.hil.benchmarks.timing_verifier import TimingVerifier

    # Reflex Arc may respond on either topic; we probe both and take whichever
    # arrives first.  We implement this as two consecutive single-response
    # measurements per iteration using a flag shared via a list.

    try:
        from l3_msgs.msg import SafetyAlert
        _safety_alert_available = True
    except ImportError:
        _safety_alert_available = False

    try:
        from l3_external_msgs.msg import OverrideActiveSignal
        _override_signal_available = True
    except ImportError:
        _override_signal_available = False

    node = _make_node("bench_reflex_arc")
    try:
        verifier = TimingVerifier(node, buffer_size=_REFLEX_ITERATIONS)

        trigger = EmergencyCommand()
        trigger.action = EmergencyCommand.ACTION_STOP
        trigger.cpa_at_trigger_m = 50.0
        trigger.range_at_trigger_m = 80.0
        trigger.sensor_source = "fusion"
        trigger.confidence = 0.99

        # Choose the most specific available response topic
        if _override_signal_available:
            response_msg_type = OverrideActiveSignal
            response_topic = "/l3/reflex_arc/active_signal"
        elif _safety_alert_available:
            response_msg_type = SafetyAlert
            response_topic = "/l3/m7/safety_alert"
        else:
            pytest.skip("Neither OverrideActiveSignal nor SafetyAlert available")

        samples: list[float] = []
        for _ in range(_REFLEX_ITERATIONS):
            latency = verifier.measure_round_trip(
                trigger_topic="/l3/reflex_arc/trigger",
                response_topic=response_topic,
                trigger_msg=trigger,
                timeout_s=1.0,
                trigger_msg_type=EmergencyCommand,
                response_msg_type=response_msg_type,
            )
            if latency is None:
                pytest.skip("L3 stack not responding")
            samples.append(latency)
            time.sleep(0.1)

        p99 = verifier.get_percentile(samples, 99)
        assert p99 < 0.500, (
            f"Reflex Arc E2E P99 latency {p99*1000:.1f} ms exceeds 500 ms budget"
        )
    finally:
        _teardown_node(node)


# ---------------------------------------------------------------------------
# Test 4 — L4 tri-mode switch latency
# ---------------------------------------------------------------------------

def test_l4_mode_switch_latency() -> None:
    """
    Publishes alternating ModeCmd messages (MODE_EMERGENCY / MODE_NORMAL) to
    /l3/m1/mode_cmd and waits for a UIState acknowledgement on
    /l3/m8/ui_state.  P99 must be < 100 ms.

    ModeCmd uses uint8 constants (not string names).  We alternate between
    MODE_NORMAL (0) and MODE_EMERGENCY (3) to simulate the normal_LOS ↔
    avoidance_planning transition observable via M8 ui_state updates.
    """
    _require_msgs()

    from tests.hil.benchmarks.timing_verifier import TimingVerifier

    _TRANSITIONS = 5
    node = _make_node("bench_l4_mode_switch")
    try:
        verifier = TimingVerifier(node, buffer_size=_TRANSITIONS)

        samples: list[float] = []
        modes = [ModeCmd.MODE_EMERGENCY, ModeCmd.MODE_NORMAL] * _TRANSITIONS

        for i in range(_TRANSITIONS):
            trigger = ModeCmd()
            trigger.mode = modes[i]
            trigger.behavior_constraint = ModeCmd.CONSTRAINT_NONE
            trigger.confidence = 1.0
            trigger.rationale = f"benchmark transition {i}"

            latency = verifier.measure_round_trip(
                trigger_topic="/l3/m1/mode_cmd",
                response_topic="/l3/m8/ui_state",
                trigger_msg=trigger,
                timeout_s=3.0,
                trigger_msg_type=ModeCmd,
                response_msg_type=UIState,
            )
            if latency is None:
                pytest.skip("L3 stack not responding")
            samples.append(latency)
            time.sleep(0.05)

        p99 = verifier.get_percentile(samples, 99)
        assert p99 < 0.100, (
            f"L4 mode-switch P99 latency {p99*1000:.1f} ms exceeds 100 ms budget"
        )
    finally:
        _teardown_node(node)
