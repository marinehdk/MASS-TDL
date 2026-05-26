#!/usr/bin/env python3
"""ASDR HMAC-SHA256 real-time integrity verifier.

Subscribes to /asdr/decision_log, verifies hmac_sha256 field on every record.
P0 condition: any FAIL → writes P0 alert to --p0-alert-file and exits with code 2.

Usage:
    SIL_INTEGRITY_KEY=mysecret python3 tools/sil/asdr_sha256_verifier.py \
        --evidence-dir docs/Design/Phase\ 3/D3.7-sil-8module-integration/evidence
"""
from __future__ import annotations

import argparse
import csv
import hmac as _hmac
import hashlib
import json
import os
import struct
import sys
import time
from pathlib import Path

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, QoSReliabilityPolicy, QoSDurabilityPolicy, QoSHistoryPolicy

from sil_msgs.msg import ASDREvent

CSV_HEADER = ["timestamp", "scenario_id", "segment", "computed_sha256", "expected_sha256", "status"]


class ASDRVerifier(Node):
    def __init__(self, evidence_dir: Path, p0_alert_file: Path | None):
        super().__init__("asdr_sha256_verifier_d37")
        key_str = os.environ.get("SIL_INTEGRITY_KEY", "")
        if not key_str:
            self.get_logger().warn("SIL_INTEGRITY_KEY not set — all records will FAIL (hmac will be empty)")
        self._key = key_str.encode()
        self._evidence_dir = evidence_dir
        self._p0_alert_file = p0_alert_file
        self._csv_path = evidence_dir / "asdr_integrity.csv"
        self._counts = {"total": 0, "pass": 0, "fail": 0}

        evidence_dir.mkdir(parents=True, exist_ok=True)
        if not self._csv_path.exists():
            with open(self._csv_path, "w", newline="") as f:
                csv.writer(f).writerow(CSV_HEADER)

        qos = QoSProfile(
            reliability=QoSReliabilityPolicy.RELIABLE,
            durability=QoSDurabilityPolicy.VOLATILE,
            history=QoSHistoryPolicy.KEEP_LAST, depth=50,
        )
        self.create_subscription(ASDREvent, "/asdr/decision_log", self._on_asdr, qos)
        self.get_logger().info(f"ASDRVerifier: listening on /asdr/decision_log → {self._csv_path}")

    def _on_asdr(self, msg: ASDREvent):
        self._counts["total"] += 1
        ts = time.time()

        stamp_bytes = struct.pack(">qI", msg.stamp.sec, msg.stamp.nanosec)
        payload = stamp_bytes + msg.decision_id.encode() + msg.payload_json.encode()

        computed = _hmac.new(self._key, payload, hashlib.sha256).digest()
        expected = bytes(msg.hmac_sha256) if msg.hmac_sha256 else b""

        if expected and _hmac.compare_digest(computed, expected):
            status = "PASS"
            self._counts["pass"] += 1
        elif not expected:
            status = "NO_HMAC"
            self._counts["pass"] += 1
        else:
            status = "FAIL"
            self._counts["fail"] += 1
            self.get_logger().error(f"P0-6 ASDR integrity FAIL: decision_id={msg.decision_id}")
            self._trigger_p0(msg.decision_id)

        row = [
            f"{ts:.6f}",
            msg.decision_id,
            "",
            computed.hex(),
            expected.hex() if expected else "",
            status,
        ]
        with open(self._csv_path, "a", newline="") as f:
            csv.writer(f).writerow(row)

        if self._counts["total"] % 100 == 0:
            self.get_logger().info(
                f"ASDR: total={self._counts['total']} pass={self._counts['pass']} fail={self._counts['fail']}"
            )

    def _trigger_p0(self, decision_id: str):
        alert = {
            "type": "P0-6",
            "description": "ASDR_INTEGRITY_FAILURE",
            "decision_id": decision_id,
            "ts": time.time(),
        }
        if self._p0_alert_file:
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
    ap.add_argument("--p0-alert-file", type=Path, default=None,
                    help="JSON file for P0 alert IPC with orchestrator")
    args = ap.parse_args()
    rclpy.init()
    node = ASDRVerifier(args.evidence_dir, args.p0_alert_file)
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
