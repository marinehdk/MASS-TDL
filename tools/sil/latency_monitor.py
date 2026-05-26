#!/usr/bin/env python3
"""§15.2 24-row DDS end-to-end latency probe — independent ROS2 subscriber node.

IEC 61508 note: subscriber-only, publishes nothing to decision bus.
Latency = wall-clock time received by this monitor - msg.stamp (publisher clock).
Clock sync: all Docker containers share host clock (OrbStack default ≤1ms jitter).

Usage (inside SIL Docker or after sourcing ROS2):
    python3 tools/sil/latency_monitor.py \
        --evidence-dir docs/Design/Phase\ 3/D3.7-sil-8module-integration/evidence \
        --state-file /tmp/d37_orch_state.json
"""
from __future__ import annotations

import argparse
import csv
import sys
import time
import threading
from collections import defaultdict
from pathlib import Path

import numpy as np
import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, QoSReliabilityPolicy, QoSDurabilityPolicy, QoSHistoryPolicy

# §15.2 interface matrix: (pair_id, topic, threshold_ms, label)
# Rows sharing a topic get the same measurement duplicated in CSV.
INTERFACE_MATRIX = [
    (1,  "/l3/m1/odd_state",         100, "M1→M4 ODDState"),
    (2,  "/l3/m1/odd_state",         100, "M1→M6 ODDState"),
    (3,  "/l3/m1/odd_state",         100, "M1→M7 ODDState"),
    (4,  "/l3/m2/world_model",        50, "M2→M4 WorldModel"),
    (5,  "/l3/m2/world_model",        50, "M2→M5 WorldModel"),
    (6,  "/l3/m2/world_model",        50, "M2→M6 WorldModel"),
    (7,  "/l3/m2/world_model",        50, "M2→M7 WorldModel"),
    (8,  "/l3/m3/mission_plan",      200, "M3→M4 MissionState"),
    (9,  "/l3/m4/behavior_plan",     100, "M4→M5 BehaviorPlan"),
    (10, "/sil/sat2_data",           100, "M4→M8 SAT2Data"),
    (11, "/l3/m5/avoidance_plan",   1000, "M5→L4 AvoidancePlan"),
    (12, "/sil/sat3_data",           100, "M5→M8 SAT3Data"),
    (13, "/l3/m5/avoidance_plan",   1000, "M5→M7 AvoidancePlan"),
    (14, "/l3/m6/colregs_advice",    100, "M6→M4 COLREGs"),
    (15, "/sil/sat2_data",           100, "M6→M8 colregs_chain"),
    (16, "/m7/safety_verdict",        50, "M7→M1 VETO event"),
    (17, "/m7/sil_observability",     50, "M7→M8 observability"),
    (18, "/sil/sotif_metrics",       100, "M7→M8 SOTIFMetrics"),
    (19, "/sil/hmi_transparency",     20, "M8→Shore HMI"),
    (20, "/fusion/nav_state",         30, "Fusion→M2"),
    (21, "/fusion/nav_state",         30, "Fusion→M7"),
    (22, "/reflex/override_cmd",     200, "Y-axis Reflex event"),
    (23, "/params/vessel_config",    500, "ParamDB→M1"),
    (24, "/asdr/decision_log",       100, "ASDR→Shore event"),
]

TOPIC_TO_PAIRS: dict[str, list[tuple]] = defaultdict(list)
for _pid, _topic, _thr, _desc in INTERFACE_MATRIX:
    TOPIC_TO_PAIRS[_topic].append((_pid, _thr, _desc))

CSV_HEADER = [
    "segment", "t_window_start", "topic_pair_id", "topic",
    "p50_ms", "p95_ms", "p99_ms", "sample_count", "threshold_ms", "status",
]
WINDOW_SECS = 60


def _stamp_to_ns(msg) -> int | None:
    """Extract stamp as nanoseconds. Returns None if msg has no stamp field."""
    stamp = getattr(msg, "stamp", None)
    if stamp is None:
        hdr = getattr(msg, "header", None)
        stamp = getattr(hdr, "stamp", None) if hdr else None
    if stamp is None:
        return None
    return int(stamp.sec) * 1_000_000_000 + int(stamp.nanosec)


def _current_segment(state_file: Path | None) -> int:
    """Read segment number from orchestrator state file; default 1."""
    if state_file and state_file.exists():
        import json
        try:
            return int(json.loads(state_file.read_text()).get("segment", 1))
        except Exception:
            pass
    return 1


