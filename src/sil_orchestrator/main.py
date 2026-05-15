"""SIL Orchestrator — FastAPI REST API bridging frontend to ROS2 lifecycle.

Spec: docs/Design/SIL/2026-05-12-sil-architecture-design.md §1 orchestration plane
"""

import json
import os
import time
import warnings
from pathlib import Path

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles

from sil_orchestrator.config import RUN_DIR, EXPORT_DIR
from sil_orchestrator.scenario_store import ScenarioStore
from sil_orchestrator.selfcheck_routes import router as selfcheck_router
from sil_orchestrator.export_routes import router as export_router
from sil_orchestrator.scenario_routes import router as scenario_router
from sil_orchestrator.scoring_routes import router as scoring_router
from sil_orchestrator.lifecycle_bridge import LifecycleBridge, LifecycleState  # noqa: F401

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
    result = await bridge.configure(scenario_id)
    return {"success": result.success, "error": result.error}


@app.post("/api/v1/lifecycle/activate")
async def lifecycle_activate():
    result = await bridge.activate()
    run_id = None
    if result.success and bridge.scenario_id:
        detail = _store.get(bridge.scenario_id)
        backend = detail.get("backend", "demo") if detail else "demo"
        if backend == "ros2":
            run_id = _seed_run_dir_ros2(bridge.scenario_id)
        else:
            run_id = _seed_run_dir_demo(bridge.scenario_id)
    return {"success": result.success, "error": result.error, "run_id": run_id}


@app.post("/api/v1/lifecycle/deactivate")
async def lifecycle_deactivate():
    result = await bridge.deactivate()
    return {"success": result.success, "error": result.error, "run_id": _last_run_id}


@app.post("/api/v1/lifecycle/cleanup")
async def lifecycle_cleanup():
    """Reset to UNCONFIGURED so a new scenario can be configured.

    Idempotent + tolerant of stale ACTIVE state: if a previous test session left
    the FSM in ACTIVE (which the strict 4-state lifecycle would reject), this
    transparently deactivates first. Always converges to UNCONFIGURED.
    """
    if bridge.current_state == LifecycleState.UNCONFIGURED:
        return {"success": True, "error": ""}
    if bridge.current_state == LifecycleState.ACTIVE:
        deact = await bridge.deactivate()
        if not deact.success:
            return {"success": False, "error": deact.error}
    result = await bridge.cleanup()
    return {"success": result.success, "error": result.error}


# Self-check, export, scenario CRUD, and scoring routes
app.include_router(selfcheck_router)
app.include_router(export_router)
app.include_router(scenario_router)
app.include_router(scoring_router)

# Static serve so /exports/{run_id}_evidence.marzip downloads work
EXPORT_DIR.mkdir(parents=True, exist_ok=True)
app.mount("/exports", StaticFiles(directory=str(EXPORT_DIR)), name="exports")
