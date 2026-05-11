# test/test_ais_replay_node.py
import numpy as np
from scenario_authoring.replay.ais_replay_node import _interpolate_trajectory_50hz


def test_interpolate_50hz_outputs_correct_rate():
    """2Hz input -> 50Hz output, dt=0.02s."""
    t_orig = np.arange(0.0, 10.0, 0.5)  # 2 Hz
    traj_2hz = np.column_stack([
        t_orig,
        np.linspace(63.0, 63.01, len(t_orig)),
        np.linspace(5.0, 5.01, len(t_orig)),
        np.full(len(t_orig), 10.0),
        np.full(len(t_orig), 90.0),
    ])
    traj_50hz = _interpolate_trajectory_50hz(traj_2hz)

    dt_vals = np.diff(traj_50hz[:, 0])
    assert np.allclose(dt_vals, 0.02, atol=0.001)
    assert traj_50hz.shape[0] > traj_2hz.shape[0] * 20  # ~25x more points


def test_interpolate_50hz_preserves_range():
    """Interpolation preserves lat/lon/sog range."""
    t_orig = np.arange(0.0, 10.0, 0.5)
    traj_2hz = np.column_stack([
        t_orig,
        np.linspace(63.0, 63.5, len(t_orig)),
        np.linspace(5.0, 5.5, len(t_orig)),
        np.linspace(8.0, 12.0, len(t_orig)),
        np.linspace(0.0, 350.0, len(t_orig)),
    ])
    traj_50hz = _interpolate_trajectory_50hz(traj_2hz)

    assert traj_50hz[:, 1].min() >= 63.0
    assert traj_50hz[:, 1].max() <= 63.5
    assert traj_50hz[:, 3].min() >= 8.0
    assert traj_50hz[:, 3].max() <= 12.0
    assert np.all(traj_50hz[:, 4] >= 0)
    assert np.all(traj_50hz[:, 4] < 360)
