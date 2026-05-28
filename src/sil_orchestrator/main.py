"""SIL Orchestrator — FastAPI REST API bridging frontend to ROS2 lifecycle.

Spec: docs/Design/SIL/2026-05-12-sil-architecture-design.md §1 orchestration plane
"""

import json
import math
import os
import time
import warnings
import threading
from enum import Enum
from pathlib import Path

import yaml

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles

from sil_orchestrator.config import RUN_DIR, EXPORT_DIR
from sil_orchestrator.scenario_store import ScenarioStore
from sil_orchestrator.selfcheck_routes import router as selfcheck_router
from sil_orchestrator.export_routes import router as export_router
from sil_orchestrator.scenario_routes import router as scenario_router
from sil_orchestrator.schema_routes import router as schema_router
from sil_orchestrator.scoring_routes import router as scoring_router, \
    _kpi_router as kpi_router
from sil_orchestrator.ops_routes import router as ops_router
from sil_orchestrator.arrow_routes import router as arrow_router
from sil_orchestrator.gif_pack_routes import router as gif_pack_router
from sil_orchestrator.asdr_routes import router as asdr_router
from sil_orchestrator.demo_avoidance import AvoidanceState, TargetState, step_demo_avoidance, _dcpa_nm, _tcpa_s, _haversine_nm
from sil_orchestrator.demo_scorer import score_demo_run

try:
    import rclpy
    from rclpy.callback_groups import ReentrantCallbackGroup
    from rclpy.executors import MultiThreadedExecutor
    from sil_orchestrator.lifecycle_bridge import LifecycleBridge, LifecycleState, ScenarioInjectionError, _copy_preflight_evidence
    _HAS_RCLPY = True
except ImportError:
    _HAS_RCLPY = False
    LifecycleState = None
    ScenarioInjectionError = Exception
    _copy_preflight_evidence = None

if _HAS_RCLPY:
    rclpy.init(args=None)

