"""
timing_verifier.py — ROS2-based latency measurement utilities for MASS L3 E3 benchmarks.

Records inter-message latencies from ROS2 topics, computes percentiles (P50/P95/P99)
from a rolling sample buffer, and provides round-trip and topic-rate helpers.

No external dependencies beyond the Python stdlib and rclpy.
"""

from __future__ import annotations

import statistics
import threading
import time
from collections import deque
from typing import Any, Optional


class TimingVerifier:
    """
    Measures latency between ROS2 topic events.

    Parameters
    ----------
    node :
        An rclpy Node used for creating subscriptions and publishers.
    buffer_size :
        Maximum number of latency samples retained in the rolling buffer.
    """

    def __init__(self, node: Any, buffer_size: int = 100) -> None:
        self._node = node
        self._buffer_size = buffer_size
        self._samples: deque[float] = deque(maxlen=buffer_size)

    # ------------------------------------------------------------------
    # Public measurement API
    # ------------------------------------------------------------------

    def measure_round_trip(
        self,
        trigger_topic: str,
        response_topic: str,
        trigger_msg: Any,
        timeout_s: float,
        trigger_msg_type: Any,
        response_msg_type: Any,
    ) -> Optional[float]:
        """
        Publish *trigger_msg* on *trigger_topic* and wait for the first
        message to arrive on *response_topic*.

        Returns the elapsed wall-clock latency in seconds, or ``None`` if
        no response arrives within *timeout_s*.

        The latency sample is appended to the internal rolling buffer.
        """
        arrived = threading.Event()
        t_response: list[float] = []

        def _response_cb(_msg: Any) -> None:  # noqa: ANN001
            if not arrived.is_set():
                t_response.append(time.monotonic())
                arrived.set()

        try:
            pub = self._node.create_publisher(trigger_msg_type, trigger_topic, 10)
            sub = self._node.create_subscription(
                response_msg_type, response_topic, _response_cb, 10
            )
        except Exception:  # noqa: BLE001
            return None

        try:
            t0 = time.monotonic()
            pub.publish(trigger_msg)

            deadline = t0 + timeout_s
            while not arrived.is_set() and time.monotonic() < deadline:
                try:
                    import rclpy
                    rclpy.spin_once(self._node, timeout_sec=0.01)
                except Exception:  # noqa: BLE001
                    time.sleep(0.01)

            if not arrived.is_set():
                return None

            latency = t_response[0] - t0
            self._samples.append(latency)
            return latency
        finally:
            try:
                self._node.destroy_publisher(pub)
                self._node.destroy_subscription(sub)
            except Exception:  # noqa: BLE001
                pass

    def measure_topic_rate(self, topic: str, duration_s: float, msg_type: Any) -> float:
        """
        Count messages received on *topic* for *duration_s* seconds.

        Returns the observed message rate in Hz.
        """
        count: list[int] = [0]

        def _cb(_msg: Any) -> None:  # noqa: ANN001
            count[0] += 1

        sub = None
        try:
            sub = self._node.create_subscription(msg_type, topic, _cb, 10)
        except Exception:  # noqa: BLE001
            return 0.0

        t_start = time.monotonic()
        t_end = t_start + duration_s
        try:
            while time.monotonic() < t_end:
                try:
                    import rclpy
                    rclpy.spin_once(self._node, timeout_sec=0.05)
                except Exception:  # noqa: BLE001
                    time.sleep(0.05)
        finally:
            try:
                self._node.destroy_subscription(sub)
            except Exception:  # noqa: BLE001
                pass

        elapsed = time.monotonic() - t_start
        if elapsed <= 0.0:
            return 0.0
        return count[0] / elapsed

    # ------------------------------------------------------------------
    # Statistics helpers
    # ------------------------------------------------------------------

    def get_percentile(self, samples: list[float], p: float) -> float:
        """
        Return the *p*-th percentile (0–100) of *samples* using a
        nearest-rank method (no numpy required).

        Raises ``ValueError`` if *samples* is empty.
        """
        if not samples:
            raise ValueError("Cannot compute percentile of empty sample list")
        sorted_s = sorted(samples)
        n = len(sorted_s)
        if n == 1:
            return sorted_s[0]
        # nearest-rank: index = ceil(p/100 * n) - 1, clamped
        idx = max(0, min(n - 1, int((p / 100.0) * n + 0.5) - 1))
        return sorted_s[idx]

    def p99(self) -> float:
        """P99 of the internal rolling buffer."""
        return self.get_percentile(list(self._samples), 99)

    def p95(self) -> float:
        """P95 of the internal rolling buffer."""
        return self.get_percentile(list(self._samples), 95)

    def p50(self) -> float:
        """P50 (median) of the internal rolling buffer."""
        return self.get_percentile(list(self._samples), 50)

    def clear(self) -> None:
        """Reset the rolling sample buffer."""
        self._samples.clear()
