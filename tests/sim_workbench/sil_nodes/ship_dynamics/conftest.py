# tests/sim_workbench/sil_nodes/ship_dynamics/conftest.py
"""D1.3.1' Shared fixtures — MMG qualification test suite."""
import math
import sys
import os
from dataclasses import dataclass, field
from typing import List

import pytest
import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__),
    "../../../../src/sim_workbench/sil_nodes/ship_dynamics"))
from ship_dynamics.mmg_coefficients import MMGCoefficients
from ship_dynamics.mmg_model import MMGModel, ShipState


@pytest.fixture(scope="session")
def default_model():
    """FCB 45m default MMG model (session-scoped — one instance per test run)."""
    return MMGModel(MMGCoefficients())


@pytest.fixture(scope="session")
def default_coeffs():
    """FCB 45m default MMGCoefficients."""
    return MMGCoefficients()


@pytest.fixture
def cruise_state(default_coeffs):
    """Initial cruise state: 18 kn, heading North (psi=π/2)."""
    return ShipState(
        x=0.0, y=0.0,
        psi=default_coeffs.psi0,  # 1.5708 rad = North
        u=default_coeffs.u0,       # 9.26 m/s = 18 kn
    )


@dataclass
class TrajectoryRecorder:
    """Records ShipState snapshots during simulation."""
    times: List[float] = field(default_factory=list)
    x: List[float] = field(default_factory=list)
    y: List[float] = field(default_factory=list)
    psi: List[float] = field(default_factory=list)
    u: List[float] = field(default_factory=list)
    v: List[float] = field(default_factory=list)
    r: List[float] = field(default_factory=list)
    phi: List[float] = field(default_factory=list)
    p: List[float] = field(default_factory=list)

    def record(self, t: float, state: ShipState):
        self.times.append(t)
        self.x.append(state.x)
        self.y.append(state.y)
        self.psi.append(state.psi)
        self.u.append(state.u)
        self.v.append(state.v)
        self.r.append(state.r)
        self.phi.append(state.phi)
        self.p.append(state.p)

    def to_numpy(self):
        """Convert all recorded lists to numpy arrays."""
        return {k: np.array(v) for k, v in self.__dict__.items()}


def run_simulation(model, initial_state, delta_fn, n_fn,
                   duration_s, dt=None, recorder=None):
    """Run MMG simulation with time-varying control inputs.

    Args:
        model: MMGModel instance
        initial_state: ShipState
        delta_fn: callable(t_s) -> delta_rad (rudder angle)
        n_fn: callable(t_s) -> n_rps (propeller speed)
        duration_s: total simulation time in seconds
        dt: integration step (default: model.c.dt = 0.02s)
        recorder: optional TrajectoryRecorder

    Returns:
        final ShipState
    """
    if dt is None:
        dt = model.c.dt
    state = initial_state
    steps = int(duration_s / dt)
    for i in range(steps):
        t = i * dt
        state = model.rk4_step(
            state,
            delta_cmd=delta_fn(t),
            n_rps_cmd=n_fn(t),
        )
        if recorder:
            recorder.record(t, state)
    return state


def wrap_angle_pi(angle: float) -> float:
    """Wrap angle to [-π, π]."""
    return ((angle + math.pi) % (2 * math.pi)) - math.pi


def heading_difference(psi1: float, psi2: float) -> float:
    """Shortest angular difference in radians, range [-π, π]."""
    return wrap_angle_pi(psi1 - psi2)
