"""Phase 1 E1.8: M7 IEC 61508 watchdog monitor Python white-box tests.

Mirrors C++ WatchdogMonitor semantics (watchdog_monitor.hpp):
  - Startup grace: no message received yet → loss_count=0, no critical.
  - Each evaluate() call past the module's timeout increments loss_count by 1.
  - heartbeat_ok[i] is False when loss_count[i] STRICTLY GREATER than tolerance_count[i].
  - on_message_received() resets loss_count[i] to 0.
"""
from tests.m7.watchdog_stub import MonitoredModule, WatchdogConfig, WatchdogMonitor

T0 = 10_000.0  # ms, arbitrary base time for test reproducibility


def _monitor(timeout_ms: int = 300, tolerance: int = 3) -> WatchdogMonitor:
    cfg = WatchdogConfig(
        timeout_ms=[float(timeout_ms)] * int(MonitoredModule.COUNT),
        tolerance_count=[tolerance] * int(MonitoredModule.COUNT),
    )
    return WatchdogMonitor(cfg)


def test_m7_watchdog_startup_grace_no_loss():
    """Before any message: startup grace → loss_count=0, any_critical=False for all modules."""
    mon = _monitor()
    result = mon.evaluate(T0 + 10_000.0)  # 10 s past, no messages received
    for i in range(int(MonitoredModule.COUNT)):
        assert result.heartbeat_ok[i], f"module {i} must be OK during startup grace"
        assert result.loss_count[i] == 0
    assert not result.any_critical
    assert result.critical_count == 0


def test_m7_watchdog_within_timeout_heartbeat_ok():
    """After first message, evaluate within timeout window: heartbeat_ok stays True."""
    mon = _monitor(timeout_ms=300, tolerance=3)
    mon.on_message_received(MonitoredModule.M1, T0)
    result = mon.evaluate(T0 + 200.0)  # 200 ms < 300 ms timeout
    assert result.heartbeat_ok[MonitoredModule.M1]
    assert result.loss_count[MonitoredModule.M1] == 0


def test_m7_watchdog_timeout_increments_loss():
    """Each evaluate() call past timeout increments loss_count by 1."""
    mon = _monitor(timeout_ms=300, tolerance=10)
    mon.on_message_received(MonitoredModule.M2, T0)
    result1 = mon.evaluate(T0 + 400.0)  # 400 ms > 300 ms → loss_count = 1
    assert result1.loss_count[MonitoredModule.M2] == 1
    assert result1.heartbeat_ok[MonitoredModule.M2]  # tolerance=10, not critical yet
    result2 = mon.evaluate(T0 + 800.0)  # another timeout → loss_count = 2
    assert result2.loss_count[MonitoredModule.M2] == 2


def test_m7_watchdog_exceeds_tolerance_goes_critical():
    """loss_count STRICTLY GREATER than tolerance → heartbeat_ok=False, any_critical=True."""
    mon = _monitor(timeout_ms=300, tolerance=1)
    mon.on_message_received(MonitoredModule.M1, T0)
    mon.evaluate(T0 + 400.0)  # loss_count = 1 == tolerance → still heartbeat_ok
    result = mon.evaluate(T0 + 800.0)  # loss_count = 2 > tolerance=1 → critical
    assert not result.heartbeat_ok[MonitoredModule.M1]
    assert result.any_critical
    assert result.critical_count >= 1


def test_m7_watchdog_recovery_resets_loss():
    """on_message_received() resets loss_count to 0 and clears critical state."""
    mon = _monitor(timeout_ms=300, tolerance=0)
    mon.on_message_received(MonitoredModule.M3, T0)
    mon.evaluate(T0 + 400.0)  # loss_count = 1 > tolerance=0 → critical
    mon.on_message_received(MonitoredModule.M3, T0 + 500.0)  # recovery
    result = mon.evaluate(T0 + 600.0)  # 100 ms since last message → within timeout
    assert result.heartbeat_ok[MonitoredModule.M3]
    assert result.loss_count[MonitoredModule.M3] == 0