# test/test_target_modes.py
import numpy as np
import pytest
from scenario_authoring.replay.target_modes import (
    AisReplayVessel,
    IntelligentVessel,
    NcdmVessel,
    TargetShipReplayer,
)


def test_ais_replay_vessel_instantiable():
    """AisReplayVessel can be constructed with a trajectory."""
    traj = np.column_stack([
        np.arange(0.0, 10.0, 0.02),  # t
        np.linspace(63.0, 63.001, 500),  # lat
        np.linspace(5.0, 5.001, 500),  # lon
        np.full(500, 10.0),  # sog
        np.full(500, 90.0),  # cog
    ])
    replayer = AisReplayVessel(traj)
    assert replayer is not None
    assert isinstance(replayer, TargetShipReplayer)
    state = replayer.get_targets_at(1.0)
    assert state is not None
    assert "lat" in state
    assert "lon" in state


def test_ais_replay_out_of_range_returns_none():
    """get_targets_at returns None when time is before or after trajectory."""
    traj = np.column_stack([
        np.arange(0.0, 10.0, 0.02),
        np.linspace(63.0, 63.001, 500),
        np.linspace(5.0, 5.001, 500),
        np.full(500, 10.0),
        np.full(500, 90.0),
    ])
    replayer = AisReplayVessel(traj)
    assert replayer.get_targets_at(-1.0) is None  # before start
    assert replayer.get_targets_at(100.0) is None  # after end


def test_ncdm_vessel_is_implemented():
    from scenario_authoring.replay.target_modes import NcdmVessel
    nv = NcdmVessel(lat0=63.44, lon0=10.38, heading0_deg=0.0, sog_kn=10.0,
                    duration_s=30.0, dt=0.1, seed=0)
    state = nv.get_targets_at(15.0)
    assert state is not None
    assert isinstance(state["lat"], float)


def test_intelligent_vessel_raises_not_implemented():
    """IntelligentVessel raises NotImplementedError with 'D3.6' message."""
    with pytest.raises(NotImplementedError, match="D3.6"):
        IntelligentVessel()
