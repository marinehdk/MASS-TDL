"""ScoringNode — ROS2 LifecycleNode with 6-dim Hagen 2022 scoring.

Subscribes:
  - /sil/own_ship_state (50 Hz) — sil_msgs/OwnShipState
  - /sil/target_vessel_state (10 Hz) — sil_msgs/TargetVesselState
  - /l3/colregs_active (event) — std_msgs/String ("R14:ok,R8:partial" format)
Publishes:
  - /sil/scoring (1 Hz) — sil_msgs/ScoringRow
Side-channel:
  - Arrow IPC → runs/{run_id}/scoring.arrow (1 Hz via ArrowWriter)
"""

import os
import time
from pathlib import Path

from .hagen_scorer import HagenScorer, ScoringRow
from .arrow_writer import ArrowWriter


# Try ROS2 imports — graceful degradation on macOS dev
try:
    import rclpy
    from rclpy.node import Node
    from rclpy.lifecycle import LifecycleNode, LifecycleState, TransitionCallbackReturn
    HAS_ROS2 = True
except ImportError:
    HAS_ROS2 = False


if HAS_ROS2:
    from lifecycle_msgs.msg import State, Transition
    from sil_msgs.msg import OwnShipState, TargetVesselState, ScoringRow as ScoringRowMsg
    from std_msgs.msg import String


class ScoringLifecycleNode(LifecycleNode if HAS_ROS2 else object):
    """ROS2 LifecycleNode wrapping HagenScorer + ArrowWriter."""

    def __init__(self, node_name: str = "scoring_node"):
        if HAS_ROS2:
            super().__init__(node_name)
        self._scorer = HagenScorer()
        self._arrow_writer = None
        self._own_ship_sub = None
        self._target_vessel_sub = None
        self._colregs_sub = None
        self._scoring_pub = None
        self._timer = None
        self._run_dir = None
        self._latest_own_ship = {}
        self._latest_targets = []
        self._latest_rule_states = {}

    # ─── Lifecycle callbacks (only active when rclpy available) ───

    def on_configure(self, state=None):
        if HAS_ROS2:
            self.get_logger().info("scoring_node configuring")
            self._own_ship_sub = self.create_subscription(
                OwnShipState, "/sil/own_ship_state", self._own_ship_cb, 10)
            self._target_vessel_sub = self.create_subscription(
                TargetVesselState, "/sil/target_vessel_state", self._target_vessel_cb, 10)
            self._colregs_sub = self.create_subscription(
                String, "/l3/colregs_active", self._colregs_cb, 10)
            self._scoring_pub = self.create_publisher(
                ScoringRowMsg, "/sil/scoring", 10)
        return TransitionCallbackReturn.SUCCESS if HAS_ROS2 else None

    def on_activate(self, state=None):
        if HAS_ROS2:
            self.get_logger().info("scoring_node activating")
        run_dir = os.environ.get("SIL_RUN_DIR", "/var/sil/runs")
        run_id = os.environ.get("SIL_RUN_ID", f"run-{int(time.time() * 1000):x}")
        self._run_dir = Path(run_dir) / run_id
        self._run_dir.mkdir(parents=True, exist_ok=True)
        arrow_path = self._run_dir / "scoring.arrow"
        self._arrow_writer = ArrowWriter(str(arrow_path))
        if HAS_ROS2:
            self._timer = self.create_timer(1.0, self._score_and_publish)
        return TransitionCallbackReturn.SUCCESS if HAS_ROS2 else None

    def on_deactivate(self, state=None):
        if HAS_ROS2:
            self.get_logger().info("scoring_node deactivating")
            if self._timer:
                self.destroy_timer(self._timer)
                self._timer = None
        if self._arrow_writer:
            self._arrow_writer.close()
            self._arrow_writer = None
        return TransitionCallbackReturn.SUCCESS if HAS_ROS2 else None

    def on_cleanup(self, state=None):
        if HAS_ROS2:
            self.get_logger().info("scoring_node cleaning up")
            if self._own_ship_sub:
                self.destroy_subscription(self._own_ship_sub)
            if self._target_vessel_sub:
                self.destroy_subscription(self._target_vessel_sub)
            if self._colregs_sub:
                self.destroy_subscription(self._colregs_sub)
            if self._scoring_pub:
                self.destroy_publisher(self._scoring_pub)
        self._scorer = HagenScorer()
        return TransitionCallbackReturn.SUCCESS if HAS_ROS2 else None

    # ─── Topic callbacks ───────────────────────────────────────

    def _own_ship_cb(self, msg):
        self._latest_own_ship = {
            "lat": msg.lat, "lon": msg.lon,
            "heading": msg.heading, "sog": msg.sog, "cog": msg.cog,
            "rot": msg.rot, "rudder_angle": msg.rudder_angle,
        }

    def _target_vessel_cb(self, msg):
        self._latest_targets.append({
            "lat": msg.lat, "lon": msg.lon,
            "heading": msg.heading, "sog": msg.sog, "cog": msg.cog,
        })
        if len(self._latest_targets) > 50:
            self._latest_targets = self._latest_targets[-20:]

    def _colregs_cb(self, msg):
        self._latest_rule_states = {}
        for pair in msg.data.split(","):
            if ":" in pair:
                k, v = pair.split(":", 1)
                self._latest_rule_states[k.strip()] = v.strip()

    # ─── Scoring timer (1 Hz) ──────────────────────────────────

    def _score_and_publish(self):
        if not self._latest_own_ship:
            return
        own = self._latest_own_ship
        targets = [(t["lat"], t["lon"], t["cog"], t["sog"]) for t in self._latest_targets]
        row = self._scorer.score_frame(
            own_lat=own["lat"], own_lon=own["lon"],
            own_heading=own["heading"], own_sog=own["sog"],
            targets=targets,
            rule_states=self._latest_rule_states,
            t_action_s=0.0, t_target_action_s=120.0,
            rudder_deg=own.get("rudder_angle", 0.0),
            turning_rate_dps=abs(own.get("rot", 0.0)),
            behavior_phase="transit",
            trajectory_curvature=0.0, trajectory_accel_ms2=0.0,
            timestamp=time.time(),
        )
        if HAS_ROS2 and self._scoring_pub:
            msg = ScoringRowMsg()
            msg.stamp = self.get_clock().now().to_msg()
            msg.safety = row.safety
            msg.rule_compliance = row.rule_compliance
            msg.delay = row.delay_penalty
            msg.magnitude = row.action_magnitude_penalty
            msg.phase = row.phase_score
            msg.plausibility = row.plausibility
            msg.total = row.total
            self._scoring_pub.publish(msg)
        if self._arrow_writer:
            self._arrow_writer.append(row)


def main(args=None):
    """CLI entry point (ROS2 console_scripts stub)."""
    if HAS_ROS2:
        rclpy.init(args=args)
        node = ScoringLifecycleNode()
        executor = rclpy.executors.SingleThreadedExecutor()
        executor.add_node(node)
        try:
            executor.spin()
        except KeyboardInterrupt:
            pass
        finally:
            node.destroy_node()
            rclpy.shutdown()
    else:
        print("ScoringNode ready (no ROS2 runtime)")
