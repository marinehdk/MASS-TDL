"""Core simulation engine for D1.3b batch runner.

simulate() runs a single scenario (dual-pass: no-action / with-action).
Uses fcb_sim_py pybind11 binding for MMG RK4 integration.
Target ships propagate as straight-line constant velocity (no MMG).
"""
from __future__ import annotations

import math
import time
from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Any

from scenario_spec import ScenarioSpec


@dataclass
class SimResult:
    stable: bool
    dcpa_m: float = float("inf")
    tcpa_s: float = 0.0
    wall_clock_s: float = 0.0
    own_trajectory_sampled: list[tuple[float, float]] = field(default_factory=list)

    def to_json_dict(
        self,
        spec: ScenarioSpec,
        no_action_dcpa_m: float,
        yaml_path: str,
        geometric_pass: bool = True,
        bearing_pass: bool = True,
    ) -> dict[str, Any]:
        solvability_pass = self.dcpa_m >= spec.pass_criteria.min_dcpa_with_action_m
        stability_pass = self.stable
        wall_clock_pass = self.wall_clock_s <= 60.0
        geometric_pass = geometric_pass and (no_action_dcpa_m < spec.pass_criteria.max_dcpa_no_action_m)
        overall = geometric_pass and bearing_pass and solvability_pass and stability_pass and wall_clock_pass

        return {
            "schema_version": "1.0",
            "scenario_id": spec.scenario_id,
            "scenario_yaml": yaml_path,
            "run_timestamp": datetime.now(tz=timezone.utc).isoformat(),
            "result": "PASS" if overall else "FAIL",
            "sub_checks": {
                "geometric_compliance": geometric_pass,
                "bearing_sector": bearing_pass,
                "solvability": solvability_pass,
                "stability": stability_pass,
                "wall_clock_le_60s": wall_clock_pass,
            },
            "metrics": {
                "dcpa_no_action_m": round(no_action_dcpa_m, 2),
                "dcpa_with_action_m": round(self.dcpa_m, 2),
                "tcpa_no_action_s": round(self.tcpa_s, 2),
            },
            "performance": {
                "wall_clock_s": round(self.wall_clock_s, 4),
                "n_steps": int(spec.simulation.duration_s / spec.simulation.dt_s),
                "sim_duration_s": spec.simulation.duration_s,
            },
            "disturbance_recorded": {
                "wind_kn": spec.disturbance_model.wind_kn,
                "current_kn": spec.disturbance_model.current_kn,
                "vis_m": spec.disturbance_model.vis_m,
            },
            "trajectory_points": len(self.own_trajectory_sampled),
        }


def _compute_min_cpa(
    own_traj: list[tuple[float, float]],
    tgt_trajs: list[list[tuple[float, float]]],
    dt: float,
) -> tuple[float, float]:
    """Return (DCPA_m, TCPA_s) over the full trajectory.

    tgt_trajs is indexed as tgt_trajs[step_i][target_j] = (x, y)
    """
    min_d = float("inf")
    min_t = 0.0
    for i, (ox, oy) in enumerate(own_traj):
        for j in range(len(tgt_trajs[i])):
            tx, ty = tgt_trajs[i][j]
            d = math.hypot(ox - tx, oy - ty)
            if d < min_d:
                min_d = d
                min_t = i * dt
    return min_d, min_t


def simulate(spec: ScenarioSpec, apply_avoidance: bool) -> SimResult:
    """Run a single scenario simulation and return SimResult.

    Requires fcb_sim_py to be importable (source install/setup.bash).
    Falls back to mock implementation on macOS for testing.
    """
    try:
        import fcb_sim_py
    except ImportError:
        try:
            import fcb_sim_py_mock as fcb_sim_py
        except ImportError as exc:
            raise RuntimeError(
                "fcb_sim_py not importable. Run: source install/setup.bash"
            ) from exc

    own_ic = spec.initial_conditions.own_ship
    dt = spec.simulation.dt_s
    n_steps = int(spec.simulation.duration_s / dt)
    sample_every = 100  # store trajectory every 2 s

    # Initialize own ship state
    state = fcb_sim_py.FcbState()
    state.x = own_ic.x_m
    state.y = own_ic.y_m
    state.psi = own_ic.psi_math_rad   # converted from nautical heading
    state.u = own_ic.speed_mps        # converted from knots
    params = fcb_sim_py.MmgParams()

    # Initialize targets (straight-line constant velocity)
    # Convert COG nautical (CW from North) to ENU velocity components
    targets: list[tuple[float, float, float, float]] = []  # (x, y, vx, vy)
    for tgt in spec.initial_conditions.targets:
        cog_rad = math.radians(tgt.cog_nav_deg)
        vx = tgt.sog_mps * math.sin(cog_rad)   # East component
        vy = tgt.sog_mps * math.cos(cog_rad)   # North component
        targets.append((tgt.x_m, tgt.y_m, vx, vy))

    n_rps = own_ic.n_rps
    u_target = own_ic.speed_mps
    delta_rad = 0.0

    own_traj: list[tuple[float, float]] = []
    tgt_traj: list[list[tuple[float, float]]] = []
    own_sampled: list[tuple[float, float]] = []

    t_wall_start = time.perf_counter()

    for i in range(n_steps):
        t_sim = i * dt

        # Avoidance control
        if apply_avoidance:
            t_avoid = spec.encounter.avoidance_time_s
            t_end = t_avoid + spec.encounter.avoidance_duration_s
            if abs(t_sim - t_avoid) < dt / 2.0:
                delta_rad = spec.encounter.avoidance_delta_rad
            elif t_sim > t_end:
                delta_rad = 0.0

        # P-controller to maintain initial speed
        n_rps += 0.1 * (u_target - state.u)
        n_rps = max(0.0, min(10.0, n_rps))

        # Own ship: MMG RK4 step
        state = fcb_sim_py.rk4_step(state, delta_rad, n_rps, params, dt)

        # Stability check
        if not math.isfinite(state.u) or not math.isfinite(state.psi):
            return SimResult(stable=False)

        # Target ships: straight-line extrapolation
        targets = [(x + vx * dt, y + vy * dt, vx, vy) for x, y, vx, vy in targets]

        own_traj.append((state.x, state.y))
        tgt_traj.append([(x, y) for x, y, _, _ in targets])

        if i % sample_every == 0:
            own_sampled.append((state.x, state.y))

    wall_clock = time.perf_counter() - t_wall_start
    dcpa_m, tcpa_s = _compute_min_cpa(own_traj, tgt_traj, dt)

    return SimResult(
        stable=True,
        dcpa_m=dcpa_m,
        tcpa_s=tcpa_s,
        wall_clock_s=wall_clock,
        own_trajectory_sampled=own_sampled,
    )
