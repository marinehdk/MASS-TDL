"""
test_hil_e2e.py — pytest HIL end-to-end tests for the MASS L3 Tactical Layer.

Runs 100 representative COLREGs scenarios against the full L3 stack (M1–M8)
and evaluates pass/fail against expected COLREGs rule + role + action criteria.

All tests are marked @pytest.mark.hil so they can be skipped in non-HIL CI
environments with:  pytest -m "not hil"

rclpy is an optional import — the entire module is skipped when rclpy is not
available (e.g. pure unit-test environments).
"""

from __future__ import annotations

import dataclasses
import json
from pathlib import Path
from typing import List

import pytest

pytestmark = pytest.mark.hil

# Skip entire module when ROS2 is not installed
rclpy = pytest.importorskip("rclpy")

from scenario_schema import (  # noqa: E402  (after importorskip guard)
    ExpectedOutcome,
    OwnShipInit,
    Scenario,
    TargetShipInit,
)
from hil_runner import HilResult, HilScenarioRunner  # noqa: E402

_SCENARIOS_PATH = Path(__file__).parent / "scenarios" / "scenarios_100.json"
_TIMEOUT_S = 30.0
_PASS_RATE_THRESHOLD = 0.95


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _load_scenarios() -> List[Scenario]:
    """Deserialise scenarios_100.json into Scenario dataclasses."""
    with open(_SCENARIOS_PATH, encoding="utf-8") as fh:
        raw_list = json.load(fh)

    scenarios: List[Scenario] = []
    for d in raw_list:
        own = OwnShipInit(**d["own_ship"])
        targets = [TargetShipInit(**t) for t in d["targets"]]
        expected = ExpectedOutcome(**d["expected"])
        scenario = Scenario(
            scenario_id=d["scenario_id"],
            colregs_rule=d["colregs_rule"],
            description=d["description"],
            odd_visibility_nm=d["odd_visibility_nm"],
            odd_mode=d["odd_mode"],
            own_ship=own,
            targets=targets,
            expected=expected,
            tags=d.get("tags", []),
        )
        scenarios.append(scenario)
    return scenarios


def _run_all(ros2_node, scenarios: List[Scenario]) -> List[HilResult]:
    runner = HilScenarioRunner(ros2_node)
    results = []
    try:
        for scenario in scenarios:
            result = runner.run_scenario(scenario, timeout_s=_TIMEOUT_S)
            results.append(result)
    finally:
        runner.destroy()
    return results


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

@pytest.mark.hil
def test_hil_100_scenarios(ros2_node):
    """
    Run all 100 scenarios and assert overall pass rate >= 95%.

    Failure summary is printed for post-run analysis; the test assertion
    includes a count so the CI output is immediately actionable.
    """
    scenarios = _load_scenarios()
    assert len(scenarios) == 100, (
        f"Expected 100 scenarios in scenarios_100.json, got {len(scenarios)}"
    )

    results = _run_all(ros2_node, scenarios)

    passed = sum(1 for r in results if r.passed)
    total = len(results)
    pass_rate = passed / total if total > 0 else 0.0

    # Print failure summary for debugging
    failures = [r for r in results if not r.passed]
    if failures:
        lines = [f"\nFailed {len(failures)}/{total} scenarios:"]
        for r in failures:
            lines.append(
                f"  [{r.scenario_id}] elapsed={r.elapsed_s:.1f}s "
                f"rule={r.actual_rule!r} reason={r.failure_reason!r}"
            )
        print("\n".join(lines))

    assert pass_rate >= _PASS_RATE_THRESHOLD, (
        f"HIL pass rate {pass_rate:.1%} < {_PASS_RATE_THRESHOLD:.0%} "
        f"({passed}/{total} passed)"
    )


@pytest.mark.hil
def test_hil_rule17_standon(ros2_node):
    """
    Rule 17 (stand-on) scenarios: M5 must NOT publish a non-empty avoidance
    plan.  Expected action for all Rule_17 scenarios is 'maintain_course'.
    """
    all_scenarios = _load_scenarios()
    rule17 = [s for s in all_scenarios if s.colregs_rule == "Rule_17"]

    if not rule17:
        pytest.skip("No Rule_17 scenarios found in scenarios_100.json")

    runner = HilScenarioRunner(ros2_node)
    try:
        for scenario in rule17:
            result = runner.run_scenario(scenario, timeout_s=_TIMEOUT_S)
            assert result.actual_action in ("maintain_course", "no_response"), (
                f"[{scenario.scenario_id}] Rule 17 stand-on vessel "
                f"received avoidance action '{result.actual_action}': "
                f"{result.failure_reason}"
            )
    finally:
        runner.destroy()


@pytest.mark.hil
def test_hil_rule19_degraded(ros2_node):
    """
    Rule 19 / DEGRADED scenarios: M1 must set ODDState.health >= 1
    (HEALTH_DEGRADED or HEALTH_CRITICAL) within the timeout.
    ODDState.msg: health 0=FULL, 1=DEGRADED, 2=CRITICAL.
    """
    all_scenarios = _load_scenarios()
    rule19 = [
        s for s in all_scenarios
        if s.colregs_rule == "Rule_19" or s.odd_mode in ("DEGRADED", "CRITICAL")
    ]

    if not rule19:
        pytest.skip("No Rule_19 / DEGRADED scenarios found in scenarios_100.json")

    try:
        from l3_msgs.msg import ODDState  # noqa: F401
    except ImportError:
        pytest.skip("l3_msgs not available — cannot check ODDState")

    from rclpy.qos import QoSProfile, ReliabilityPolicy, HistoryPolicy
    qos = QoSProfile(
        history=HistoryPolicy.KEEP_LAST,
        depth=10,
        reliability=ReliabilityPolicy.RELIABLE,
    )

    odd_states: list = []

    def _on_odd(msg):
        odd_states.append(msg)

    sub = ros2_node.create_subscription(
        ODDState,
        "/l3/m1/odd_state",
        _on_odd,
        qos,
    )

    from target_injector import TargetInjectorNode
    injector = TargetInjectorNode(ros2_node)

    try:
        import time
        for scenario in rule19:
            odd_states.clear()
            injector.load_scenario(scenario)

            start = time.monotonic()
            degraded_seen = False
            while time.monotonic() - start < _TIMEOUT_S:
                try:
                    rclpy.spin_once(ros2_node, timeout_sec=0.1)
                except Exception:  # noqa: BLE001
                    pass
                for state in odd_states:
                    # ODDState.health: 0=HEALTH_FULL, 1=HEALTH_DEGRADED, 2=HEALTH_CRITICAL
                    if getattr(state, "health", 0) >= 1:
                        degraded_seen = True
                        break
                if degraded_seen:
                    break

            assert degraded_seen, (
                f"[{scenario.scenario_id}] M1 ODD mode did not transition "
                f"to DEGRADED within {_TIMEOUT_S}s for {scenario.odd_mode} scenario"
            )
    finally:
        injector.stop()
        ros2_node.destroy_subscription(sub)
