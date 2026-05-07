"""
hil_runner.py — Orchestrates individual HIL scenario runs against the full
MASS L3 stack and evaluates pass/fail criteria.

Subscribes to:
  /l3/m6/colregs_constraint  (l3_msgs/COLREGsConstraint)
  /l3/m5/avoidance_plan      (l3_msgs/AvoidancePlan)
  /l3/m1/odd_state           (l3_msgs/ODDState)           [Rule 19 / degraded]

Per v1.1.2 §10, §9, §11 response contracts.
"""

from __future__ import annotations

import dataclasses
import threading
import time
from typing import Optional

try:
    import rclpy
    from rclpy.node import Node
    from rclpy.qos import QoSProfile, ReliabilityPolicy, HistoryPolicy
    _RCLPY_AVAILABLE = True
except ImportError:
    _RCLPY_AVAILABLE = False
    Node = object  # type: ignore

try:
    from l3_msgs.msg import COLREGsConstraint, AvoidancePlan, ODDState
    _MSGS_AVAILABLE = True
except ImportError:
    _MSGS_AVAILABLE = False

from scenario_schema import Scenario
from target_injector import TargetInjectorNode

# Minimum observation window for stand-on scenarios: wait to see if M5
# emits a spurious avoidance plan before declaring a pass.
_STANDON_OBSERVATION_S = 2.0


# ---------------------------------------------------------------------------
# Result dataclass
# ---------------------------------------------------------------------------

@dataclasses.dataclass
class HilResult:
    """Result of a single HIL scenario run."""
    scenario_id: str
    passed: bool
    actual_rule: str          # rule label from first COLREGsConstraint received
    actual_role: str          # "give_way" | "stand_on" | "" if not determined
    actual_action: str        # "avoidance_planned" | "maintain_course" | "no_response"
    elapsed_s: float
    failure_reason: str       # empty string on pass


# ---------------------------------------------------------------------------
# Runner
# ---------------------------------------------------------------------------