app = FastAPI(title="SIL Orchestrator", version="0.1.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

if _HAS_RCLPY:
    _cb_group = ReentrantCallbackGroup()
    bridge = LifecycleBridge(callback_group=_cb_group)

    def _spin_bridge():
        executor = MultiThreadedExecutor(num_threads=4)
        executor.add_node(bridge)
        executor.spin()

    threading.Thread(target=_spin_bridge, daemon=True).start()
else:
    import threading

    class _DemoState(Enum):
        UNCONFIGURED = "UNCONFIGURED"
        INACTIVE = "INACTIVE"
        ACTIVE = "ACTIVE"

    class _DemoBridge:
        def __init__(self):
            self._state = _DemoState.UNCONFIGURED
            self._scenario_id: str | None = None
            self._sim_rate: float = 1.0
            self._simulation_duration_s = None
            self._timer_start_time = None
            self._timer_task = None
            self._backup_timer_task = None

        @property
        def current_state(self):
            return self._state

        @property
        def scenario_id(self):
            return self._scenario_id

        async def configure(self, scenario_id: str):
            self._scenario_id = scenario_id
            self._state = _DemoState.INACTIVE
            
            try:
                from sil_orchestrator.lifecycle_bridge import _load_scenario_yaml
                yaml_data = _load_scenario_yaml(scenario_id)
                duration = None
                metadata = yaml_data.get("metadata", {}) if isinstance(yaml_data, dict) else {}
                if isinstance(metadata, dict):
                    sim_settings = metadata.get("simulation_settings", {})
                    if isinstance(sim_settings, dict):
                        duration = sim_settings.get("total_time")
                
                if duration is None:
                    sim_section = yaml_data.get("simulation", {}) if isinstance(yaml_data, dict) else {}
                    if isinstance(sim_section, dict):
                        duration = sim_section.get("total_time") or sim_section.get("duration_s")

                self._simulation_duration_s = duration
            except Exception:
                self._simulation_duration_s = None

            self._timer_start_time = None
            if self._timer_task:
                self._timer_task.cancel()
                self._timer_task = None
            if self._backup_timer_task:
                self._backup_timer_task.cancel()
                self._backup_timer_task = None

            return type("R", (), {"success": True, "error": ""})()

        async def activate(self):
            self._state = _DemoState.ACTIVE
            if self._timer_task:
                self._timer_task.cancel()
                self._timer_task = None
            if self._backup_timer_task:
                self._backup_timer_task.cancel()
                self._backup_timer_task = None

            if self._simulation_duration_s is not None:
                import asyncio
                import time
                self._timer_start_time = time.time()
                self._timer_task = asyncio.create_task(self._auto_stop_timer())
                self._backup_timer_task = asyncio.create_task(self._auto_stop_backup_timer())
            return type("R", (), {"success": True, "error": ""})()

        async def deactivate(self):
            if self._timer_task:
                self._timer_task.cancel()
                self._timer_task = None
            if self._backup_timer_task:
                self._backup_timer_task.cancel()
                self._backup_timer_task = None
            self._state = _DemoState.INACTIVE
            return type("R", (), {"success": True, "error": ""})()

        async def _auto_stop_timer(self):
            try:
                duration = float(self._simulation_duration_s)
                import asyncio
                await asyncio.sleep(duration)
                await lifecycle_deactivate()
            except asyncio.CancelledError:
                pass
            except Exception:
                pass

        async def _auto_stop_backup_timer(self):
            try:
                duration = float(self._simulation_duration_s)
                import asyncio
                await asyncio.sleep(duration + 30.0)
                self._state = _DemoState.INACTIVE
                if self._timer_task:
                    self._timer_task.cancel()
                    self._timer_task = None
            except asyncio.CancelledError:
                pass
            except Exception:
                pass

        async def _reset_to_unconfigured(self):
            if self._timer_task:
                self._timer_task.cancel()
                self._timer_task = None
            if self._backup_timer_task:
                self._backup_timer_task.cancel()
                self._backup_timer_task = None
            self._state = _DemoState.UNCONFIGURED
            self._scenario_id = None
            return type("R", (), {"success": True, "error": ""})()

        async def set_sim_rate(self, rate: float):
            self._sim_rate = rate
            return type("R", (), {"success": True, "error": ""})()

    bridge = _DemoBridge()
    LifecycleState = _DemoState

_store = ScenarioStore()
_last_run_id: str | None = None
_avoidance_state: AvoidanceState | None = None
_demo_start_wall: float | None = None
_demo_sim_time: float = 0.0
_demo_last_wall: float | None = None
_demo_min_cpa_nm: float = float("inf")
_demo_tcpa_at_min: float = 0.0
_demo_max_rudder_deg: float = 0.0
_demo_max_cross_track_nm: float = 0.0
_demo_rot_samples: list[float] = []
_demo_initial_lat: float | None = None
_demo_initial_lon: float | None = None


def _seed_run_dir(scenario_id: str) -> str:
    """DEPRECATED: use _create_run_dir() instead."""
    warnings.warn(
        "_seed_run_dir is deprecated; use _create_run_dir",
        DeprecationWarning,
        stacklevel=2,
    )
    return _create_run_dir(scenario_id)


def _create_run_dir(scenario_id: str) -> str:
    global _last_run_id
    run_id = f"run-{int(time.time() * 1000):x}"
    _last_run_id = run_id
    run_path = RUN_DIR / run_id
    run_path.mkdir(parents=True, exist_ok=True)
    detail = _store.get(scenario_id)
    if detail is not None:
        (run_path / "scenario.yaml").write_text(detail["yaml_content"])
        (run_path / "scenario.sha256").write_text(detail["hash"])
    return run_id


def _write_scoring_json(scenario_id: str) -> None:
    if _last_run_id is None:
        return
    run_path = RUN_DIR / _last_run_id
    if not run_path.exists():
        return
    result = score_demo_run(
        min_cpa_nm=_demo_min_cpa_nm,
        tcpa_at_min_s=_demo_tcpa_at_min,
        max_rudder_deg=_demo_max_rudder_deg,
        max_cross_track_nm=_demo_max_cross_track_nm,
        rot_samples=list(_demo_rot_samples),
        distance_nm=_haversine_nm(
            _demo_initial_lat or 0.0, _demo_initial_lon or 0.0,
            _avoidance_state.own_lat if _avoidance_state else 0.0,
            _avoidance_state.own_lon if _avoidance_state else 0.0,
        ) if _demo_initial_lat is not None else 0.0,
        duration_s=_demo_sim_time,
        avoidance_initiated=_demo_max_rudder_deg > 0.1,

    )
    scoring_data = {
        "run_id": _last_run_id,
        "scenario_id": scenario_id,
        "started_at": time.time(),
        "kpis": {
            "min_cpa_nm": result.min_cpa_nm,
            "avg_rot_dpm": result.avg_rot_dpm,
            "distance_nm": result.distance_nm,
            "duration_s": result.duration_s,
        },
        "scoring_dimensions": {
            "safety": result.safety,
            "rule_compliance": result.rule_compliance,
            "delay_penalty": result.delay_penalty,
            "action_magnitude_penalty": result.action_magnitude_penalty,
            "phase_score": result.phase_score,
            "plausibility": result.plausibility,
            "total": result.total,
        },
        "rule_chain": result.rule_chain,
        "verdict": result.verdict,
    }
    (run_path / "scoring.json").write_text(json.dumps(scoring_data, indent=2))


def _seed_run_dir_ros2(scenario_id: str) -> str:
    """ROS2 path: create run dir with scenario YAML only; scoring.arrow written by scoring_node."""
    global _last_run_id
    run_id = f"run-{int(time.time() * 1000):x}"
    _last_run_id = run_id
    run_path = RUN_DIR / run_id
    run_path.mkdir(parents=True, exist_ok=True)
    detail = _store.get(scenario_id)
    if detail is not None:
        (run_path / "scenario.yaml").write_text(detail["yaml_content"])
        (run_path / "scenario.sha256").write_text(detail["hash"])
    os.environ["SIL_RUN_DIR"] = str(RUN_DIR)
    os.environ["SIL_RUN_ID"] = run_id
    return run_id


@app.get("/api/v1/health")
async def health():
    return {"status": "ok"}


@app.get("/api/v1/lifecycle/status")
async def lifecycle_status():
    detail = _store.get(bridge.scenario_id) if bridge.scenario_id else None
    backend = detail.get("backend", "demo") if detail else "demo"
    effective_backend = "demo" if not _HAS_RCLPY else backend

    time_remaining_s = -1.0
    if (hasattr(bridge, "_timer_start_time") and 
        bridge._timer_start_time is not None and 
        hasattr(bridge, "_simulation_duration_s") and 
        bridge._simulation_duration_s is not None):
        state_str = bridge.current_state.value if hasattr(bridge.current_state, "value") else str(bridge.current_state)
        if state_str.upper() == "ACTIVE":
            elapsed = time.time() - bridge._timer_start_time
            time_remaining_s = max(0.0, float(bridge._simulation_duration_s) - elapsed)

    return {
        "current_state": bridge.current_state.value if hasattr(bridge.current_state, "value") else bridge.current_state,
        "scenario_id": bridge.scenario_id,
        "run_id": _last_run_id,
        "effective_backend": effective_backend,
        "time_remaining_s": time_remaining_s,
    }


@app.post("/api/v1/lifecycle/configure")
async def lifecycle_configure(request: dict):
    scenario_id = request.get("scenario_id", "")
    detail = _store.get(scenario_id)
    backend = detail.get("backend", "demo") if detail else "demo"
    effective_backend = "demo" if not _HAS_RCLPY else backend
    if effective_backend == "ros2":
        try:
            result = await bridge.configure(scenario_id)
        except ScenarioInjectionError as exc:
            return {"success": False, "error": str(exc)}
        return {"success": result.success, "error": result.error}
    # Demo/internal mode: bypass ROS2 lifecycle service
    bridge._scenario_id = scenario_id
    bridge._state = LifecycleState.INACTIVE
    return {"success": True, "error": ""}


@app.post("/api/v1/lifecycle/activate")
async def lifecycle_activate():
    detail = _store.get(bridge.scenario_id) if bridge.scenario_id else None
    backend = detail.get("backend", "demo") if detail else "demo"
    effective_backend = "demo" if not _HAS_RCLPY else backend
    if effective_backend != "ros2" and bridge.scenario_id:
        bridge._state = LifecycleState.ACTIVE
        run_id = _create_run_dir(bridge.scenario_id)
        if _copy_preflight_evidence:
            _copy_preflight_evidence(bridge.scenario_id, run_id)
        return {"success": True, "error": "", "run_id": run_id}
    result = await bridge.activate()
    run_id = None
    if result.success and bridge.scenario_id:
        if effective_backend == "ros2":
            run_id = _seed_run_dir_ros2(bridge.scenario_id)
        else:
            run_id = _create_run_dir(bridge.scenario_id)
        if _copy_preflight_evidence:
            _copy_preflight_evidence(bridge.scenario_id, run_id)
    return {"success": result.success, "error": result.error, "run_id": run_id}


@app.post("/api/v1/lifecycle/deactivate")
async def lifecycle_deactivate():
    detail = _store.get(bridge.scenario_id) if bridge.scenario_id else None
    backend = detail.get("backend", "demo") if detail else "demo"
    effective_backend = "demo" if not _HAS_RCLPY else backend
    if effective_backend != "ros2" and bridge.scenario_id:
        try:
            _write_scoring_json(bridge.scenario_id)
        except Exception as exc:
            import logging
            logging.getLogger("sil_orchestrator").error(f"_write_scoring_json failed: {exc}", exc_info=True)
    result = await bridge.deactivate()
    return {"success": result.success, "error": result.error, "run_id": _last_run_id}


@app.post("/api/v1/lifecycle/cleanup")
async def lifecycle_cleanup():
    """Reset to UNCONFIGURED so a new scenario can be configured.

    Idempotent + tolerant of stale ACTIVE state: uses the real ROS2 node state
    (not the Python mirror which can be stale after a restart) to decide which
    transitions to issue before cleanup.
    """
    global _avoidance_state, _demo_start_wall, _demo_sim_time, _demo_last_wall
    global _demo_min_cpa_nm, _demo_tcpa_at_min, _demo_max_rudder_deg, _demo_max_cross_track_nm, _demo_rot_samples, _demo_initial_lat, _demo_initial_lon
    detail = _store.get(bridge.scenario_id) if bridge.scenario_id else None
    backend = detail.get("backend", "demo") if detail else "demo"
    if backend != "ros2":
        bridge._state = LifecycleState.UNCONFIGURED
        bridge._scenario_id = None
        _avoidance_state = None
        _demo_start_wall = None
        _demo_sim_time = 0.0
        _demo_last_wall = None
        _demo_min_cpa_nm = float("inf")
        _demo_tcpa_at_min = 0.0
        _demo_max_rudder_deg = 0.0
        _demo_max_cross_track_nm = 0.0
        _demo_rot_samples = []
        _demo_initial_lat = None
        _demo_initial_lon = None
        return {"success": True, "error": ""}
    result = await bridge._reset_to_unconfigured()
    _avoidance_state = None
    _demo_start_wall = None
    _demo_sim_time = 0.0
    _demo_last_wall = None
    _demo_min_cpa_nm = float("inf")
    _demo_tcpa_at_min = 0.0
    _demo_max_rudder_deg = 0.0
    _demo_max_cross_track_nm = 0.0
    _demo_rot_samples = []
    _demo_initial_lat = None
    _demo_initial_lon = None
    return {"success": result.success, "error": result.error}


@app.post("/api/v1/lifecycle/rate")
async def lifecycle_rate(request: dict):
    rate = request.get("rate", 1.0)
    detail = _store.get(bridge.scenario_id) if bridge.scenario_id else None
    backend = detail.get("backend", "demo") if detail else "demo"
    if backend == "ros2":
        result = await bridge.set_sim_rate(rate)
        return {"success": result.success, "error": result.error}
    else:
        bridge._sim_rate = rate
        return {"success": True, "error": ""}


# Self-check, export, scenario CRUD, and scoring routes
app.include_router(selfcheck_router)
app.include_router(export_router)
app.include_router(scenario_router)
app.include_router(schema_router)
app.include_router(scoring_router)
app.include_router(ops_router)
app.include_router(arrow_router)
app.include_router(kpi_router)
app.include_router(gif_pack_router)
app.include_router(asdr_router)

# ── Demo telemetry (non-ROS2 dead-reckoning) ─────────────────────────

@app.get("/api/v1/demo/telemetry")
async def demo_telemetry():
    global _avoidance_state, _demo_start_wall, _demo_sim_time, _demo_last_wall
    global _demo_min_cpa_nm, _demo_tcpa_at_min, _demo_max_rudder_deg, _demo_max_cross_track_nm, _demo_rot_samples, _demo_initial_lat, _demo_initial_lon
    if bridge.current_state != LifecycleState.ACTIVE:
        return {"error": "Lifecycle not active"}
    if bridge.scenario_id is None:
        return {"error": "No scenario configured"}

    detail = _store.get(bridge.scenario_id)
    if detail is None:
        return {"error": "Scenario not found"}

    now = time.time()
    if _avoidance_state is None or _demo_start_wall is None:
        yaml_data = yaml.safe_load(detail["yaml_content"])
        if not isinstance(yaml_data, dict):
            return {"error": "Invalid YAML"}
        own = yaml_data.get("ownShip", {})
        own_init = own.get("initial", {})
        own_pos = own_init.get("position", {})
        own_heading_deg = float(own_init.get("heading", 0))
        own_cog_deg = float(own_init.get("cog", 0))
        own_sog_kn = float(own_init.get("sog", 0))
        target_ships = yaml_data.get("targetShips", [])
        targets = []
        for ts in target_ships:
            ts_init = ts.get("initial", {})
            ts_pos = ts_init.get("position", {})
            ts_static = ts.get("static", {})
            mmsi = ts_static.get("mmsi")
            if mmsi is None and ts.get("id"):
                mmsi = abs(hash(ts["id"])) % 900000000 + 100000000
            targets.append(TargetState(
                lat=float(ts_pos.get("latitude", 0)),
                lon=float(ts_pos.get("longitude", 0)),
                heading_rad=math.radians(float(ts_init.get("heading", 0))),
                sog_ms=float(ts_init.get("sog", 0)) * 0.514444,
                mmsi=mmsi or 0,
            ))
        _avoidance_state = AvoidanceState(
            own_lat=float(own_pos.get("latitude", 0)),
            own_lon=float(own_pos.get("longitude", 0)),
            own_heading_rad=math.radians(own_heading_deg),
            own_sog_ms=own_sog_kn * 0.514444,
            own_cog_rad=math.radians(own_cog_deg),
            original_heading_rad=math.radians(own_heading_deg),
            targets=targets,
        )
        _demo_start_wall = now
        _demo_sim_time = 0.0
        _demo_last_wall = now
        _demo_initial_lat = _avoidance_state.own_lat
        _demo_initial_lon = _avoidance_state.own_lon

    sim_rate = bridge._sim_rate if hasattr(bridge, "_sim_rate") else 1.0
    dt = (now - _demo_last_wall) * sim_rate if _demo_last_wall is not None else 0.0
    _demo_last_wall = now
    _demo_sim_time += dt

    step_demo_avoidance(_avoidance_state, dt)

    if _avoidance_state is not None:
        for tgt in _avoidance_state.targets:
            dcpa = _dcpa_nm(
                _avoidance_state.own_lat, _avoidance_state.own_lon,
                _avoidance_state.own_heading_rad, _avoidance_state.own_sog_ms,
                tgt.lat, tgt.lon, tgt.heading_rad, tgt.sog_ms,
            )
            tcpa = _tcpa_s(
                _avoidance_state.own_lat, _avoidance_state.own_lon,
                _avoidance_state.own_heading_rad, _avoidance_state.own_sog_ms,
                tgt.lat, tgt.lon, tgt.heading_rad, tgt.sog_ms,
            )
            if _avoidance_state.heading_offset_rad > 0.43:
                if dcpa < _demo_min_cpa_nm:
                    _demo_min_cpa_nm = dcpa
                    _demo_tcpa_at_min = tcpa



        rudder_deg = abs(math.degrees(_avoidance_state.heading_offset_rad))
        if rudder_deg > _demo_max_rudder_deg:
            _demo_max_rudder_deg = rudder_deg


        if _demo_initial_lat is not None:
            xtk = _haversine_nm(
                _demo_initial_lat, _demo_initial_lon,
                _avoidance_state.own_lat, _avoidance_state.own_lon,
            )
            if xtk > _demo_max_cross_track_nm:
                _demo_max_cross_track_nm = xtk

        _demo_rot_samples.append(_avoidance_state.rot_rad_s * 180.0 / math.pi * 60.0)

    st = _avoidance_state
    target_states = []
    for tgt in st.targets:
        target_states.append({
            "mmsi": tgt.mmsi,
            "lat": tgt.lat,
            "lon": tgt.lon,
            "heading": tgt.heading_rad,
            "sog": tgt.sog_ms,
            "cog": tgt.heading_rad,
            "rot": 0.0,
            "ship_type": "Cargo",
            "mode": "replay",
        })

    return {
        "own_ship": {
            "lat": st.own_lat,
            "lon": st.own_lon,
            "heading": st.own_heading_rad,
            "sog": st.own_sog_ms,
            "cog": st.own_cog_rad,
            "rot": st.rot_rad_s,
            "u": st.own_sog_ms,
            "v": 0.0,
            "r": st.rot_rad_s,
            "rudder_angle": -st.rot_rad_s / 0.1,
            "throttle": 0.0,
        },
        "targets": target_states,
        "sim_time": st.sim_time,
    }


@app.post("/api/v1/demo/reset")
async def demo_reset():
    global _avoidance_state, _demo_start_wall, _demo_sim_time, _demo_last_wall
    global _demo_min_cpa_nm, _demo_tcpa_at_min, _demo_max_rudder_deg, _demo_max_cross_track_nm, _demo_rot_samples, _demo_initial_lat, _demo_initial_lon
    _avoidance_state = None
    _demo_start_wall = None
    _demo_sim_time = 0.0
    _demo_last_wall = None
    _demo_min_cpa_nm = float("inf")
    _demo_tcpa_at_min = 0.0
    _demo_max_rudder_deg = 0.0
    _demo_max_cross_track_nm = 0.0
    _demo_rot_samples = []
    _demo_initial_lat = None
    _demo_initial_lon = None
    return {"success": True}


# Static serve so /exports/{run_id}_evidence.marzip downloads work
EXPORT_DIR.mkdir(parents=True, exist_ok=True)
app.mount("/exports", StaticFiles(directory=str(EXPORT_DIR)), name="exports")

# Static serve so /runs/{run_id}/replay.arrow downloads work
RUN_DIR.mkdir(parents=True, exist_ok=True)
app.mount("/runs", StaticFiles(directory=str(RUN_DIR)), name="runs")
