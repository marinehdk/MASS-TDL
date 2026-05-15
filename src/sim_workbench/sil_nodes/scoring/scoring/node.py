"""Scoring Node — 6-dim scoring (Hagen 2022): safety, rule, delay, magnitude, phase, plausibility.

ROS2 lifecycle node that publishes sil_msgs/ScoringRow @ 1Hz.
"""

from __future__ import annotations

import json

import rclpy
from rclpy.lifecycle import LifecycleNode, TransitionCallbackReturn
from rclpy.qos import QoSProfile, ReliabilityPolicy, DurabilityPolicy, HistoryPolicy
from sil_msgs.msg import ScoringRow, OwnShipState, TargetVesselState


class ScoringNode(LifecycleNode):
    """Six-dimensional scenario scoring per Hagen (2022).

    Dimensions
    ----------
    safety          : CPA normalised against a 1 NM threshold.
    rule_compliance : binary — 1.0 if no rule broken, else 0.0.
    delay           : exponential decay from 60 s allowed lag.
    magnitude       : path deviation normalised against 500 m.
    phase           : reserved / structural (stub = 1.0).
    plausibility    : reserved / structural (stub = 1.0).
    """

    DIMS = ["safety", "rule_compliance", "delay", "magnitude", "phase", "plausibility"]

    def __init__(self, weights: dict | None = None, **kwargs) -> None:
        super().__init__("scoring_node", **kwargs)
        self._weights = weights or {d: 1.0 / 6 for d in self.DIMS}
        self._rows: list[dict] = []
        self._score_pub = None
        self._timer = None
        self._own_ship_sub = None
        self._target_sub = None
        self._own_ship_state: OwnShipState | None = None
        self._target_state: TargetVesselState | None = None

    # ── Lifecycle callbacks ──────────────────────────────────────────────

    def on_configure(self, state) -> TransitionCallbackReturn:
        """Declare weights parameter and load from parameter server."""
        self.declare_parameter(
            "weights_json",
            '{"safety":0.25,"rule_compliance":0.25,"delay":0.15,"magnitude":0.15,"phase":0.1,"plausibility":0.1}',
        )
        raw = self.get_parameter("weights_json").get_parameter_value().string_value
        parsed = json.loads(raw)
        for d in self.DIMS:
            if d not in parsed:
                self._logger.warning(f"Missing weight '{d}', defaulting to 1/{len(self.DIMS)}")
                parsed[d] = 1.0 / len(self.DIMS)
        self._weights = parsed
        self._logger.info(f"configured with weights={self._weights}")
        return TransitionCallbackReturn.SUCCESS

    def on_activate(self, state) -> TransitionCallbackReturn:
        """Create publisher, timer, and subscriptions."""
        qos = QoSProfile(
            reliability=ReliabilityPolicy.RELIABLE,
            durability=DurabilityPolicy.TRANSIENT_LOCAL,
            history=HistoryPolicy.KEEP_LAST,
            depth=100,
        )
        self._score_pub = self.create_publisher(ScoringRow, "/sil/scoring", qos)
        self._timer = self.create_timer(1.0, self._score_callback)

        sub_qos = QoSProfile(
            reliability=ReliabilityPolicy.BEST_EFFORT,
            durability=DurabilityPolicy.VOLATILE,
            history=HistoryPolicy.KEEP_LAST,
            depth=1,
        )
        self._own_ship_sub = self.create_subscription(
            OwnShipState,
            "/sil/own_ship_state",
            self._on_own_ship,
            sub_qos,
        )
        self._target_sub = self.create_subscription(
            TargetVesselState,
            "/sil/target_vessel_state",
            self._on_target,
            sub_qos,
        )

        self._logger.info("activated — publishing ScoringRow @ 1 Hz on /sil/scoring")
        return TransitionCallbackReturn.SUCCESS

    def on_deactivate(self, state) -> TransitionCallbackReturn:
        """Destroy timer and subscriptions."""
        if self._timer is not None:
            self.destroy_timer(self._timer)
            self._timer = None
        if self._own_ship_sub is not None:
            self.destroy_subscription(self._own_ship_sub)
            self._own_ship_sub = None
        if self._target_sub is not None:
            self.destroy_subscription(self._target_sub)
            self._target_sub = None
        if self._score_pub is not None:
            self.destroy_publisher(self._score_pub)
            self._score_pub = None
        self._logger.info("deactivated")
        return TransitionCallbackReturn.SUCCESS

    def on_cleanup(self, state) -> TransitionCallbackReturn:
        """Clear accumulated scoring rows."""
        self._rows.clear()
        self._own_ship_state = None
        self._target_state = None
        self._logger.info("cleanup — rows cleared")
        return TransitionCallbackReturn.SUCCESS

    # ── Subscription callbacks ───────────────────────────────────────────

    def _on_own_ship(self, msg: OwnShipState) -> None:
        self._own_ship_state = msg

    def _on_target(self, msg: TargetVesselState) -> None:
        self._target_state = msg

    # ── Scoring logic ────────────────────────────────────────────────────

    def score(
        self,
        timestamp: float,
        cpa: float = 1.0,
        rule_broken: bool = False,
        delay_s: float = 0.0,
        path_deviation: float = 0.0,
    ) -> dict:
        """Produce one scored row for a single simulation timestep."""
        safety = min(1.0, cpa / 1.0)
        rule = 0.0 if rule_broken else 1.0
        delay_score = max(0.0, 1.0 - delay_s / 60.0)
        magnitude = max(0.0, 1.0 - path_deviation / 500.0)
        phase = 1.0
        plausibility = 1.0

        total = (
            self._weights["safety"] * safety
            + self._weights["rule_compliance"] * rule
            + self._weights["delay"] * delay_score
            + self._weights["magnitude"] * magnitude
            + self._weights["phase"] * phase
            + self._weights["plausibility"] * plausibility
        )

        row = {
            "stamp": timestamp,
            "safety": safety,
            "rule_compliance": rule,
            "delay": delay_score,
            "magnitude": magnitude,
            "phase": phase,
            "plausibility": plausibility,
            "total": total,
        }
        self._rows.append(row)
        return row

    def get_rows(self) -> list[dict]:
        """Return all scored rows."""
        return list(self._rows)

    def get_final_verdict(self, threshold: float = 0.7) -> tuple[bool, float]:
        """Aggregate average total score across all rows.

        Returns (pass, average).
        """
        if not self._rows:
            return False, 0.0
        avg = sum(r["total"] for r in self._rows) / len(self._rows)
        return avg >= threshold, avg

    # ── Timer callback ──────────────────────────────────────────────────

    def _score_callback(self) -> None:
        """Compute and publish a scoring row at 1 Hz."""
        if self._own_ship_state is None:
            self._logger.debug("skipping score — no own ship state yet")
            return

        now = self.get_clock().now()
        stamp = now.to_msg()

        # Placeholder values — CPA estimation and rule check TBD
        row = self.score(
            timestamp=now.nanoseconds * 1e-9,
            cpa=1.0,
            rule_broken=False,
            delay_s=0.0,
            path_deviation=0.0,
        )

        msg = ScoringRow()
        msg.stamp = stamp
        msg.safety = row["safety"]
        msg.rule_compliance = row["rule_compliance"]
        msg.delay = row["delay"]
        msg.magnitude = row["magnitude"]
        msg.phase = row["phase"]
        msg.plausibility = row["plausibility"]
        msg.total = row["total"]

        self._score_pub.publish(msg)
        self._logger.debug(f"published ScoringRow total={row['total']:.3f}")


def main(args: list[str] | None = None) -> None:
    """ROS2 lifecycle node entry point."""
    rclpy.init(args=args)
    node = ScoringNode()
    rclpy.spin(node)
    node.destroy_node()
    rclpy.shutdown()


if __name__ == "__main__":
    main()
