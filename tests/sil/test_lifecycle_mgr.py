"""Unit tests for lifecycle FSM logic (offline -- no ROS2 runtime needed)."""
import pytest
from sil_lifecycle.lifecycle_mgr import ScenarioLifecycleMgr, LifecycleState


@pytest.fixture
def mgr():
    return ScenarioLifecycleMgr(tick_hz=50.0)


class TestInitialState:
    def test_starts_unconfigured(self, mgr):
        assert mgr.current_state == LifecycleState.UNCONFIGURED

    def test_scenario_id_empty(self, mgr):
        assert mgr.scenario_id == ""

    def test_sim_time_zero(self, mgr):
        assert mgr.sim_time == 0.0

    def test_sim_rate_defaults_to_one(self, mgr):
        assert mgr.sim_rate == 1.0


class TestValidTransitions:
    def test_configure_from_unconfigured(self, mgr):
        assert mgr.configure("r14-head-on", "abc123")
        assert mgr.current_state == LifecycleState.INACTIVE
        assert mgr.scenario_id == "r14-head-on"
        assert mgr.scenario_hash == "abc123"

    def test_full_lifecycle_flow(self, mgr):
        assert mgr.configure("test", "hash")
        assert mgr.activate()
        assert mgr.current_state == LifecycleState.ACTIVE
        assert mgr.deactivate()
        assert mgr.current_state == LifecycleState.INACTIVE
        assert mgr.cleanup()
        assert mgr.current_state == LifecycleState.UNCONFIGURED
        assert mgr.scenario_id == ""


class TestGuardConditions:
    def test_cannot_activate_from_unconfigured(self, mgr):
        assert mgr.activate() is False

    def test_cannot_deactivate_from_unconfigured(self, mgr):
        assert mgr.deactivate() is False

    def test_cannot_deactivate_from_inactive(self, mgr):
        mgr.configure("test")
        assert mgr.activate()
        assert mgr.deactivate()
        # Second deactivate should fail (now INACTIVE)
        assert mgr.deactivate() is False

    def test_cannot_configure_from_active(self, mgr):
        mgr.configure("test")
        mgr.activate()
        assert mgr.configure("test2") is False

    def test_cannot_activate_twice(self, mgr):
        mgr.configure("test")
        mgr.activate()
        assert mgr.activate() is False

    def test_cannot_cleanup_from_active(self, mgr):
        mgr.configure("test")
        mgr.activate()
        assert mgr.cleanup() is False


class TestTickAndTime:
    def test_tick_increments_sim_time_when_active(self, mgr):
        mgr.configure("test")
        mgr.activate()
        mgr.tick()
        expected_dt = 1.0 / 50.0
        assert mgr.sim_time == pytest.approx(expected_dt)

    def test_tick_does_not_increment_when_inactive(self, mgr):
        mgr.configure("test")
        mgr.tick()
        assert mgr.sim_time == 0.0

    def test_tick_respects_sim_rate(self, mgr):
        mgr.configure("test")
        mgr.activate()
        mgr.set_sim_rate(2.0)
        mgr.tick()
        assert mgr.sim_time == pytest.approx(2.0 / 50.0)

    def test_set_sim_rate_rejects_negative(self, mgr):
        assert mgr.set_sim_rate(-1.0) is False


class TestGetStatus:
    def test_status_after_configure(self, mgr):
        mgr.configure("r14", "sha")
        status = mgr.get_status_dict()
        assert status["current_state"] == "INACTIVE"
        assert status["scenario_id"] == "r14"
        assert status["scenario_hash"] == "sha"
        assert status["sim_time"] == 0.0
