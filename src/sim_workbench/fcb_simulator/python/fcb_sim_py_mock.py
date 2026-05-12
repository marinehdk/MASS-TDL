"""Mock fcb_sim_py for macOS / no-build environments.

Provides the same API as the pybind11 fcb_sim_py binding:
  - FcbState (dataclass-like)
  - MmgParams (dataclass-like)
  - rk4_step(state, delta_rad, n_rps, params, dt) -> FcbState

Uses a simple point-mass model instead of full MMG hydrodynamics.
"""
from __future__ import annotations

import math
from dataclasses import dataclass


@dataclass
class FcbState:
    x: float = 0.0
    y: float = 0.0
    psi: float = 0.0  # math convention: CCW from East, rad
    u: float = 0.0   # longitudinal speed m/s
    v: float = 0.0    # lateral speed m/s
    r: float = 0.0   # yaw rate rad/s
    phi: float = 0.0
    phi_dot: float = 0.0


@dataclass
class MmgParams:
    L: float = 46.0
    B: float = 9.5
    d: float = 3.2
    mass: float = 342.0
    Izz: float = 50.0


def _dx_dt(s: FcbState) -> float:
    return s.u * math.cos(s.psi) - s.v * math.sin(s.psi)


def _dy_dt(s: FcbState) -> float:
    return s.u * math.sin(s.psi) + s.v * math.cos(s.psi)


def _dpsi_dt(s: FcbState) -> float:
    return s.r


def _du_dt(u: float, thrust: float) -> float:
    return 0.1 * (thrust - u * abs(u) * 0.01)


def _dv_dt(v: float, rudder_effect: float) -> float:
    return -0.05 * v + 0.3 * rudder_effect


def _dr_dt(delta_rad: float, r: float) -> float:
    return 0.5 * delta_rad - 0.1 * r


def rk4_step(state: FcbState, delta_rad: float, n_rps: float, params: MmgParams, dt: float) -> FcbState:
    thrust = 0.5 * n_rps * n_rps
    rudder_effect = 2.0 * delta_rad

    k1_x = _dx_dt(state)
    k1_y = _dy_dt(state)
    k1_psi = _dpsi_dt(state)
    k1_u = _du_dt(state.u, thrust)
    k1_v = _dv_dt(state.v, rudder_effect)
    k1_r = _dr_dt(delta_rad, state.r)

    s2_x = state.x + 0.5 * dt * k1_x
    s2_y = state.y + 0.5 * dt * k1_y
    s2_psi = state.psi + 0.5 * dt * k1_psi
    s2_u = state.u + 0.5 * dt * k1_u
    s2_v = state.v + 0.5 * dt * k1_v
    s2_r = state.r + 0.5 * dt * k1_r

    s2 = FcbState(x=s2_x, y=s2_y, psi=s2_psi, u=s2_u, v=s2_v, r=s2_r, phi=state.phi, phi_dot=state.phi_dot)

    k2_x = _dx_dt(s2)
    k2_y = _dy_dt(s2)
    k2_psi = _dpsi_dt(s2)
    k2_u = _du_dt(s2.u, thrust)
    k2_v = _dv_dt(s2.v, rudder_effect)
    k2_r = _dr_dt(delta_rad, s2.r)

    s3_x = state.x + 0.5 * dt * k2_x
    s3_y = state.y + 0.5 * dt * k2_y
    s3_psi = state.psi + 0.5 * dt * k2_psi
    s3_u = state.u + 0.5 * dt * k2_u
    s3_v = state.v + 0.5 * dt * k2_v
    s3_r = state.r + 0.5 * dt * k2_r

    s3 = FcbState(x=s3_x, y=s3_y, psi=s3_psi, u=s3_u, v=s3_v, r=s3_r, phi=state.phi, phi_dot=state.phi_dot)

    k3_x = _dx_dt(s3)
    k3_y = _dy_dt(s3)
    k3_psi = _dpsi_dt(s3)
    k3_u = _du_dt(s3.u, thrust)
    k3_v = _dv_dt(s3.v, rudder_effect)
    k3_r = _dr_dt(delta_rad, s3.r)

    s4_x = state.x + dt * k3_x
    s4_y = state.y + dt * k3_y
    s4_psi = state.psi + dt * k3_psi
    s4_u = state.u + dt * k3_u
    s4_v = state.v + dt * k3_v
    s4_r = state.r + dt * k3_r

    s4 = FcbState(x=s4_x, y=s4_y, psi=s4_psi, u=s4_u, v=s4_v, r=s4_r, phi=state.phi, phi_dot=state.phi_dot)

    k4_x = _dx_dt(s4)
    k4_y = _dy_dt(s4)
    k4_psi = _dpsi_dt(s4)
    k4_u = _du_dt(s4.u, thrust)
    k4_v = _dv_dt(s4.v, rudder_effect)
    k4_r = _dr_dt(delta_rad, s4.r)

    return FcbState(
        x=state.x + (dt / 6.0) * (k1_x + 2*k2_x + 2*k3_x + k4_x),
        y=state.y + (dt / 6.0) * (k1_y + 2*k2_y + 2*k3_y + k4_y),
        psi=state.psi + (dt / 6.0) * (k1_psi + 2*k2_psi + 2*k3_psi + k4_psi),
        u=state.u + (dt / 6.0) * (k1_u + 2*k2_u + 2*k3_u + k4_u),
        v=state.v + (dt / 6.0) * (k1_v + 2*k2_v + 2*k3_v + k4_v),
        r=state.r + (dt / 6.0) * (k1_r + 2*k2_r + 2*k3_r + k4_r),
        phi=state.phi,
        phi_dot=state.phi_dot,
    )