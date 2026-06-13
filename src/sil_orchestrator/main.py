"""SIL Orchestrator — FastAPI REST API bridging frontend to ROS2 lifecycle.

Spec: docs/Design/SIL/2026-05-12-sil-architecture-design.md §1 orchestration plane
"""

import json
import os
import time
import warnings
import threading
from pathlib import Path

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
from sil_orchestrator.routers.debug_routes import router as debug_router
from sil_orchestrator.encounters_routes import router as encounters_router
from sil_orchestrator.integration.routes import router as integration_router
from sil_orchestrator.runtime.routes import router as runtime_router
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


def _require_ros2() -> None:
    """Raise RuntimeError if rclpy is not available. Called at startup and in tests."""
    if not _HAS_RCLPY:
        raise RuntimeError(
            "rclpy not available: ROS2 mode is required for DEMO-1. "
            "Ensure ROS2 environment is initialized "
            "(source install/setup.bash && export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp)"
        )


app = FastAPI(title="SIL Orchestrator", version="0.1.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

# Fail-fast: raise at server startup if rclpy is missing.
# _require_ros2() is also directly callable in tests (monkeypatch _HAS_RCLPY).
@app.on_event("startup")
async def _startup_require_ros2():
    _require_ros2()


if _HAS_RCLPY:
    rclpy.init(args=None)
    _cb_group = ReentrantCallbackGroup()
    bridge = LifecycleBridge(callback_group=_cb_group)
    app.state.bridge = bridge

    def _spin_bridge():
        executor = MultiThreadedExecutor(num_threads=4)
        executor.add_node(bridge)
        executor.spin()

    threading.Thread(target=_spin_bridge, daemon=True).start()
else:
    # bridge is None when rclpy is absent; server will refuse requests at startup
    bridge = None  # type: ignore[assignment]
    app.state.bridge = None

_store = ScenarioStore()
_last_run_id: str | None = None


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
    effective_backend = "ros2"

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
    try:
        result = await bridge.configure(scenario_id)
    except ScenarioInjectionError as exc:
        return {"success": False, "error": str(exc)}
    return {"success": result.success, "error": result.error}


@app.post("/api/v1/lifecycle/activate")
async def lifecycle_activate():
    # The bridge (sil_topic_bridge.py DebugTraceWriter) owns the trace file
    # handle and truncates on ACTIVE.  Touching the file from the orchestrator
    # side races the bridge's open handle and creates sparse (NUL-padded)
    # corruption — see fix/debug-snapshot-stale-trace for forensic detail.
    result = await bridge.activate()
    run_id = None
    if result.success and bridge.scenario_id:
        run_id = _seed_run_dir_ros2(bridge.scenario_id)
        if _copy_preflight_evidence:
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
    result = await bridge._reset_to_unconfigured()
    return {"success": result.success, "error": result.error}


@app.post("/api/v1/lifecycle/rate")
async def lifecycle_rate(request: dict):
    rate = request.get("rate", 1.0)
    result = await bridge.set_sim_rate(rate)
    return {"success": result.success, "error": result.error}


# Self-check, export, scenario CRUD, scoring, and encounter injection routes
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
app.include_router(debug_router)
app.include_router(encounters_router)
app.include_router(integration_router)
app.include_router(runtime_router)

# Static serve so /exports/{run_id}_evidence.marzip downloads work
EXPORT_DIR.mkdir(parents=True, exist_ok=True)
app.mount("/exports", StaticFiles(directory=str(EXPORT_DIR)), name="exports")

# Static serve so /runs/{run_id}/replay.arrow downloads work
RUN_DIR.mkdir(parents=True, exist_ok=True)
app.mount("/runs", StaticFiles(directory=str(RUN_DIR)), name="runs")
