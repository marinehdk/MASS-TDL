# test/test_fmu_turning_circle.py
"""Verify turning circle against D1.3a reference: advance ~ 3L +/- 5%, transfer ~ 0.7L +/- 5%."""
from __future__ import annotations
import math
import numpy as np


def _compute_turning_metrics(traj: list[tuple[float, float, float]]):
    """Compute advance and transfer from XY-psi trajectory."""
    x0, y0, psi0 = traj[0]
    cruise_dir = np.array([math.cos(psi0), math.sin(psi0)])

    for x, y, psi in traj:
        delta_psi = abs(psi - psi0) % (2 * math.pi)
        if delta_psi >= math.pi / 2:
            advance = (x - x0) * cruise_dir[0] + (y - y0) * cruise_dir[1]
            transfer = abs((x - x0) * cruise_dir[1] - (y - y0) * cruise_dir[0])
            return advance, transfer

    return float('nan'), float('nan')


def test_turning_circle_advance():
    """35 deg rudder: advance ~ 135m +/- 6.75m (3L +/- 5%, L=45m)."""
    try:
        from fcb_mmg_py import FCBSimulator, ShipState
    except ImportError:
        import pytest
        pytest.skip("fcb_mmg_py not built (requires colcon build)")

    sim = FCBSimulator()
    try:
        sim.load_params("config/fcb_dynamics.yaml")
    except Exception:
        import pytest
        pytest.skip("fcb_dynamics.yaml not found")

    state = ShipState()

    # Approach: 600s straight at ~12 kn
    for _ in range(3000):
        state = sim.step(state, 0.0, 3.5, 0.2)

    # Turning circle: 35 deg rudder, 300s
    delta_rad = math.radians(35.0)
    traj = [(state.x, state.y, state.psi)]
    for _ in range(1500):
        state = sim.step(state, delta_rad, 3.5, 0.2)
        traj.append((state.x, state.y, state.psi))

    advance, transfer = _compute_turning_metrics(traj)

    L = 45.0
    assert not math.isnan(advance), "Advance is NaN -- turning circle may not have completed 90 deg"
    assert abs(advance - 3 * L) < 0.15 * L, \
        f"advance={advance:.1f}m, expected ~{3*L}m +/-{0.15*L}m"
    assert abs(transfer - 0.7 * L) < 0.05 * L, \
        f"transfer={transfer:.1f}m, expected ~{0.7*L}m +/-{0.05*L}m"
