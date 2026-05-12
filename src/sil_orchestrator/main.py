"""SIL Orchestrator — FastAPI REST API bridging frontend to ROS2 lifecycle.

Spec: docs/Design/SIL/2026-05-12-sil-architecture-design.md §1 orchestration plane
"""

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from sil_orchestrator.lifecycle_bridge import LifecycleBridge, LifecycleState

app = FastAPI(title="SIL Orchestrator", version="0.1.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

bridge = LifecycleBridge()


@app.get("/api/v1/health")
async def health():
    return {"status": "ok"}


@app.get("/api/v1/lifecycle/status")
async def lifecycle_status():
    return {
        "current_state": bridge.current_state.value,
        "scenario_id": bridge.scenario_id,
    }


@app.post("/api/v1/lifecycle/configure")
async def lifecycle_configure(request: dict):
    scenario_id = request.get("scenario_id", "")
    result = await bridge.configure(scenario_id)
    return {"success": result.success, "error": result.error}


@app.post("/api/v1/lifecycle/activate")
async def lifecycle_activate():
    result = await bridge.activate()
    return {"success": result.success, "error": result.error}


@app.post("/api/v1/lifecycle/deactivate")
async def lifecycle_deactivate():
    result = await bridge.deactivate()
    return {"success": result.success, "error": result.error}
