"""Phase 1 E1.8: M7 IEC 61508 watchdog monitor white-box tests."""
import time
import pytest
from tests.m7.watchdog_stub import WatchdogMonitor, WatchdogConfig


@pytest.fixture
def wd():
    return WatchdogMonitor(WatchdogConfig(grace_period_s=0.5, heartbeat_interval_s=0.2, max_missed=3))


def test_register_and_initialize(wd):
    wd.init_8_modules()
    assert len(wd.modules) == 8
    assert all(f"M{i}" in wd.modules for i in range(1, 9))


def test_grace_period_no_timeout(wd):
    wd.init_8_modules()
    wd.heartbeat("M1")
    time.sleep(0.1)
    assert wd.check_timeout("M1") == False
    assert wd.modules["M1"].in_grace == True


def test_grace_period_expires(wd):
    wd.init_8_modules()
    wd.heartbeat("M1")
    time.sleep(0.6)
    wd.heartbeat("M1")
    assert wd.modules["M1"].in_grace == False
    assert wd.modules["M1"].missed_count == 0


def test_loss_counting_triggers_mrc(wd):
    wd.init_8_modules()
    wd.heartbeat("M1")
    time.sleep(0.6)
    wd.heartbeat("M1")
    assert wd.modules["M1"].in_grace == False
    # Wait past heartbeat_interval so first check sees a timeout
    time.sleep(0.25)
    for _ in range(3):
        wd.check_timeout("M1")
        time.sleep(0.25)
    assert wd.modules["M1"].mrc_triggered == True
    assert wd.any_mrc() == True


def test_recovery_after_healthy(wd):
    wd.init_8_modules()
    wd.heartbeat("M1")
    time.sleep(0.6)
    wd.heartbeat("M1")
    time.sleep(0.25)
    for _ in range(3):
        wd.check_timeout("M1")
        time.sleep(0.25)
    assert wd.modules["M1"].mrc_triggered == True
    for _ in range(2):
        wd.heartbeat("M1")
        time.sleep(0.1)
    assert wd.modules["M1"].mrc_triggered == False


def test_check_all_modules(wd):
    wd.init_8_modules()
    result = wd.check_all()
    assert len(result) == 8
    assert all(isinstance(v, bool) for v in result.values())


def test_double_init_idempotent(wd):
    wd.init_8_modules()
    wd.init_8_modules()
    assert len(wd.modules) == 8


def test_unknown_module_no_crash(wd):
    assert wd.check_timeout("UNKNOWN") == False
    wd.heartbeat("UNKNOWN")