class HilScenarioRunner:
    """
    Runs a single scenario against the live L3 stack and returns a HilResult.

    The caller is responsible for initialising rclpy and providing an
    initialised rclpy Node.  The runner subscribes to the output topics,
    injects the scenario targets via TargetInjectorNode, and spins until
    pass criteria are satisfied or the timeout expires.
    """

    def __init__(self, ros_node: "Node") -> None:
        self._node = ros_node
        self._injector: Optional[TargetInjectorNode] = None

        # Thread-safe state updated by subscription callbacks
        self._lock = threading.Lock()
        self._constraint_received: Optional["COLREGsConstraint"] = None
        self._plan_received: Optional["AvoidancePlan"] = None
        self._odd_state_received: Optional["ODDState"] = None

        if not (_RCLPY_AVAILABLE and _MSGS_AVAILABLE):
            return

        qos = QoSProfile(
            history=HistoryPolicy.KEEP_LAST,
            depth=10,
            reliability=ReliabilityPolicy.RELIABLE,
        )

        self._constraint_sub = ros_node.create_subscription(
            COLREGsConstraint,
            "/l3/m6/colregs_constraint",
            self._on_constraint,
            qos,
        )
        self._plan_sub = ros_node.create_subscription(
            AvoidancePlan,
            "/l3/m5/avoidance_plan",
            self._on_plan,
            qos,
        )
        self._odd_sub = ros_node.create_subscription(
            ODDState,
            "/l3/m1/odd_state",
            self._on_odd_state,
            qos,
        )

    # ------------------------------------------------------------------
    # Subscription callbacks (called on the spin thread)
    # ------------------------------------------------------------------

    def _on_constraint(self, msg: "COLREGsConstraint") -> None:
        with self._lock:
            self._constraint_received = msg

    def _on_plan(self, msg: "AvoidancePlan") -> None:
        with self._lock:
            self._plan_received = msg

    def _on_odd_state(self, msg: "ODDState") -> None:
        with self._lock:
            self._odd_state_received = msg

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    def run_scenario(
        self,
        scenario: Scenario,
        timeout_s: float = 30.0,
    ) -> HilResult:
        """
        Inject *scenario*, spin until pass/fail criteria met or *timeout_s*.

        Returns a populated HilResult.
        """
        # Reset state
        with self._lock:
            self._constraint_received = None
            self._plan_received = None
            self._odd_state_received = None

        # Start injecting targets
        if self._injector is None:
            self._injector = TargetInjectorNode(self._node)
        self._injector.load_scenario(scenario)

        start = time.monotonic()

        # Spin loop
        try:
            while True:
                elapsed = time.monotonic() - start
                if elapsed >= timeout_s:
                    break

                if _RCLPY_AVAILABLE:
                    try:
                        rclpy.spin_once(self._node, timeout_sec=0.1)
                    except Exception:  # noqa: BLE001
                        pass

                with self._lock:
                    constraint = self._constraint_received
                    plan = self._plan_received
                    odd_state = self._odd_state_received

                done, result = self._evaluate(
                    scenario, constraint, plan, odd_state, elapsed
                )
                if done:
                    return result

        finally:
            # Stop injecting after each scenario so the next scenario starts clean
            if self._injector is not None:
                self._injector.load_scenario(None)  # silence injector between scenarios

        elapsed = time.monotonic() - start
        with self._lock:
            constraint = self._constraint_received
            plan = self._plan_received
            odd_state = self._odd_state_received

        # Final evaluation at timeout
        _done, result = self._evaluate(
            scenario, constraint, plan, odd_state, elapsed, timed_out=True
        )
        return result

    def destroy(self) -> None:
        """Release subscriptions and stop injector."""
        if self._injector is not None:
            self._injector.stop()
            self._injector = None
        try:
            self._node.destroy_subscription(self._constraint_sub)
            self._node.destroy_subscription(self._plan_sub)
            if hasattr(self, "_odd_sub") and self._odd_sub is not None:
                self._node.destroy_subscription(self._odd_sub)
        except Exception:  # noqa: BLE001
            pass

    # ------------------------------------------------------------------
    # Pass / fail criteria
    # ------------------------------------------------------------------

    def _evaluate(
        self,
        scenario: Scenario,
        constraint: Optional["COLREGsConstraint"],
        plan: Optional["AvoidancePlan"],
        odd_state: Optional["ODDState"],
        elapsed: float,
        timed_out: bool = False,
    ) -> tuple[bool, HilResult]:
        """
        Return (done, result).  done=False means keep spinning.
        """
        colregs_rule = scenario.expected.colregs_rule
        own_role = scenario.expected.own_role

        actual_rule = self._extract_rule(constraint)
        actual_role = ""
        actual_action = "no_response"
        failure_reason = ""

        # --- Rule 19 / DEGRADED scenarios -----------------------------------
        if colregs_rule == "Rule_19" or scenario.odd_mode in ("DEGRADED", "CRITICAL"):
            # Any response (constraint or plan) counts as pass.
            if constraint is not None or plan is not None:
                actual_action = "response_received"
                return True, HilResult(
                    scenario_id=scenario.scenario_id,
                    passed=True,
                    actual_rule=actual_rule,
                    actual_role=actual_role,
                    actual_action=actual_action,
                    elapsed_s=elapsed,
                    failure_reason="",
                )
            if timed_out:
                failure_reason = "no response within timeout (Rule19/DEGRADED)"
                return True, HilResult(
                    scenario_id=scenario.scenario_id,
                    passed=False,
                    actual_rule=actual_rule,
                    actual_role=actual_role,
                    actual_action=actual_action,
                    elapsed_s=elapsed,
                    failure_reason=failure_reason,
                )
            return False, HilResult(  # keep spinning
                scenario_id=scenario.scenario_id,
                passed=False,
                actual_rule="",
                actual_role="",
                actual_action="no_response",
                elapsed_s=elapsed,
                failure_reason="",
            )

        # --- give_way / both_give_way roles ----------------------------------
        if own_role in ("give_way", "both_give_way"):
            constraint_ok = constraint is not None
            plan_ok = plan is not None and len(plan.waypoints) >= 1
            if constraint_ok and plan_ok:
                actual_role = "give_way"
                actual_action = "avoidance_planned"
                return True, HilResult(
                    scenario_id=scenario.scenario_id,
                    passed=True,
                    actual_rule=actual_rule,
                    actual_role=actual_role,
                    actual_action=actual_action,
                    elapsed_s=elapsed,
                    failure_reason="",
                )
            if timed_out:
                missing = []
                if not constraint_ok:
                    missing.append("M6 constraint")
                if not plan_ok:
                    missing.append("M5 avoidance plan with waypoints")
                failure_reason = f"give_way timeout: missing {', '.join(missing)}"
                return True, HilResult(
                    scenario_id=scenario.scenario_id,
                    passed=False,
                    actual_rule=actual_rule,
                    actual_role=actual_role,
                    actual_action="no_response",
                    elapsed_s=elapsed,
                    failure_reason=failure_reason,
                )
            return False, HilResult(  # keep spinning
                scenario_id=scenario.scenario_id,
                passed=False,
                actual_rule="",
                actual_role="",
                actual_action="no_response",
                elapsed_s=elapsed,
                failure_reason="",
            )

        # --- stand_on role (Rule 17) -----------------------------------------
        if own_role == "stand_on":
            constraint_ok = constraint is not None
            # stand_on: M5 should NOT publish a non-empty avoidance plan
            plan_is_empty = (
                plan is None
                or len(plan.waypoints) == 0
            )
            # Require minimum observation window before declaring pass so that
            # a spurious M5 plan arriving slightly after the constraint is caught.
            observation_elapsed = elapsed >= _STANDON_OBSERVATION_S
            if constraint_ok and plan_is_empty and observation_elapsed:
                actual_role = "stand_on"
                actual_action = "maintain_course"
                return True, HilResult(
                    scenario_id=scenario.scenario_id,
                    passed=True,
                    actual_rule=actual_rule,
                    actual_role=actual_role,
                    actual_action=actual_action,
                    elapsed_s=elapsed,
                    failure_reason="",
                )
            if constraint_ok and not plan_is_empty:
                # M5 published avoidance waypoints for a stand_on vessel — fail immediately
                actual_role = "give_way"  # incorrectly acting as give_way
                actual_action = "avoidance_planned"
                return True, HilResult(
                    scenario_id=scenario.scenario_id,
                    passed=False,
                    actual_rule=actual_rule,
                    actual_role=actual_role,
                    actual_action=actual_action,
                    elapsed_s=elapsed,
                    failure_reason="stand_on vessel received non-empty avoidance plan from M5",
                )
            if timed_out:
                failure_reason = "stand_on timeout: M6 constraint not received"
                return True, HilResult(
                    scenario_id=scenario.scenario_id,
                    passed=False,
                    actual_rule=actual_rule,
                    actual_role=actual_role,
                    actual_action="no_response",
                    elapsed_s=elapsed,
                    failure_reason=failure_reason,
                )
            return False, HilResult(  # keep spinning
                scenario_id=scenario.scenario_id,
                passed=False,
                actual_rule="",
                actual_role="",
                actual_action="no_response",
                elapsed_s=elapsed,
                failure_reason="",
            )

        # Unknown role — fail safe
        if timed_out or constraint is not None:
            return True, HilResult(
                scenario_id=scenario.scenario_id,
                passed=False,
                actual_rule=actual_rule,
                actual_role=actual_role,
                actual_action=actual_action,
                elapsed_s=elapsed,
                failure_reason=f"unknown own_role '{own_role}'",
            )
        return False, HilResult(
            scenario_id=scenario.scenario_id,
            passed=False,
            actual_rule="",
            actual_role="",
            actual_action="no_response",
            elapsed_s=elapsed,
            failure_reason="",
        )

    @staticmethod
    def _extract_rule(constraint: Optional["COLREGsConstraint"]) -> str:
        """Return a rule label string from the first active_rules entry."""
        if constraint is None:
            return ""
        if constraint.active_rules:
            rule_id = constraint.active_rules[0].rule_id
            return f"Rule_{rule_id}"
        return ""
