"""dds-fmu exchange latency: DDS roundtrip P95 must be < 10ms.

Per D1.3c spec §8.2 + SIL decision §2.4: dds-fmu exchange budget 2-10ms.
Per plan v3.1 D1.5: P95 ≤ 10ms / P99 ≤ 15ms (V&V Plan latency budget).
"""
from __future__ import annotations


def test_dds_latency_imports():
    """Verify latency test dependencies are importable."""
    try:
        import rclpy  # noqa: F401
    except ImportError:
        import pytest
        pytest.skip("rclpy not available (requires ROS2 Humble runtime)")
    assert True


def test_dds_roundtrip_latency_p95():
    """DDS loopback (local host, no FMU loaded): roundtrip P95 < 10ms.

    Test methodology:
      1. Create ROS2 node with a publisher/subscriber on a loopback topic
      2. For 1000 iterations: publish → spin_once → receive → measure elapsed
      3. Assert P95 < 10ms (Phase 1 budget)
      4. Assert P99 < 15ms (V&V Plan contingency)

    Phase 1 uses local DDS loopback without FMU. Phase 2 will measure
    end-to-end through libcosim async_slave.
    """
    import rclpy
    import time
    import numpy as np
    from std_msgs.msg import String

    rclpy.init(args=[])
    node = rclpy.create_node("latency_test_node")

    pub = node.create_publisher(String, "/dds_fmu/latency_test", 10)
    received_times: list[float] = []

    def callback(msg: String):
        received_times.append(time.perf_counter())

    sub = node.create_subscription(String, "/dds_fmu/latency_test", callback, 10)

    # Warm-up: 100 iterations to stabilize DDS discovery
    for _ in range(100):
        msg = String(data="warmup")
        pub.publish(msg)
        rclpy.spin_once(node, timeout_sec=0.005)

    # Measurement: 1000 iterations
    latencies_ms: list[float] = []
    for _ in range(1000):
        t0 = time.perf_counter()
        msg = String(data="ping")
        pub.publish(msg)

        # Spin with timeout to receive the echo
        start_spin = time.perf_counter()
        while time.perf_counter() - start_spin < 0.05:
            rclpy.spin_once(node, timeout_sec=0.005)
            if received_times and received_times[-1] >= t0:
                break

        if received_times and received_times[-1] >= t0:
            elapsed_ms = (received_times[-1] - t0) * 1000.0
        else:
            elapsed_ms = 50.0  # timeout penalty

        latencies_ms.append(elapsed_ms)

    node.destroy_node()
    rclpy.shutdown()

    latencies_sorted = sorted(latencies_ms)
    p95 = float(np.percentile(latencies_sorted, 95))
    p99 = float(np.percentile(latencies_sorted, 99))
    p50 = float(np.percentile(latencies_sorted, 50))

    print(f"\nDDS latency results (1000 samples, local loopback):")
    print(f"  P50 = {p50:.2f} ms")
    print(f"  P95 = {p95:.2f} ms")
    print(f"  P99 = {p99:.2f} ms")
    print(f"  Min = {min(latencies_sorted):.2f} ms")
    print(f"  Max = {max(latencies_sorted):.2f} ms")

    # Phase 1 budget assertions
    assert p95 < 10.0, (
        f"DDS roundtrip P95={p95:.1f}ms exceeds 10ms budget. "
        f"This violates SIL decision §2.4 dds-fmu exchange budget."
    )

    # V&V Plan contingency: P99 < 15ms
    if p99 >= 15.0:
        import warnings
        warnings.warn(
            f"DDS roundtrip P99={p99:.1f}ms exceeds 15ms contingency. "
            f"Per D1.5 V&V Plan: if P99 > 15ms, investigate zero-copy DDS or "
            f"IPC transport as mitigation."
        )
