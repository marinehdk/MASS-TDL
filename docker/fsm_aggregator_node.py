#!/usr/bin/env python3
"""FSM Aggregator Node — stateless aggregation of M1/M4/M5/M7 into /l3/fsm_state.

Per v1.1.3-pre-stub §12.2 M8 transparency; per CLAUDE.md §3 v3.0 mandate.
This node is a lightweight stateless Doer in the M8 group. It does NOT
maintain an internal state machine — all transitions are event-driven from
subscriptions.

blocked_by: D-DEMO1-R3 (M4 IvP fix), D-DEMO1-R4 (mock L2 publisher)

Publishes /l3/fsm_state @ 10 Hz with:
  - current_state: TRANSIT | COLREG_AVOIDANCE | TOR | OVERRIDE | MRC | HANDBACK
  - active_rule: human-readable rule source
  - rationale: structured diagnostic string
  - confidence: [0, 1] per v3.0 mandate
"""

import signal
import threading
import time

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, QoSReliabilityPolicy, QoSDurabilityPolicy, QoSHistoryPolicy

from l3_msgs.msg import FsmState, ODDState, BehaviorPlan, AvoidancePlan, SafetyAlert


STATE_TRANSIT = FsmState.STATE_TRANSIT
STATE_COLREG_AVOIDANCE = FsmState.STATE_COLREG_AVOIDANCE
STATE_TOR = FsmState.STATE_TOR
STATE_OVERRIDE = FsmState.STATE_OVERRIDE
STATE_MRC = FsmState.STATE_MRC
STATE_HANDBACK = FsmState.STATE_HANDBACK

BEHAVIOR_COLREG_AVOID = BehaviorPlan.BEHAVIOR_COLREG_AVOID
BEHAVIOR_TRANSIT = BehaviorPlan.BEHAVIOR_TRANSIT

SEVERITY_MRC_REQUIRED = SafetyAlert.SEVERITY_MRC_REQUIRED
SEVERITY_CRITICAL = SafetyAlert.SEVERITY_CRITICAL
ALERT_IEC61508_FAULT = SafetyAlert.ALERT_IEC61508_FAULT

ENVELOPE_OUT = ODDState.ENVELOPE_OUT
ENVELOPE_IN = ODDState.ENVELOPE_IN
HEALTH_FULL = ODDState.HEALTH_FULL

STATE_NAMES = {
    STATE_TRANSIT: "TRANSIT",
    STATE_COLREG_AVOIDANCE: "COLREG_AVOIDANCE",
    STATE_TOR: "TOR",
    STATE_OVERRIDE: "OVERRIDE",
    STATE_MRC: "MRC",
    STATE_HANDBACK: "HANDBACK",
}

ENVELOPE_NAMES = {
    ODDState.ENVELOPE_IN: "IN",
    ODDState.ENVELOPE_EDGE: "EDGE",
    ODDState.ENVELOPE_OUT: "OUT",
    ODDState.ENVELOPE_MRC_PREP: "MRC_PREP",
    ODDState.ENVELOPE_MRC_ACTIVE: "MRC_ACTIVE",
}

BEHAVIOR_NAMES = {
    BehaviorPlan.BEHAVIOR_TRANSIT: "TRANSIT",
    BehaviorPlan.BEHAVIOR_COLREG_AVOID: "COLREG_AVOID",
    BehaviorPlan.BEHAVIOR_DP_HOLD: "DP_HOLD",
    BehaviorPlan.BEHAVIOR_BERTH: "BERTH",
    BehaviorPlan.BEHAVIOR_MRC_DRIFT: "MRC_DRIFT",
    BehaviorPlan.BEHAVIOR_MRC_ANCHOR: "MRC_ANCHOR",
    BehaviorPlan.BEHAVIOR_MRC_HEAVE_TO: "MRC_HEAVE_TO",
}

SEVERITY_NAMES = {
    SafetyAlert.SEVERITY_INFO: "INFO",
    SafetyAlert.SEVERITY_WARNING: "WARNING",
    SafetyAlert.SEVERITY_CRITICAL: "CRITICAL",
    SafetyAlert.SEVERITY_MRC_REQUIRED: "MRC_REQUIRED",
}


def _sensor_qos(depth: int = 5) -> QoSProfile:
    return QoSProfile(
        reliability=QoSReliabilityPolicy.BEST_EFFORT,
        durability=QoSDurabilityPolicy.VOLATILE,
        history=QoSHistoryPolicy.KEEP_LAST,
        depth=depth,
    )


def _latched_qos(depth: int = 50) -> QoSProfile:
    return QoSProfile(
        reliability=QoSReliabilityPolicy.RELIABLE,
        durability=QoSDurabilityPolicy.TRANSIENT_LOCAL,
        history=QoSHistoryPolicy.KEEP_LAST,
        depth=depth,
    )


