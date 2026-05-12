"""D1.3b.3 Mock Live Runner — real-time scenario simulation without ROS2.

Runs a ScenarioSpec in real-time and broadcasts the state via sil_ws.
"""
from __future__ import annotations

import asyncio
import logging
import math
import time
from datetime import datetime, timezone
from pathlib import Path

import yaml
import sys
ROOT_DIR = Path(__file__).resolve().parents[5]
TOOLS_SIL_DIR = ROOT_DIR / "tools" / "sil"
for p in [str(ROOT_DIR), str(TOOLS_SIL_DIR)]:
    if p not in sys.path:
        sys.path.insert(0, p)

from tools.sil.scenario_spec import ScenarioSpec
from web_server import sil_ws
from web_server.sil_schemas import SilNavState, SilTargetState, SilDebugSchema

logger = logging.getLogger("m8_web_backend.sil_live_runner")

# Trondheim Fjord origin (matches MapView.tsx CENTER)
LAT0, LON0 = 63.435, 10.395
R_EARTH = 6371000.0  # meters


def xy_to_latlon(x_m: float, y_m: float) -> tuple[float, float]:
    lat = LAT0 + (y_m / R_EARTH) * (180.0 / math.pi)
    lon = LON0 + (x_m / (R_EARTH * math.cos(LAT0 * math.pi / 180.0))) * (180.0 / math.pi)
    return lat, lon


class LiveRunner:
    def __init__(self, scenario_path: Path):
        self.scenario_path = scenario_path
        self.spec = ScenarioSpec.model_validate(yaml.safe_load(scenario_path.read_text()))
        self._stop_event = asyncio.Event()
        self._task: asyncio.Task | None = None

    async def start(self):
        if self._task and not self._task.done():
            return
        self._stop_event.clear()
        self._task = asyncio.create_task(self._run_loop())

    async def stop(self):
        self._stop_event.set()
        if self._task:
            await self._task

    async def _run_loop(self):
        logger.info("Starting live simulation for %s", self.spec.scenario_id)
        sil_ws.set_job_status(self.spec.scenario_id, "running")

        try:
            import fcb_sim_py
            if not hasattr(fcb_sim_py, "FcbState"):
                 raise ImportError
        except ImportError:
            import fcb_sim_py_mock as fcb_sim_py

        # Initialize own ship
        own_ic = self.spec.initial_conditions.own_ship
        state = fcb_sim_py.FcbState()
        state.x, state.y = own_ic.x_m, own_ic.y_m
        state.psi = own_ic.psi_math_rad
        state.u = own_ic.speed_mps
        params = fcb_sim_py.MmgParams()

        # Initialize targets
        targets: list[dict] = []
        for t in self.spec.initial_conditions.targets:
            cog_rad = math.radians(t.cog_nav_deg)
            targets.append({
                "mmsi": str(t.target_id),
                "x": t.x_m,
                "y": t.y_m,
                "vx": t.sog_mps * math.sin(cog_rad),
                "vy": t.sog_mps * math.cos(cog_rad),
                "cog_deg": t.cog_nav_deg,
                "sog_kn": t.sog_mps / 0.5144
            })

        dt = self.spec.simulation.dt_s
        n_steps = int(self.spec.simulation.duration_s / dt)
        
        # Real-time control: we'll jump in chunks to match wall clock
        wall_start = time.perf_counter()
        
        for i in range(n_steps):
            if self._stop_event.is_set():
                break

            t_sim = i * dt
            
            # Apply simple avoidance logic from simulate.py
            delta_rad = 0.0
            t_avoid = self.spec.encounter.avoidance_time_s
            t_end = t_avoid + self.spec.encounter.avoidance_duration_s
            if t_avoid <= t_sim <= t_end:
                delta_rad = self.spec.encounter.avoidance_delta_rad

            # Own ship step
            n_rps = own_ic.n_rps + 0.1 * (own_ic.speed_mps - state.u)
            state = fcb_sim_py.rk4_step(state, delta_rad, n_rps, params, dt)

            # Target ships step
            for t in targets:
                t["x"] += t["vx"] * dt
                t["y"] += t["vy"] * dt

            # Broadcast at 5Hz (every 10 steps if dt=0.02)
            if i % 10 == 0:
                lat, lon = xy_to_latlon(state.x, state.y)
                own_ship = SilNavState(
                    lat=lat, lon=lon,
                    sog_kn=state.u / 0.5144,
                    cog_deg=(90.0 - math.degrees(state.psi)) % 360.0,
                    heading_deg=(90.0 - math.degrees(state.psi)) % 360.0
                )

                tgt_states = []
                for t in targets:
                    t_lat, t_lon = xy_to_latlon(t["x"], t["y"])
                    # Simple CPA calc for display
                    dx, dy = t["x"] - state.x, t["y"] - state.y
                    dist_nm = math.hypot(dx, dy) / 1852.0
                    tgt_states.append(SilTargetState(
                        mmsi=t["mmsi"], lat=t_lat, lon=t_lon,
                        sog_kn=t["sog_kn"], cog_deg=t["cog_deg"],
                        heading_deg=t["cog_deg"],
                        cpa_nm=dist_nm, tcpa_s=0.0, # stub
                        colreg_role="safe", confidence=1.0
                    ))

                # Build and broadcast schema
                schema = SilDebugSchema(
                    timestamp=datetime.now(tz=timezone.utc),
                    scenario_id=self.spec.scenario_id,
                    job_status="running",
                    own_ship=own_ship,
                    targets=tgt_states,
                    rule_text=f"COLREGs {self.spec.encounter.rule}",
                    decision_text="Executing avoidance..." if delta_rad != 0 else "Maintaining course",
                )
                await sil_ws.broadcast_schema(schema)

                # Sync with wall clock
                elapsed_wall = time.perf_counter() - wall_start
                if t_sim > elapsed_wall:
                    await asyncio.sleep(t_sim - elapsed_wall)

        sil_ws.set_job_status(self.spec.scenario_id, "done")
        logger.info("Live simulation for %s completed", self.spec.scenario_id)

    def is_running(self):
        return self._task and not self._task.done()
