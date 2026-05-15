"""Tests for MultiThreadedExecutor threading model in sil orchestrator.

Ref: docs/superpowers/plans/2026-05-15-gap-015-sil-fixes.md Task 1.2
"""
import inspect
import sys
import pytest


class TestThreadingModel:
    """Verify orchestrator uses MultiThreadedExecutor with ReentrantCallbackGroup."""

    def test_executor_is_multi_threaded(self):
        """main.py _spin_bridge should configure MultiThreadedExecutor, not SingleThreaded."""
        import sil_orchestrator.main as m
        source = inspect.getsource(m._spin_bridge)
        assert "MultiThreadedExecutor" in source, (
            f"Expected MultiThreadedExecutor, got:\n{source}"
        )

    def test_reentrant_callback_group_configured(self):
        """main.py should create ReentrantCallbackGroup and pass to LifecycleBridge."""
        import sil_orchestrator.main as m
        source = inspect.getsource(inspect.getmodule(m))
        assert "ReentrantCallbackGroup" in source, (
            f"Expected ReentrantCallbackGroup in main module"
        )
        assert "callback_group" in source, (
            f"Expected callback_group parameter passed to LifecycleBridge"
        )

    def test_no_telemetry_bridge_reference(self):
        """After GAP-015 retirement, zero telemetry_bridge references in main module."""
        import sil_orchestrator.main as m
        source = inspect.getsource(inspect.getmodule(m))
        assert "telemetry_bridge" not in source, (
            "telemetry_bridge reference found — GAP-015 not fully closed"
        )

    def test_lifecycle_bridge_accepts_callback_group(self):
        """LifecycleBridge.__init__ accepts optional callback_group parameter."""
        from sil_orchestrator.lifecycle_bridge import LifecycleBridge
        sig = inspect.signature(LifecycleBridge.__init__)
        assert "callback_group" in sig.parameters, (
            f"LifecycleBridge.__init__ missing callback_group param: {sig}"
        )

    def test_p99_latency_under_22ms(self):
        """Lab test: MultiThreadedExecutor p99 latency < 22ms at 50Hz.

        Requires rclpy and a running ROS2 domain. Skipped on macOS / CI.
        """
        pytest.importorskip("rclpy")
        import rclpy
        if not rclpy.ok():
            pytest.skip("rclpy not initialized")
        import time
        from rclpy.executors import MultiThreadedExecutor
        from sil_orchestrator.lifecycle_bridge import LifecycleBridge

        bridge = LifecycleBridge()
        executor = MultiThreadedExecutor(num_threads=4)
        executor.add_node(bridge)

        latencies = []
        for _ in range(100):
            t0 = time.perf_counter()
            executor.spin_once(timeout_sec=0.02)
            t1 = time.perf_counter()
            latencies.append((t1 - t0) * 1000)

        p99 = sorted(latencies)[int(len(latencies) * 0.99)]
        assert p99 < 22.0, f"p99 latency {p99:.1f}ms exceeds 22ms threshold"