class FsmAggregatorNode(Node):

    def __init__(self) -> None:
        super().__init__("fsm_aggregator")
        self.get_logger().info("[fsm_aggregator] Starting FSM aggregator node")

        sq = _sensor_qos()
        lq = _latched_qos()

        self._last_odd: ODDState | None = None
        self._last_odd_time: float = 0.0
        self._last_behavior: BehaviorPlan | None = None
        self._last_avoid: AvoidancePlan | None = None
        self._last_safety: SafetyAlert | None = None
        self._envelope_out_since: float | None = None

        self._sub_odd = self.create_subscription(
            ODDState, "/l3/m1/odd_state", self._on_odd, lq)
        self._sub_behavior = self.create_subscription(
            BehaviorPlan, "/l3/m4/behavior_plan", self._on_behavior, sq)
        self._sub_avoid = self.create_subscription(
            AvoidancePlan, "/l3/m5/avoidance_plan", self._on_avoid, sq)
        self._sub_safety = self.create_subscription(
            SafetyAlert, "/l3/m7/safety_alert", self._on_safety, sq)

        self._pub_fsm = self.create_publisher(
            FsmState, "/l3/fsm_state", _sensor_qos(depth=10))

        self._timer = self.create_timer(0.1, self._on_timer)

    def _on_odd(self, msg: ODDState) -> None:
        self._last_odd = msg
        self._last_odd_time = time.monotonic()

    def _on_behavior(self, msg: BehaviorPlan) -> None:
        self._last_behavior = msg

    def _on_avoid(self, msg: AvoidancePlan) -> None:
        self._last_avoid = msg

    def _on_safety(self, msg: SafetyAlert) -> None:
        self._last_safety = msg

    def _on_timer(self) -> None:
        now = time.monotonic()
        odd = self._last_odd
        beh = self._last_behavior
        avoid = self._last_avoid
        safety = self._last_safety

        fsm_state = STATE_TRANSIT
        active_rule = "Nominal autopilot"
        confidence = 0.95

        envelope_val = odd.envelope_state if odd else ENVELOPE_IN
        envelope_name = ENVELOPE_NAMES.get(envelope_val, str(envelope_val))
        beh_val = beh.behavior if beh else BEHAVIOR_TRANSIT
        beh_name = BEHAVIOR_NAMES.get(beh_val, str(beh_val))
        sev_val = safety.severity if safety else SafetyAlert.SEVERITY_INFO
        sev_name = SEVERITY_NAMES.get(sev_val, str(sev_val))
        avoid_wp_count = len(avoid.waypoints) if avoid else 0

        if envelope_val == ENVELOPE_OUT:
            if self._envelope_out_since is None:
                self._envelope_out_since = now
            elapsed = now - self._envelope_out_since
            if elapsed > 5.0:
                fsm_state = STATE_MRC
                active_rule = f"M1 ODD ENVELOPE_OUT > 5s ({elapsed:.1f}s)"
                confidence = 0.9
        else:
            self._envelope_out_since = None

        if fsm_state == STATE_TRANSIT:
            if safety and sev_val == SEVERITY_MRC_REQUIRED:
                fsm_state = STATE_MRC
                active_rule = safety.recommended_mrm if safety.recommended_mrm else "MRM-UNKNOWN"
                confidence = safety.confidence if safety.confidence > 0 else 0.9
            elif safety and sev_val == SEVERITY_CRITICAL and safety.alert_type != ALERT_IEC61508_FAULT:
                fsm_state = STATE_TOR
                active_rule = f"SAT-1:{safety.description}" if safety.description else "SAT-1:Critical alert"
                confidence = safety.confidence if safety.confidence > 0 else 0.7

        if fsm_state == STATE_TRANSIT and beh and beh_val == BEHAVIOR_COLREG_AVOID:
            fsm_state = STATE_COLREG_AVOIDANCE
            active_rule = f"Rule 14 head-on"
            confidence = beh.confidence if beh.confidence > 0 else 0.5

        if fsm_state == STATE_COLREG_AVOIDANCE and avoid:
            if avoid_wp_count < 1:
                fsm_state = STATE_HANDBACK
                active_rule = "Avoidance complete / handback"
                confidence = 0.8

        if fsm_state == STATE_TRANSIT and odd:
            if envelope_val == ENVELOPE_IN:
                confidence = odd.confidence if odd.confidence > 0 else 0.95

        out = FsmState()
        out.stamp = self.get_clock().now().to_msg()
        out.schema_version = 1
        out.current_state = fsm_state
        out.active_rule = active_rule
        out.rationale = (
            f"state={STATE_NAMES.get(fsm_state, str(fsm_state))} "
            f"rule='{active_rule}' "
            f"odd_env={envelope_name} "
            f"beh={beh_name} "
            f"safety_sev={sev_name} "
            f"avoid_wp={avoid_wp_count}"
        )
        out.confidence = confidence

        self._pub_fsm.publish(out)


def main() -> None:
    rclpy.init()
    node = FsmAggregatorNode()
    executor = rclpy.executors.SingleThreadedExecutor()
    executor.add_node(node)

    shutdown_event = threading.Event()

    def _sigint_handler(sig, frame):
        shutdown_event.set()

    signal.signal(signal.SIGINT, _sigint_handler)

    try:
        while rclpy.ok() and not shutdown_event.is_set():
            executor.spin_once(timeout_sec=0.1)
    except KeyboardInterrupt:
        pass
    finally:
        node.get_logger().info("[fsm_aggregator] Shutting down")
        executor.shutdown()
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == "__main__":
    main()
