"""SIL Orchestrator — FastAPI REST API bridging frontend to ROS2 lifecycle.

Spec: docs/Design/SIL/2026-05-12-sil-architecture-design.md §1 orchestration plane
"""

import json
import math
import os
import time
import warnings
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
from sil_orchestrator.lifecycle_bridge import LifecycleBridge, LifecycleState, ScenarioInjectionError, _copy_preflight_evidence  # noqa: F401

import rclpy
from rclpy.callback_groups import ReentrantCallbackGroup
from rclpy.executors import MultiThreadedExecutor
import threading

rclpy.init(args=None)

app = FastAPI(title="SIL Orchestrator", version="0.1.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

_cb_group = ReentrantCallbackGroup()
bridge = LifecycleBridge(callback_group=_cb_group)

def _spin_bridge():
    executor = MultiThreadedExecutor(num_threads=4)
    executor.add_node(bridge)
    executor.spin()

threading.Thread(target=_spin_bridge, daemon=True).start()

_store = ScenarioStore()
_last_run_id: str | None = None
_demo_initial_state: dict | None = None
_demo_start_wall: float | None = None


def _seed_run_dir(scenario_id: str) -> str:
    """DEPRECATED: use _seed_run_dir_ros2() instead."""
    warnings.warn(
        "_seed_run_dir is deprecated; use _seed_run_dir_ros2",
        DeprecationWarning,
        stacklevel=2,
    )
    return _seed_run_dir_demo(scenario_id)


def _seed_run_dir_demo(scenario_id: str) -> str:
    """Legacy DEMO-1 path: write hardcoded scoring stub.

    Phase 1: stubs scoring KPIs from the YAML's expected block. Phase 2 wires
    real bag + Arrow output from the rosbag2_recorder + scoring_node.
    """
    global _last_run_id
    run_id = f"run-{int(time.time() * 1000):x}"
    _last_run_id = run_id
    run_path = RUN_DIR / run_id
    run_path.mkdir(parents=True, exist_ok=True)
    detail = _store.get(scenario_id)
    if detail is not None:
        (run_path / "scenario.yaml").write_text(detail["yaml_content"])
        (run_path / "scenario.sha256").write_text(detail["hash"])
    stub = {
        "run_id": run_id,
        "scenario_id": scenario_id,
        "started_at": time.time(),
        "kpis": {
            "min_cpa_nm": 0.42,
            "avg_rot_dpm": 2.1,
            "distance_nm": 4.8,
            "duration_s": 342,
        },
        "rule_chain": [
            "Rule 14 (Head-on)",
            "Rule 8 (Action to avoid collision)",
            "Rule 16 (Give-way)",
        ],
        "verdict": "pending",
    }
    (run_path / "scoring.json").write_text(json.dumps(stub, indent=2))
    return run_id


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
    return {
        "current_state": bridge.current_state.value,
        "scenario_id": bridge.scenario_id,
        "run_id": _last_run_id,
    }


@app.post("/api/v1/lifecycle/configure")
async def lifecycle_configure(request: dict):
    scenario_id = request.get("scenario_id", "")
    detail = _store.get(scenario_id)
    backend = detail.get("backend", "demo") if detail else "demo"
    if backend == "ros2":
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
    if backend != "ros2" and bridge.scenario_id:
        bridge._state = LifecycleState.ACTIVE
        run_id = _seed_run_dir_demo(bridge.scenario_id)
        _copy_preflight_evidence(bridge.scenario_id, run_id)
        return {"success": True, "error": "", "run_id": run_id}
    result = await bridge.activate()
    run_id = None
    if result.success and bridge.scenario_id:
        if backend == "ros2":
            run_id = _seed_run_dir_ros2(bridge.scenario_id)
        else:
            run_id = _seed_run_dir_demo(bridge.scenario_id)
        _copy_preflight_evidence(bridge.scenario_id, run_id)
    return {"success": result.success, "error": result.error, "run_id": run_id}


@app.post("/api/v1/lifecycle/deactivate")
async def lifecycle_deactivate():
    result = await bridge.deactivate()
    return {"success": result.success, "error": result.error, "run_id": _last_run_id}


@app.post("/api/v1/lifecycle/cleanup")
async def lifecycle_cleanup():
    """Reset to UNCONFIGURED so a new scenario can be configured.

    Idempotent + tolerant of stale ACTIVE state: uses the real ROS2 node state
    (not the Python mirror which can be stale after a restart) to decide which
    transitions to issue before cleanup.
    """
    global _demo_initial_state, _demo_start_wall
    detail = _store.get(bridge.scenario_id) if bridge.scenario_id else None
    backend = detail.get("backend", "demo") if detail else "demo"
    if backend != "ros2":
        bridge._state = LifecycleState.UNCONFIGURED
        bridge._scenario_id = None
        _demo_initial_state = None
        _demo_start_wall = None
        return {"success": True, "error": ""}
    result = await bridge._reset_to_unconfigured()
    _demo_initial_state = None
    _demo_start_wall = None
    return {"success": result.success, "error": result.error}


# Self-check, export, scenario CRUD, and scoring routes
app.include_router(selfcheck_router)
app.include_router(export_router)
app.include_router(scenario_router)
app.include_router(schema_router)
app.include_router(scoring_router)
app.include_router(ops_router)
app.include_router(arrow_router)
app.include_router(kpi_router)

# ── Demo telemetry (non-ROS2 dead-reckoning) ─────────────────────────

def _dead_reckon_step(lat: float, lon: float, heading_rad: float, sog_ms: float, dt: float):
    lat_rad = math.radians(lat)
    lat += sog_ms * math.cos(heading_rad) * dt / 111120.0
    lon += sog_ms * math.sin(heading_rad) * dt / (111120.0 * math.cos(lat_rad))
    return lat, lon


@app.get("/api/v1/demo/telemetry")
async def demo_telemetry():
    global _demo_initial_state, _demo_start_wall
    if bridge.current_state != LifecycleState.ACTIVE:
        return {"error": "Lifecycle not active"}
    if bridge.scenario_id is None:
        return {"error": "No scenario configured"}

    detail = _store.get(bridge.scenario_id)
    if detail is None:
        return {"error": "Scenario not found"}

    now = time.time()
    if _demo_initial_state is None or _demo_start_wall is None:
        yaml_data = yaml.safe_load(detail["yaml_content"])
        if not isinstance(yaml_data, dict):
            return {"error": "Invalid YAML"}
        own = yaml_data.get("ownShip", {})
        own_init = own.get("initial", {})
        own_pos = own_init.get("position", {})
        target_ships = yaml_data.get("targetShips", [])
        targets = []
        for ts in target_ships:
            ts_init = ts.get("initial", {})
            ts_pos = ts_init.get("position", {})
            ts_static = ts.get("static", {})
            mmsi = ts_static.get("mmsi")
            if mmsi is None and ts.get("id"):
                mmsi = abs(hash(ts["id"])) % 900000000 + 100000000
            targets.append({
                "mmsi": mmsi or 0,
                "lat": float(ts_pos.get("latitude", 0)),
                "lon": float(ts_pos.get("longitude", 0)),
                "heading_deg": float(ts_init.get("heading", 0)),
                "sog_kn": float(ts_init.get("sog", 0)),
            })
        _demo_initial_state = {
            "own_lat": float(own_pos.get("latitude", 0)),
            "own_lon": float(own_pos.get("longitude", 0)),
            "own_heading_deg": float(own_init.get("heading", 0)),
            "own_sog_kn": float(own_init.get("sog", 0)),
            "own_cog_deg": float(own_init.get("cog", 0)),
            "targets": targets,
        }
        _demo_start_wall = now

    init = _demo_initial_state
    dt = now - _demo_start_wall
    own_heading_rad = math.radians(init["own_heading_deg"])
    own_sog_ms = init["own_sog_kn"] * 0.514444
    own_lat, own_lon = _dead_reckon_step(
        init["own_lat"], init["own_lon"], own_heading_rad, own_sog_ms, dt
    )

    target_states = []
    for t in init["targets"]:
        t_heading_rad = math.radians(t["heading_deg"])
        t_sog_ms = t["sog_kn"] * 0.514444
        t_lat, t_lon = _dead_reckon_step(
            t["lat"], t["lon"], t_heading_rad, t_sog_ms, dt
        )
        target_states.append({
            "mmsi": t["mmsi"],
            "lat": t_lat,
            "lon": t_lon,
            "heading": t_heading_rad,
            "sog": t_sog_ms,
            "cog": t_heading_rad,
            "rot": 0.0,
            "ship_type": "Cargo",
            "mode": "replay",
        })

    return {
        "own_ship": {
            "lat": own_lat,
            "lon": own_lon,
            "heading": own_heading_rad,
            "sog": own_sog_ms,
            "cog": math.radians(init["own_cog_deg"]),
            "rot": 0.0,
            "u": own_sog_ms,
            "v": 0.0,
            "r": 0.0,
            "rudder_angle": 0.0,
            "throttle": 0.0,
        },
        "targets": target_states,
        "sim_time": dt,
    }


@app.post("/api/v1/demo/reset")
async def demo_reset():
    global _demo_initial_state, _demo_start_wall
    _demo_initial_state = None
    _demo_start_wall = None
    return {"success": True}


# Static serve so /exports/{run_id}_evidence.marzip downloads work
EXPORT_DIR.mkdir(parents=True, exist_ok=True)
app.mount("/exports", StaticFiles(directory=str(EXPORT_DIR)), name="exports")

# Static serve so /runs/{run_id}/replay.arrow downloads work
RUN_DIR.mkdir(parents=True, exist_ok=True)
app.mount("/runs", StaticFiles(directory=str(RUN_DIR)), name="runs")
