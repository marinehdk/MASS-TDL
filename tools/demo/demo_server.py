"""SIL Demo-1 Head-On scenario REST + WebSocket server.

FastAPI on :8000 serves scenario/lifecycle/selfcheck/scoring/export/fault endpoints.
Asyncio WebSocket on :8765 broadcasts trajectory frames at 10 Hz.
"""

import asyncio
import hashlib
import json
import pathlib
import sys
import time
from contextlib import asynccontextmanager
from typing import Any, Dict, List, Optional, Set

# ── Ensure sibling trajectory.py is importable ──
sys.path.insert(0, str(pathlib.Path(__file__).parent))
from trajectory import compute_frames, compute_cpa, ASDR_EVENTS, DT

# ── Module-level constants ──
SOG_KN = 10.0

# ── Pre-computed trajectory data ──
_FRAMES: List[Dict] = compute_frames()
_CPA_NM: float = compute_cpa(_FRAMES)
_N_FRAMES = len(_FRAMES)
_SCENARIO_ID = "imazu-01-ho-v1.0"
_SCENARIO_SHA256 = "abc123demo"

# ── Shared runtime state ──
_ws_clients: Set = set()
_lifecycle_state = "UNCONFIGURED"
_run_id: Optional[str] = None
_scenario_config: Optional[Dict] = None
_broadcast_task: Optional[asyncio.Task] = None
_ws_server: Optional[Any] = None
_current_frame_idx = 0
_start_time: Optional[float] = None
_faults: List[Dict] = []


# ── WebSocket helpers ──

async def _send_all(topic: str, payload: Dict) -> None:
    """Broadcast a message to all connected WS clients, pruning dead ones."""
    dead = set()
    msg = json.dumps({"topic": topic, "payload": payload})
    for ws in _ws_clients:
        try:
            await ws.send(msg)
        except Exception:
            dead.add(ws)
    if dead:
        _ws_clients.difference_update(dead)


async def _ws_handler(websocket) -> None:
    """Handle a single WebSocket connection."""
    _ws_clients.add(websocket)
    try:
        await websocket.send(json.dumps({
            "topic": "/sil/lifecycle_status",
            "payload": {
                "state": _lifecycle_state,
                "run_id": _run_id,
                "frame": _current_frame_idx,
                "timestamp": time.time(),
            },
        }))
        async for _msg in websocket:
            pass
    except Exception:
        pass
    finally:
        _ws_clients.discard(websocket)


async def _run_ws_server() -> None:
    """Run the asyncio WebSocket server on port 8765."""
    import websockets.server
    global _ws_server
    async with websockets.server.serve(_ws_handler, "0.0.0.0", 8765) as server:
        _ws_server = server
        await asyncio.Future()


# ── Broadcast loop ──

async def _broadcast_loop() -> None:
    """Broadcast trajectory frames at 10 Hz."""
    global _current_frame_idx, _lifecycle_state, _start_time

    _start_time = time.time()
    next_frame_time = _start_time
    frame_interval = DT

    last_module_pulse = 0.0
    last_lifecycle = 0.0

    while _lifecycle_state == "ACTIVE" and _current_frame_idx < _N_FRAMES:
        now = time.time()
        if now < next_frame_time:
            await asyncio.sleep(max(0.0, next_frame_time - now))
            continue

        frame = _FRAMES[_current_frame_idx]
        t = frame["t"]

        await _send_all("/sil/own_ship_state", {
            "timestamp": t,
            "latitude_deg": frame["os"]["lat"],
            "longitude_deg": frame["os"]["lon"],
            "heading_rad": frame["os"]["hdg_rad"],
            "cog_rad": frame["os"]["cog_rad"],
            "sog_ms": frame["os"]["sog_ms"],
            "rot_rps": frame["os"]["rot_rps"],
            "controlState": {
                "rudderAngle_deg": float(frame["os"]["hdg_rad"] * 180.0 / 3.141592653589793),
                "thruster_pct": 50.0,
            },
        })

        await _send_all("/sil/target_vessel_state", {
            "timestamp": t,
            "vessels": [
                {
                    "mmsi": 100000001,
                    "latitude_deg": frame["ts"]["lat"],
                    "longitude_deg": frame["ts"]["lon"],
                    "heading_rad": frame["ts"]["hdg_rad"],
                    "cog_rad": frame["ts"]["cog_rad"],
                    "sog_ms": frame["ts"]["sog_ms"],
                    "rot_rps": frame["ts"]["rot_rps"],
                },
            ],
        })

        for evt in ASDR_EVENTS:
            if abs(evt["t"] - t) < (DT / 2.0):
                await _send_all("/sil/asdr_event", {
                    "timestamp": evt["t"],
                    "event_type": evt["event_type"],
                    "severity": evt["severity"],
                    "decision": evt["decision"],
                    "rationale": evt["rationale"],
                })

        if now - last_module_pulse >= 1.0:
            last_module_pulse = now
            await _send_all("/sil/module_pulse", {
                "timestamp": t,
                "pulses": {
                    "M1": {"alive": True, "hz": 0.5},
                    "M2": {"alive": True, "hz": 10.0},
                    "M3": {"alive": True, "hz": 0.1},
                    "M4": {"alive": True, "hz": 2.0},
                    "M5": {"alive": True, "hz": 2.0},
                    "M6": {"alive": True, "hz": 2.0},
                    "M7": {"alive": True, "hz": 10.0},
                    "M8": {"alive": True, "hz": 50.0},
                },
            })

        if now - last_lifecycle >= 1.0:
            last_lifecycle = now
            await _send_all("/sil/lifecycle_status", {
                "state": _lifecycle_state,
                "run_id": _run_id,
                "frame": _current_frame_idx,
                "timestamp": now,
            })

        _current_frame_idx += 1
        next_frame_time = _start_time + (_current_frame_idx * frame_interval)

    if _lifecycle_state == "ACTIVE":
        _lifecycle_state = "FINALIZED"
        await _send_all("/sil/lifecycle_status", {
            "state": _lifecycle_state,
            "run_id": _run_id,
            "frame": _current_frame_idx,
            "timestamp": time.time(),
        })


