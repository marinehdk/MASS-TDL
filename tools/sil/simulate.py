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

from tools.sil.scenario_spec import ScenarioSpec


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
    import os
    if os.getenv("USE_REAL_STACK") == "1":
        import sys
        for p in [
            "/opt/ws/src/sim_workbench/shell_b_harness",
            "/opt/ws/install/shell_b_harness/local/lib/python3.10/dist-packages",
            "/opt/ws/install/shell_b_harness/lib/python3.10/site-packages",
            "./src/sim_workbench/shell_b_harness",
        ]:
            abs_p = os.path.abspath(p)
            if abs_p not in sys.path:
                sys.path.insert(0, abs_p)

        # rl-isolation-ok: importing shell_b_harness simulator to run real stack simulation in SIL tools
        from shell_b_harness.simulator import ShellBSimulator

        port = int(os.getenv("REAL_STACK_PORT", "9096"))
        use_m7 = os.getenv("REAL_STACK_USE_M7", "1") == "1"
        sim = ShellBSimulator(
            port=port,
            use_m7=use_m7,
            verbose=False,
            ros_domain_id=42,
            headless=True,
        )
        sim.apply_avoidance = apply_avoidance
        
        t_wall_start = time.perf_counter()
        try:
            sim.reset(seed=spec.prng_seed, in_place=False)
            
            own_ic = spec.initial_conditions.own_ship
            
            # Override initial state variables
            sim.own_state.x = own_ic.x_m
            sim.own_state.y = own_ic.y_m
            sim.own_state.psi = own_ic.psi_math_rad
            sim.own_state.u = own_ic.speed_mps
            sim.target_heading_deg = own_ic.heading_nav_deg
            sim.target_sog_kn = own_ic.speed_kn
            
            if spec.initial_conditions.targets:
                tgt = spec.initial_conditions.targets[0]
                sim.ts_mmsi = tgt.target_id
                
                # Compute ts_lat and ts_lon using inverse projection
                y_offset_deg = tgt.y_m / 111120.0
                sim.ts_lat = 63.44 + y_offset_deg
                cos_lat = math.cos(math.radians(sim.ts_lat))
                x_offset_deg = tgt.x_m / (111120.0 * cos_lat)
                sim.ts_lon = 10.38 + x_offset_deg
                
                sim.ts_heading = math.radians(tgt.cog_nav_deg)
                sim.ts_sog = tgt.sog_mps

            dt = spec.simulation.dt_s
            n_steps = int(spec.simulation.duration_s / dt)
            sample_every = 100
            
            own_traj: list[tuple[float, float]] = []
            tgt_traj: list[list[tuple[float, float]]] = []
            own_sampled: list[tuple[float, float]] = []
            
            stable = True
            for i in range(n_steps):
                state = sim.step()
                if i == 0:
                    print(f"DEBUG STEP 0 STATE: {state}", flush=True)
                
                # Check stability
                u_val = state["own_ship"]["u"]
                psi_val = state["own_ship"]["psi"]
                if not math.isfinite(u_val) or not math.isfinite(psi_val):
                    stable = False
                    break
                
                # Record own ship and target vessel trajectories
                ox = state["own_ship"]["x"]
                oy = state["own_ship"]["y"]
                own_traj.append((ox, oy))
                
                tgt_step_trajs = []
                for tv in state["target_vessels"]:
                    target_lat = tv["lat"]
                    target_lon = tv["lon"]
                    y_t = (target_lat - 63.44) * 111120.0
                    x_t = (target_lon - 10.38) * 111120.0 * math.cos(math.radians(63.44))
                    tgt_step_trajs.append((x_t, y_t))
                tgt_traj.append(tgt_step_trajs)
                
                if i % sample_every == 0:
                    own_sampled.append((ox, oy))
                    
            wall_clock = time.perf_counter() - t_wall_start
            
            if not stable:
                return SimResult(stable=False)
                
            print(f"DEBUG TRAJECTORY: own_start={own_traj[0]}, own_end={own_traj[-1]}, tgt_start={tgt_traj[0]}, tgt_end={tgt_traj[-1]}", flush=True)
            dcpa_m, tcpa_s = _compute_min_cpa(own_traj, tgt_traj, dt)
            
            return SimResult(
                stable=True,
                dcpa_m=dcpa_m,
                tcpa_s=tcpa_s,
                wall_clock_s=wall_clock,
                own_trajectory_sampled=own_sampled,
            )
            
        finally:
            sim.close()

    # Try multiple import strategies for fcb_sim_py
    _fcb = None
    for _mod in ["fcb_sim_py", "src.sim_workbench.fcb_simulator.python.fcb_sim_py_mock", "fcb_sim_py_mock"]:
        try:
            if _mod == "src.sim_workbench.fcb_simulator.python.fcb_sim_py_mock":
                from src.sim_workbench.fcb_simulator.python import fcb_sim_py_mock as _m
                _fcb = _m
            else:
                _m = __import__(_mod, fromlist=["FcbState"])
            if hasattr(_m, "FcbState"):
                _fcb = _m
                break
        except ImportError:
            pass
    if _fcb is None:
        raise RuntimeError(
            "fcb_sim_py not importable and fcb_sim_py_mock not found."
        )
    fcb_sim_py = _fcb

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
