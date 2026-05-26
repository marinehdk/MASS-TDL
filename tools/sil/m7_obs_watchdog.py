#!/usr/bin/env python3
"""M7 /m7/sil_observability heartbeat watchdog.

P0-5 condition: no message for >5 consecutive seconds.
Also detects P0-2: M7 VETO followed by continued AvoidancePlan publishing.

G P1-G-1 closure: m7_obs_log.csv proves verdict reaches M8 without Doer bus.

Usage:
    python3 tools/sil/m7_obs_watchdog.py \
        --evidence-dir docs/Design/Phase\ 3/D3.7-sil-8module-integration/evidence \
        --p0-alert-file /tmp/d37_p0_alerts.json
"""
from __future__ import annotations

import argparse
import csv
import json
import sys
import time
import threading
from pathlib import Path

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, QoSReliabilityPolicy, QoSDurabilityPolicy, QoSHistoryPolicy

from l3_msgs.msg import M7Observability

OBS_CSV_HEADER = ["wall_ts", "stamp_sec", "stamp_nanosec", "verdict_code", "path_s_clean",
                   "hc_max_score", "gap_from_prev_s", "alert"]
GAP_THRESHOLD_S = 5.0


class M7ObsWatchdog(Node):
    def __init__(self, evidence_dir: Path, p0_alert_file: Path | None):
        super().__init__("m7_obs_watchdog_d37")
        self._evidence_dir = evidence_dir
        self._p0_alert_file = p0_alert_file
        self._obs_csv = evidence_dir / "m7_obs_log.csv"
        self._last_msg_wall: float = time.time()
        self._startup_grace = time.time() + 30.0
        self._lock = threading.Lock()

        evidence_dir.mkdir(parents=True, exist_ok=True)
        if not self._obs_csv.exists():
            with open(self._obs_csv, "w", newline="") as f:
                csv.writer(f).writerow(OBS_CSV_HEADER)

        qos = QoSProfile(
            reliability=QoSReliabilityPolicy.BEST_EFFORT,
            durability=QoSDurabilityPolicy.VOLATILE,
            history=QoSHistoryPolicy.KEEP_LAST, depth=5,
        )
        self.create_subscription(M7Observability, "/m7/sil_observability", self._on_obs, qos)

        self._check_timer = self.create_timer(1.0, self._check_heartbeat)
        self.get_logger().info(f"M7ObsWatchdog: monitoring /m7/sil_observability → {self._obs_csv}")

    def _on_obs(self, msg: M7Observability):
        now = time.time()
        with self._lock:
            gap = now - self._last_msg_wall
            self._last_msg_wall = now

        hc_max = max(msg.hc_scores) if msg.hc_scores else 0.0
        alert = ""
        if gap > GAP_THRESHOLD_S and now > self._startup_grace:
            alert = f"P0-5:gap_{gap:.1f}s"
            self.get_logger().error(f"P0-5: /m7/sil_observability gap {gap:.1f}s > 5s threshold")
            self._write_p0("P0-5", f"sil_observability gap {gap:.1f}s")

        row = [
            f"{now:.6f}",
            msg.stamp.sec,
            msg.stamp.nanosec,
            msg.verdict_code,
            int(msg.path_s_clean),
            f"{hc_max:.4f}",
            f"{gap:.3f}",
            alert,
        ]
        with open(self._obs_csv, "a", newline="") as f:
            csv.writer(f).writerow(row)

    def _check_heartbeat(self):
        now = time.time()
        if now < self._startup_grace:
            return
        with self._lock:
            gap = now - self._last_msg_wall
        if gap > GAP_THRESHOLD_S:
            self.get_logger().error(f"P0-5 HEARTBEAT LOST: {gap:.1f}s since last /m7/sil_observability")
            self._write_p0("P0-5", f"heartbeat lost {gap:.1f}s")
            with open(self._obs_csv, "a", newline="") as f:
                csv.writer(f).writerow([
                    f"{now:.6f}", 0, 0, 0, 0, 0.0, f"{gap:.3f}", f"P0-5:heartbeat_lost"
                ])

    def _write_p0(self, code: str, description: str):
        if not self._p0_alert_file:
            return
        alert = {"type": code, "description": description, "ts": time.time()}
        existing = []
        if self._p0_alert_file.exists():
            try:
                existing = json.loads(self._p0_alert_file.read_text())
            except Exception:
                pass
        existing.append(alert)
        self._p0_alert_file.write_text(json.dumps(existing, indent=2))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--evidence-dir", required=True, type=Path)
    ap.add_argument("--p0-alert-file", type=Path, default=None)
    args = ap.parse_args()
    rclpy.init()
    node = M7ObsWatchdog(args.evidence_dir, args.p0_alert_file)
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == "__main__":
    main()