# ── FastAPI lifespan ──

@asynccontextmanager
async def lifespan(app):
    """Start WebSocket server and background tasks."""
    global _broadcast_task, _ws_server
    ws_task = asyncio.create_task(_run_ws_server())
    yield
    if _broadcast_task and not _broadcast_task.done():
        _broadcast_task.cancel()
    ws_task.cancel()
    try:
        await ws_task
    except asyncio.CancelledError:
        pass


# ── FastAPI app ──

from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware

app = FastAPI(title="SIL Demo-1 Server", lifespan=lifespan)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


# ── Scenario endpoints ──

@app.get("/api/v1/scenarios")
async def list_scenarios():
    return [
        {
            "id": _SCENARIO_ID,
            "name": "Imazu-01 Head-on (Rule 14)",
            "encounter_type": "head_on",
        },
    ]


@app.get("/api/v1/scenarios/{scenario_id}")
async def get_scenario(scenario_id: str):
    if scenario_id != _SCENARIO_ID:
        raise HTTPException(status_code=404, detail="Scenario not found")
    yaml_content = f"""id: {_SCENARIO_ID}
name: Imazu-01 Head-on (Rule 14)
encounter_type: head_on
duration_s: 700
sog_kn: {SOG_KN}
frames: {_N_FRAMES}
cpa_nm: {_CPA_NM:.4f}
"""
    h = hashlib.sha256(yaml_content.encode()).hexdigest()
    return {"yaml": yaml_content, "sha256": h}


@app.post("/api/v1/scenarios")
async def create_scenario(body: Optional[Dict] = None):
    return {"id": "new-scenario-stub", "created": True}


@app.put("/api/v1/scenarios/{scenario_id}")
async def update_scenario(scenario_id: str, body: Optional[Dict] = None):
    return {"id": scenario_id, "updated": True}


@app.delete("/api/v1/scenarios/{scenario_id}")
async def delete_scenario(scenario_id: str):
    return None


@app.post("/api/v1/scenarios/validate")
async def validate_scenario(body: Optional[Dict] = None):
    return {"valid": True, "errors": []}


# ── Lifecycle endpoints ──

@app.get("/api/v1/lifecycle/status")
async def lifecycle_status():
    return {
        "state": _lifecycle_state,
        "run_id": _run_id,
        "frame": _current_frame_idx,
        "scenario_id": _scenario_config.get("scenario_id") if _scenario_config else None,
    }


@app.post("/api/v1/lifecycle/configure")
async def lifecycle_configure(body: Dict):
    global _lifecycle_state, _run_id, _scenario_config, _current_frame_idx, _start_time
    if _lifecycle_state not in ("UNCONFIGURED", "FINALIZED"):
        raise HTTPException(status_code=409, detail=f"Cannot configure from state {_lifecycle_state}")
    _scenario_config = body
    _run_id = f"run-{int(time.time() * 1000)}"
    _lifecycle_state = "CONFIGURED"
    _current_frame_idx = 0
    _start_time = None
    return {"state": _lifecycle_state, "run_id": _run_id, "success": True}


