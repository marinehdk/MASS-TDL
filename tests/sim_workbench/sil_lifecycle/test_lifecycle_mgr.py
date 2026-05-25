"""Unit tests for ScenarioLifecycleMgr including dynamics_mode."""
from __future__ import annotations
from sil_lifecycle.lifecycle_mgr import ScenarioLifecycleMgr, LifecycleState


def test_dynamics_mode_defaults_to_internal():
    mgr = ScenarioLifecycleMgr()
    assert mgr.dynamics_mode == "internal"


def test_configure_sets_dynamics_mode():
    mgr = ScenarioLifecycleMgr()
    assert mgr.configure("scenario_001", dynamics_mode="fmi")
    assert mgr.dynamics_mode == "fmi"
    assert mgr.current_state == LifecycleState.INACTIVE


def test_configure_rejects_invalid_mode():
    mgr = ScenarioLifecycleMgr()
    assert not mgr.configure("scenario_001", dynamics_mode="invalid")
    assert mgr.current_state == LifecycleState.UNCONFIGURED


def test_set_dynamics_mode_only_in_inactive():
    mgr = ScenarioLifecycleMgr()
    assert mgr.configure("scenario_001")
    assert mgr.activate()
    assert not mgr.set_dynamics_mode("fmi")  # rejected in ACTIVE


def test_set_dynamics_mode_accepts_in_inactive():
    mgr = ScenarioLifecycleMgr()
    assert mgr.configure("scenario_001")
    assert mgr.set_dynamics_mode("fmi")  # accepted in INACTIVE
    assert mgr.dynamics_mode == "fmi"


def test_status_dict_includes_dynamics_mode():
    mgr = ScenarioLifecycleMgr()
    mgr.configure("scenario_001", dynamics_mode="fmi")
    status = mgr.get_status_dict()
    assert status["dynamics_mode"] == "fmi"