class LatencyMonitor(Node):
    def __init__(self, evidence_dir: Path, state_file: Path | None):
        super().__init__("latency_monitor_d37")
        self._evidence_dir = evidence_dir
        self._state_file = state_file
        self._csv_path = evidence_dir / "latency_8h.csv"
        self._samples: dict[str, list[float]] = defaultdict(list)  # topic → [latency_ms...]
        self._lock = threading.Lock()
        self._window_start = time.time()

        evidence_dir.mkdir(parents=True, exist_ok=True)
        if not self._csv_path.exists():
            with open(self._csv_path, "w", newline="") as f:
                csv.writer(f).writerow(CSV_HEADER)

        self._subscribe_all()
        self._flush_timer = self.create_timer(WINDOW_SECS, self._flush_window)
        self.get_logger().info(f"LatencyMonitor: {len(TOPIC_TO_PAIRS)} topics → {self._csv_path}")

    def _subscribe_all(self):
        qos = QoSProfile(
            reliability=QoSReliabilityPolicy.BEST_EFFORT,
            durability=QoSDurabilityPolicy.VOLATILE,
            history=QoSHistoryPolicy.KEEP_LAST, depth=10,
        )
        type_map = self._build_type_map()
        for topic, msg_cls in type_map.items():
            try:
                self.create_subscription(
                    msg_cls, topic,
                    lambda msg, t=topic: self._record(msg, t),
                    qos,
                )
            except Exception as e:
                self.get_logger().warn(f"SKIP {topic}: {e}")

    def _build_type_map(self) -> dict[str, type]:
        result = {}
        def _try(topic: str, pkg: str, cls: str):
            try:
                mod = __import__(f"{pkg}.msg", fromlist=[cls])
                result[topic] = getattr(mod, cls)
            except (ImportError, AttributeError) as e:
                self.get_logger().warn(f"  type not found for {topic}: {e}")

        _try("/l3/m1/odd_state",       "l3_msgs", "ODDState")
        _try("/l3/m2/world_model",     "l3_msgs", "WorldState")
        _try("/l3/m3/mission_plan",    "l3_msgs", "MissionState")
        _try("/l3/m4/behavior_plan",   "l3_msgs", "BehaviorPlan")
        _try("/l3/m5/avoidance_plan",  "l3_msgs", "AvoidancePlan")
        _try("/sil/sat2_data",         "l3_msgs", "SATData")
        _try("/sil/sat3_data",         "l3_msgs", "SATData")
        _try("/l3/m6/colregs_advice",  "l3_msgs", "COLREGsConstraint")
        _try("/m7/safety_verdict",     "l3_msgs", "SafetyVerdict")
        _try("/m7/sil_observability",  "l3_msgs", "M7Observability")
        _try("/sil/sotif_metrics",     "l3_msgs", "SotifMetrics")
        _try("/sil/hmi_transparency",  "l3_msgs", "UIState")
        _try("/fusion/nav_state",      "l3_external_msgs", "FilteredOwnShipState")
        _try("/reflex/override_cmd",   "l3_msgs", "ReactiveOverrideCmd")
        _try("/params/vessel_config",  "l3_msgs", "ODDState")
        _try("/asdr/decision_log",     "sil_msgs", "ASDREvent")
        return result

    def _record(self, msg, topic: str):
        pub_ns = _stamp_to_ns(msg)
        if pub_ns is None:
            return
        recv_ns = time.time_ns()
        latency_ms = (recv_ns - pub_ns) / 1_000_000.0
        if latency_ms < 0 or latency_ms > 60_000:
            return
        with self._lock:
            self._samples[topic].append(latency_ms)

    def _flush_window(self):
        t_start = self._window_start
        self._window_start = time.time()
        segment = _current_segment(self._state_file)
        rows = []
        with self._lock:
            for topic, pairs in TOPIC_TO_PAIRS.items():
                samples = self._samples.pop(topic, [])
                if not samples:
                    arr = np.array([])
                    p50 = p95 = p99 = None
                else:
                    arr = np.array(samples)
                    p50 = float(np.percentile(arr, 50))
                    p95 = float(np.percentile(arr, 95))
                    p99 = float(np.percentile(arr, 99))
                for pair_id, threshold_ms, _ in pairs:
                    if p99 is not None:
                        status = "OK" if p99 <= threshold_ms else "OVER"
                    else:
                        status = "NO_DATA"
                    rows.append([
                        segment,
                        f"{t_start:.3f}",
                        pair_id, topic,
                        f"{p50:.2f}" if p50 is not None else "",
                        f"{p95:.2f}" if p95 is not None else "",
                        f"{p99:.2f}" if p99 is not None else "",
                        len(samples),
                        threshold_ms,
                        status,
                    ])
        with open(self._csv_path, "a", newline="") as f:
            csv.writer(f).writerows(rows)
        self.get_logger().info(f"[window] wrote {len(rows)} rows to latency_8h.csv")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--evidence-dir", required=True, type=Path)
    ap.add_argument("--state-file", type=Path, default=None,
                    help="Orchestrator state JSON for segment tracking")
    args = ap.parse_args()
    rclpy.init()
    node = LatencyMonitor(args.evidence_dir, args.state_file)
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