@app.post("/api/v1/lifecycle/activate")
async def lifecycle_activate(body: Optional[Dict] = None):
    global _lifecycle_state, _broadcast_task, _start_time
    if _lifecycle_state != "CONFIGURED":
        raise HTTPException(status_code=409, detail=f"Cannot activate from state {_lifecycle_state}")
    _lifecycle_state = "ACTIVE"
    _start_time = time.time()
    _broadcast_task = asyncio.create_task(_broadcast_loop())
    return {"state": _lifecycle_state, "run_id": _run_id, "success": True}


@app.post("/api/v1/lifecycle/deactivate")
async def lifecycle_deactivate(body: Optional[Dict] = None):
    global _lifecycle_state, _broadcast_task
    if _lifecycle_state != "ACTIVE":
        raise HTTPException(status_code=409, detail=f"Cannot deactivate from state {_lifecycle_state}")
    _lifecycle_state = "FINALIZED"
    if _broadcast_task and not _broadcast_task.done():
        _broadcast_task.cancel()
    return {"state": _lifecycle_state, "run_id": _run_id, "success": True}


@app.post("/api/v1/lifecycle/cleanup")
async def lifecycle_cleanup(body: Optional[Dict] = None):
    global _lifecycle_state, _run_id, _scenario_config, _current_frame_idx, _start_time, _broadcast_task, _faults
    if _broadcast_task and not _broadcast_task.done():
        _broadcast_task.cancel()
    _lifecycle_state = "UNCONFIGURED"
    _run_id = None
    _scenario_config = None
    _current_frame_idx = 0
    _start_time = None
    _faults = []
    return {"state": _lifecycle_state, "success": True}


# ── Selfcheck endpoints ──

@app.post("/api/v1/selfcheck/probe")
async def selfcheck_probe(body: Optional[Dict] = None):
    checks = []
    for i in range(1, 6):
        await asyncio.sleep(0.1)
        checks.append({
            "name": f"enc_load" if i == 1 else f"asdr_start" if i == 2 else f"utc_sync" if i == 3 else f"module_health" if i == 4 else f"scenario_sig",
            "passed": True,
            "detail": f"Check {i} passed",
        })
    return {"all_clear": True, "items": checks}


@app.get("/api/v1/selfcheck/status")
async def selfcheck_status():
    return {
        "module_pulses": {
            "M1": {"alive": True, "hz": 0.5},
            "M2": {"alive": True, "hz": 10.0},
            "M3": {"alive": True, "hz": 0.1},
            "M4": {"alive": True, "hz": 2.0},
            "M5": {"alive": True, "hz": 2.0},
            "M6": {"alive": True, "hz": 2.0},
            "M7": {"alive": True, "hz": 10.0},
            "M8": {"alive": True, "hz": 50.0},
        },
    }


# ── Scoring endpoint ──

@app.get("/api/v1/scoring/last_run")
async def scoring_last_run():
    distance_nm = round(SOG_KN * 700.0 / 3600.0, 2)
    return {
        "kpis": {
            "min_cpa_nm": round(_CPA_NM, 4),
            "avg_rot_dpm": 21.6,
            "distance_nm": distance_nm,
            "duration_s": 700,
            "max_rud_deg": 35.0,
            "decision_p99_ms": 312,
            "faults_injected": 0,
            "scenario_sha256": _SCENARIO_SHA256,
        },
        "run_id": _run_id,
        "scenario_id": _scenario_config.get("scenario_id") if _scenario_config else _SCENARIO_ID,
    }


# ── Export endpoints ──

@app.post("/api/v1/export/marzip")
async def export_marzip(body: Optional[Dict] = None):
    return {"export_id": "exp-stub", "status": "pending"}


@app.get("/api/v1/export/status/{run_id}")
async def export_status(run_id: str):
    return {"export_id": run_id, "status": "completed", "url": None}


# ── Fault endpoints ──

@app.post("/api/v1/fault/trigger")
async def fault_trigger(body: Dict):
    fault_id = f"fault-{int(time.time() * 1000)}"
    _faults.append({"id": fault_id, **body})
    return {"fault_id": fault_id, "triggered": True}


@app.post("/api/v1/fault/inject")
async def fault_inject(body: Dict):
    fault_id = f"fault-{int(time.time() * 1000)}"
    _faults.append({"id": fault_id, **body})
    return {"fault_id": fault_id, "injected": True}


@app.delete("/api/v1/fault/{fault_id}")
async def fault_delete(fault_id: str):
    global _faults
    _faults = [f for f in _faults if f["id"] != fault_id]
    return {"deleted": fault_id}


# ── Entrypoint ──

if __name__ == "__main__":
    import uvicorn
    uvicorn.run("demo_server:app", host="0.0.0.0", port=8000, reload=False)
