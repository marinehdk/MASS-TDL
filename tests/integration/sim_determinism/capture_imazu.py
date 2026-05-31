# tests/integration/sim_determinism/capture_imazu.py
"""Drive SIL lifecycle and capture own-ship telemetry to a CSV file.

Usage (inside sil-nodes container, or host with network_mode=host):
    python3 capture_imazu.py --rate 1.0 --duration 60 --output /tmp/run_1x.csv
"""
from __future__ import annotations
import argparse
import csv
import json
import math
import ssl
import sys
import time
import urllib.request
import urllib.error

import rclpy
from rclpy.node import Node
from rclpy.qos import DurabilityPolicy, HistoryPolicy, QoSProfile, ReliabilityPolicy
from rosgraph_msgs.msg import Clock as ClockMsg
from sil_msgs.msg import OwnShipState
from l3_msgs.msg import BehaviorPlan, COLREGsConstraint

# Orchestrator HTTPS endpoint (self-signed cert, verification skipped)
ORCHESTRATOR_URL = "https://localhost:8000"
SSL_CTX = ssl.create_default_context()
SSL_CTX.check_hostname = False
SSL_CTX.verify_mode = ssl.CERT_NONE

# Longitude of imazu-01-ho route meridian (for XTE calculation)
ROUTE_LON = 10.38  # degrees


def _api(path: str, payload: dict | None = None) -> dict:
    url = ORCHESTRATOR_URL + path
    data = json.dumps(payload).encode() if payload else None
    req = urllib.request.Request(
        url, data=data,
        headers={"Content-Type": "application/json"} if data else {},
        method="POST"
    )
    try:
        with urllib.request.urlopen(req, context=SSL_CTX, timeout=30) as resp:
            res = json.loads(resp.read())
            if isinstance(res, dict) and not res.get("success", True):
                err_msg = res.get("error") or "Unknown error"
                raise RuntimeError(f"API call to {path} failed: {err_msg}")
            return res
    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8", errors="ignore")
        print(f"HTTPError on API call to {path}: {e.code} {e.reason}\nBody: {body}", file=sys.stderr, flush=True)
        raise
    except Exception as e:
        print(f"Exception on API call to {path}: {e}", file=sys.stderr, flush=True)
        raise


def _meters_easting(lon_deg: float, lat_deg: float) -> float:
    """Approximate easting distance from route meridian (lon=ROUTE_LON)."""
    return (lon_deg - ROUTE_LON) * math.pi / 180.0 * 6_371_000 * math.cos(
        lat_deg * math.pi / 180.0
    )


class CaptureNode(Node):
    def __init__(self, rate: float, duration: float, output: str) -> None:
        super().__init__("capture_imazu")
        self.rate = rate
        self.duration = duration
        self.output = output
        self.rows: list[dict] = []
        self._last_behavior: str = ""
        self._last_conflict: int = 0
        self._sim_t: float = 0.0
        self._wall_start: float = time.time()
        self._running = True

        qos = QoSProfile(
            reliability=ReliabilityPolicy.BEST_EFFORT,
            durability=DurabilityPolicy.VOLATILE,
            history=HistoryPolicy.KEEP_LAST,
            depth=10,
        )
        self.create_subscription(OwnShipState, "/sil/own_ship_state",
                                 self._on_oss, qos)
        self.create_subscription(BehaviorPlan, "/l3/m4/behavior_plan",
                                 self._on_bp, qos)
        self.create_subscription(COLREGsConstraint, "/l3/m6/colregs_constraint",
                                 self._on_cr, qos)
        self.create_subscription(ClockMsg, "/clock",
                                 self._on_clock, qos)

    def _on_clock(self, msg: ClockMsg) -> None:
        self._sim_t = msg.clock.sec + msg.clock.nanosec * 1e-9

    def _on_bp(self, msg: BehaviorPlan) -> None:
        self._last_behavior = str(msg.behavior)

    def _on_cr(self, msg: COLREGsConstraint) -> None:
        self._last_conflict = int(msg.conflict_detected)

    def _on_oss(self, msg: OwnShipState) -> None:
        if not self._running:
            return
        xte_m = _meters_easting(msg.lon, msg.lat)
        self.rows.append({
            "sim_t": round(self._sim_t, 3),
            "wall_t": round(time.time(), 3),
            "lat": msg.lat,
            "lon": msg.lon,
            "heading_deg": round(math.degrees(msg.heading), 4),
            "rudder_deg": round(math.degrees(getattr(msg, "rudder_angle", 0.0)), 4),
            "xte_m": round(xte_m, 2),
            "behavior": self._last_behavior,
            "conflict": self._last_conflict,
        })
        # Stop after wall-clock duration
        if time.time() - self._wall_start > self.duration:
            self._running = False

    def done(self) -> bool:
        if time.time() - self._wall_start > self.duration:
            self._running = False
        return not self._running

    def save(self) -> None:
        if not self.rows:
            return
        fields = list(self.rows[0].keys())
        with open(self.output, "w", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=fields)
            writer.writeheader()
            writer.writerows(self.rows)


def run_capture(rate: float, duration: float, output: str) -> None:
    # Lifecycle: cleanup -> configure -> rate -> activate -> capture -> deactivate
    print(f"[capture] cleanup", flush=True)
    _api("/api/v1/lifecycle/cleanup")
    time.sleep(1.0)
    print(f"[capture] configure", flush=True)
    _api("/api/v1/lifecycle/configure", {"scenario_id": "imazu-01-ho"})
    time.sleep(2.0)
    print(f"[capture] rate={rate}", flush=True)
    _api("/api/v1/lifecycle/rate", {"rate": rate})
    time.sleep(0.5)
    print(f"[capture] activate", flush=True)
    _api("/api/v1/lifecycle/activate")
    time.sleep(0.5)

    rclpy.init()
    node = CaptureNode(rate=rate, duration=duration, output=output)
    while rclpy.ok() and not node.done():
        rclpy.spin_once(node, timeout_sec=0.1)

    node.save()
    print(f"[capture] captured {len(node.rows)} rows -> {output}", flush=True)
    node.destroy_node()
    rclpy.shutdown()

    print(f"[capture] deactivate", flush=True)
    _api("/api/v1/lifecycle/deactivate")
    time.sleep(1.0)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--rate", type=float, default=1.0)
    parser.add_argument("--duration", type=float, default=120.0,
                        help="Wall-clock seconds to capture")
    parser.add_argument("--output", default="/tmp/capture.csv")
    args = parser.parse_args()
    run_capture(args.rate, args.duration, args.output)
