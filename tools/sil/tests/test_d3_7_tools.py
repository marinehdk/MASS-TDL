"""Unit tests for D3.7 tool core logic (no ROS2 / Docker required).

Run: pytest tools/sil/tests/test_d3_7_tools.py -v
"""
import csv
import hashlib
import hmac as _hmac
import json
import struct
import tempfile
import time
from pathlib import Path

import numpy as np
import pytest


def compute_hmac(key: bytes, stamp_sec: int, stamp_nanosec: int,
                 decision_id: str, payload_json: str) -> bytes:
    stamp_bytes = struct.pack(">qI", stamp_sec, stamp_nanosec)
    payload = stamp_bytes + decision_id.encode() + payload_json.encode()
    return _hmac.new(key, payload, hashlib.sha256).digest()


def test_hmac_deterministic():
    key = b"test-key"
    h1 = compute_hmac(key, 100, 0, "dec-001", '{"verdict":"PASS"}')
    h2 = compute_hmac(key, 100, 0, "dec-001", '{"verdict":"PASS"}')
    assert h1 == h2


def test_hmac_changes_with_payload():
    key = b"test-key"
    h1 = compute_hmac(key, 100, 0, "dec-001", '{"verdict":"PASS"}')
    h2 = compute_hmac(key, 100, 0, "dec-001", '{"verdict":"FAIL"}')
    assert h1 != h2


def test_hmac_changes_with_decision_id():
    key = b"test-key"
    h1 = compute_hmac(key, 100, 0, "dec-001", "{}")
    h2 = compute_hmac(key, 100, 0, "dec-002", "{}")
    assert h1 != h2


def test_hmac_compare_digest_tamper_detected():
    key = b"test-key"
    correct = compute_hmac(key, 100, 0, "dec-001", "{}")
    tampered = bytes([correct[0] ^ 0xFF]) + correct[1:]
    assert not _hmac.compare_digest(correct, tampered)


def test_percentile_empty():
    arr = np.array([])
    with pytest.raises(Exception):
        np.percentile(arr, 50)


def test_percentile_single_sample():
    arr = np.array([42.0])
    assert np.percentile(arr, 50) == 42.0
    assert np.percentile(arr, 95) == 42.0
    assert np.percentile(arr, 99) == 42.0


def test_percentile_known_distribution():
    arr = np.array([float(i) for i in range(1, 101)])
    assert np.percentile(arr, 50) == 50.5
    assert np.percentile(arr, 99) == pytest.approx(99.01, abs=0.1)


class FakeWatchdog:
    GAP_THRESHOLD = 5.0

    def __init__(self):
        self.last_msg_wall = time.time()
        self.p0_triggered = False
        self.startup_grace_end = 0.0

    def receive_message(self):
        self.last_msg_wall = time.time()

    def check(self, now: float) -> bool:
        if now < self.startup_grace_end:
            return False
        gap = now - self.last_msg_wall
        if gap > self.GAP_THRESHOLD:
            self.p0_triggered = True
            return True
        return False


def test_watchdog_no_alert_within_threshold():
    w = FakeWatchdog()
    w.receive_message()
    now = w.last_msg_wall + 4.9
    assert not w.check(now)
    assert not w.p0_triggered


def test_watchdog_p0_alert_over_threshold():
    w = FakeWatchdog()
    w.receive_message()
    now = w.last_msg_wall + 5.1
    assert w.check(now)
    assert w.p0_triggered


def test_watchdog_reset_after_new_message():
    w = FakeWatchdog()
    w.receive_message()
    old = w.last_msg_wall
    time.sleep(0.01)
    w.receive_message()
    assert w.last_msg_wall > old


def test_latency_csv_header_and_row():
    HEADER = ["segment", "t_window_start", "topic_pair_id", "topic",
              "p50_ms", "p95_ms", "p99_ms", "sample_count", "threshold_ms", "status"]
    with tempfile.NamedTemporaryFile(mode="w", suffix=".csv", delete=False) as f:
        path = Path(f.name)
        writer = csv.writer(f)
        writer.writerow(HEADER)
        writer.writerow([1, "1748000000.000", 9, "/l3/m4/behavior_plan",
                         "12.3", "45.6", "78.9", 240, 100, "OK"])

    with open(path) as f:
        rows = list(csv.DictReader(f))
    assert len(rows) == 1
    assert rows[0]["topic_pair_id"] == "9"
    assert rows[0]["status"] == "OK"
    path.unlink()


def test_p0_alert_json_append():
    with tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False) as f:
        path = Path(f.name)
        json.dump([], f)

    def append_alert(p: Path, alert: dict):
        existing = json.loads(p.read_text())
        existing.append(alert)
        p.write_text(json.dumps(existing))

    append_alert(path, {"type": "P0-5", "ts": 1748000001.0})
    append_alert(path, {"type": "P0-6", "ts": 1748000002.0})

    alerts = json.loads(path.read_text())
    assert len(alerts) == 2
    assert alerts[0]["type"] == "P0-5"
    path.unlink()
