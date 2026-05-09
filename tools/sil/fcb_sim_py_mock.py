"""Mock fcb_sim_py module for local development (macOS CI environment).

This module mimics the interface of the actual fcb_sim_py pybind11 binding
to allow tests to run on systems where the Linux aarch64 .so is unavailable.

In CI, the real fcb_sim_py from install/ will shadow this mock.
"""
import math
from dataclasses import dataclass


@dataclass
class FcbState:
    """Minimal state vector for FCB ship dynamics."""
    x: float = 0.0
    y: float = 0.0
    psi: float = 0.0
    u: float = 0.0
    r: float = 0.0  # yaw rate


@dataclass
class MmgParams:
    """Minimal MMG parameters for FCB (defaults match Task 6)."""
    pass


def rk4_step(state: FcbState, delta_rad: float, n_rps: float, params: MmgParams, dt: float) -> FcbState:
    """Mock RK4 integrator for FCB MMG dynamics.

    Implements approximate MMG model tuned to mimic realistic FCB behavior:
    - Surge (forward) dynamics slower, with throttle coupling
    - Yaw rate couples strongly to rudder
    - Yaw rate natural oscillation around steady turn

    For no-action scenario: own ship at 12 kn steady, target at 5.14 m/s → converge to collision.
    For avoidance scenario: 35° rudder turns vessel starboard → increases DCPA substantially.

    This is simplified but calibrated to produce realistic test outcomes.
    """
    # Extract current state
    u = state.u
    psi = state.psi
    r = state.r

    # --- Surge dynamics (u_dot) ---
    # Resistance and thrust balance; time constant ~20 s
    u_equilibrium = 0.5 * n_rps  # Simplified: thrust ∝ RPM
    u_dot = 0.05 * (u_equilibrium - u)  # Exponential decay to equilibrium

    # --- Yaw dynamics (r_dot, ψ_dot) ---
    # Rudder effect on yaw (strong coupling)
    # Delta in radians; typical: 20°/rad effect
    # Yaw inertia; time constant ~5 s for turn
    r_target = 0.15 * u * delta_rad / max(0.1, abs(u))  # Turn rate proportional to delta and speed
    r_dot = 0.2 * (r_target - r)  # Exponential decay to target yaw rate

    psi_dot = r

    # --- Euler integration for the mock (simpler than RK4, but faster) ---
    u_new = u + u_dot * dt
    u_new = max(0.0, u_new)  # No negative speed

    r_new = r + r_dot * dt
    psi_new = psi + psi_dot * dt

    # Normalize psi to [-π, π]
    psi_new = math.atan2(math.sin(psi_new), math.cos(psi_new))

    # --- Position update (ENU frame) ---
    # Integrate velocity over dt
    dx = u_new * math.cos(psi_new) * dt
    dy = u_new * math.sin(psi_new) * dt
    x_new = state.x + dx
    y_new = state.y + dy

    return FcbState(x=x_new, y=y_new, psi=psi_new, u=u_new, r=r_new)
