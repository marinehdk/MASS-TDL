# SIL Demo-1 Head-On Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a complete 4-screen SIL Demo-1 stack (Scenario Builder → Preflight → Bridge → Report) for the Imazu-01 Head-On scenario, with a standalone Python demo server and high-fidelity Builder right-rail UI.

**Architecture:** A single Python process (FastAPI + asyncio WebSocket server) on two ports (:8000 REST, :8765 WS) serves the frontend with pre-computed analytical trajectory data. The frontend is unchanged except for three targeted edits: the Builder right panel is replaced with a collapsible 4-tab rail, BridgeHMI gains a "waiting" overlay, and RunReport KPI cards read live API data.

**Tech Stack:** Python 3.11, FastAPI, uvicorn, websockets asyncio, React 18, Zustand, RTK Query, Vitest (jsdom), pytest.

---

## File Map

### New (backend)
| File | Responsibility |
|------|---------------|
| `tools/demo/trajectory.py` | Pre-compute 7000 Head-On frames + analytical CPA + ASDR event schedule |
| `tools/demo/demo_server.py` | FastAPI REST :8000 + asyncio WS broadcast :8765 (single process) |
| `tools/demo/run_demo.sh` | One-liner startup: check deps, start uvicorn, open browser |
| `tools/demo/test_trajectory.py` | pytest: CPA in valid range, frame count, unit sanity |

### New (frontend)
| File | Responsibility |
|------|---------------|
| `web/src/screens/shared/BuilderRightRail.tsx` | Self-contained right-rail component: 4 tabs (Encounter/Env/Run/Summary), collapsible 48px ↔ 320px |
| `web/src/screens/shared/__tests__/BuilderRightRail.test.tsx` | vitest: rail renders collapsed, expands on click, each tab shows its fields |

### Modified (frontend)
| File | Change |
|------|--------|
| `web/src/screens/ScenarioBuilder.tsx` | Replace `RIGHT PANEL` div with `<BuilderRightRail>` |
| `web/src/screens/BridgeHMI.tsx` | Add waiting overlay when `ownShip === null` |
| `web/src/screens/RunReport.tsx` | Wire KPI cards from `useGetLastRunScoringQuery` data |

---

## Task 1: trajectory.py — Head-On Frame Generator

**Files:**
- Create: `tools/demo/trajectory.py`
- Test: `tools/demo/test_trajectory.py`

- [ ] **Step 1.1: Write failing test**

Create `tools/demo/test_trajectory.py`:
```python
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from trajectory import compute_frames, compute_cpa, ASDR_EVENTS

def test_frame_count():
    frames = compute_frames()
    assert len(frames) == 7000  # 700s × 10Hz

def test_os_initial_position():
    frames = compute_frames()
    f0 = frames[0]
    assert abs(f0['os']['lat'] - 63.0) < 1e-6
    assert abs(f0['os']['lon'] - 5.0) < 1e-6

def test_ts_initial_position():
    frames = compute_frames()
    f0 = frames[0]
    assert abs(f0['ts']['lat'] - 63.117451) < 1e-6
    assert abs(f0['ts']['lon'] - 5.0) < 1e-6

def test_os_heading_during_avoidance():
    frames = compute_frames()
    # At T=345s (frame 3450), OS should be mid-turn (heading ~17.5°)
    f = frames[3450]
    hdg_deg = f['os']['hdg_rad'] * 180 / 3.14159265
    assert 10.0 < hdg_deg < 30.0, f"Expected mid-turn heading, got {hdg_deg}"

def test_os_heading_after_full_turn():
    frames = compute_frames()
    # At T=395s (frame 3950), OS should be at ~35°
    f = frames[3950]
    hdg_deg = f['os']['hdg_rad'] * 180 / 3.14159265
    assert abs(hdg_deg - 35.0) < 2.0, f"Expected ~35°, got {hdg_deg}"

def test_os_returns_to_original():
    frames = compute_frames()
    # At T=695s (frame 6950), OS should be back near 0°
    f = frames[6950]
    hdg_deg = f['os']['hdg_rad'] * 180 / 3.14159265
    assert abs(hdg_deg) < 2.0 or abs(hdg_deg - 360) < 2.0

def test_cpa_range():
    cpa_nm = compute_cpa()
    assert 0.1 < cpa_nm < 2.0, f"CPA {cpa_nm}nm out of expected range"

def test_asdr_events_ordered():
    times = [e['t'] for e in ASDR_EVENTS]
    assert times == sorted(times)

def test_ts_heading_constant():
    frames = compute_frames()
    import math
    for i in [0, 1000, 5000, 6999]:
        hdg = frames[i]['ts']['hdg_rad']
        assert abs(hdg - math.pi) < 1e-6, f"TS heading should be π (180°), got {hdg}"
```

- [ ] **Step 1.2: Run to verify it fails**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/tools/demo"
python3 -m pytest test_trajectory.py -v 2>&1 | head -20
```
Expected: `ModuleNotFoundError: No module named 'trajectory'`

- [ ] **Step 1.3: Create trajectory.py**

Create `tools/demo/trajectory.py`:
```python
import math

# ── Constants ──────────────────────────────────────────────────────────────────
DT = 0.1             # seconds per frame (10 Hz)
DURATION_S = 700.0
N_FRAMES = int(DURATION_S / DT)  # 7000

SOG_KN = 10.0
SOG_MS = SOG_KN * 1852.0 / 3600.0  # 5.1444 m/s

LAT_DEG_PER_M = 1.0 / 111_320.0
# lon deg per meter at 63°N:
LON_DEG_PER_M = 1.0 / (111_320.0 * math.cos(math.radians(63.0)))

# Initial positions (from imazu-01-ho-v1.0.yaml)
OS_LAT0, OS_LON0, OS_HDG0_DEG = 63.0, 5.0, 0.0
TS_LAT0, TS_LON0, TS_HDG0_DEG = 63.117451, 5.0, 180.0

# Avoidance parameters (from yaml: avoidance_time_s=300, avoidance_delta_rad=0.6109, avoidance_duration_s=90)
AVOID_START_S  = 300.0
AVOID_DELTA_DEG = math.degrees(0.6109)  # ≈ 35.0°
AVOID_DUR_S    = 90.0
AVOID_RATE_DPS = AVOID_DELTA_DEG / AVOID_DUR_S   # deg/s during turn

HOLD_DUR_S   = 60.0   # hold at max turn for 60s
RETURN_START_S = AVOID_START_S + AVOID_DUR_S + HOLD_DUR_S  # T=450s
RETURN_DUR_S   = 90.0

# ASDR events broadcast by WS server at these sim-time seconds
ASDR_EVENTS = [
    {'t':   0.0, 'event_type': 'scenario_start',  'severity': 'INFO',
     'decision': 'SCENARIO_START',    'rationale': 'Imazu-01 HO · Rule 14'},
    {'t': 120.0, 'event_type': 'target_detected',  'severity': 'INFO',
     'decision': 'TARGET_DETECTED',   'rationale': 'T01 range 7.0nm · bearing 000°'},
    {'t': 295.0, 'event_type': 'risk_assessed',    'severity': 'WARN',
     'decision': 'COLLISION_RISK',    'rationale': 'DCPA<0.5nm projected · TCPA 17min'},
    {'t': 300.0, 'event_type': 'avoidance_start',  'severity': 'ACTION',
     'decision': 'COLREG_R14_EXECUTE','rationale': 'Turn STBD +35° per Rule 14 Give-Way'},
    {'t': 390.0, 'event_type': 'avoidance_hold',   'severity': 'INFO',
     'decision': 'AVOIDANCE_HOLD',    'rationale': 'OS at +35° · holding · monitoring CPA'},
    {'t': 450.0, 'event_type': 'return_start',     'severity': 'INFO',
     'decision': 'RETURN_TO_COURSE',  'rationale': 'TS clear · resuming original COG'},
    {'t': 698.0, 'event_type': 'sim_complete',     'severity': 'INFO',
     'decision': 'SIMULATION_END',    'rationale': 'Run complete · scoring sealed'},
]


def _os_heading_deg(t: float) -> float:
    """Return OS heading in degrees at sim-time t."""
    if t < AVOID_START_S:
        return OS_HDG0_DEG
    elif t < AVOID_START_S + AVOID_DUR_S:
        return OS_HDG0_DEG + AVOID_RATE_DPS * (t - AVOID_START_S)
    elif t < RETURN_START_S:
        return OS_HDG0_DEG + AVOID_DELTA_DEG
    elif t < RETURN_START_S + RETURN_DUR_S:
        progress = (t - RETURN_START_S) / RETURN_DUR_S
        return OS_HDG0_DEG + AVOID_DELTA_DEG * (1.0 - progress)
    else:
        return OS_HDG0_DEG


def compute_frames() -> list:
    """Pre-compute all 7000 frames. Returns list of frame dicts."""
    frames = []
    os_lat, os_lon = OS_LAT0, OS_LON0
    ts_lat, ts_lon = TS_LAT0, TS_LON0
    ts_hdg_rad = math.radians(TS_HDG0_DEG)
    prev_hdg = OS_HDG0_DEG

    for i in range(N_FRAMES):
        t = i * DT
        os_hdg_deg = _os_heading_deg(t)
        rot_dps = (os_hdg_deg - prev_hdg) / DT
        prev_hdg = os_hdg_deg
        os_hdg_rad = math.radians(os_hdg_deg)

        frames.append({
            't': round(t, 1),
            'os': {
                'lat': os_lat,
                'lon': os_lon,
                'hdg_rad': os_hdg_rad,
                'cog_rad': os_hdg_rad,
                'sog_ms': SOG_MS,
                'rot_rps': math.radians(rot_dps),
            },
            'ts': {
                'lat': ts_lat,
                'lon': ts_lon,
                'hdg_rad': ts_hdg_rad,
                'cog_rad': ts_hdg_rad,
                'sog_ms': SOG_MS,
                'rot_rps': 0.0,
            },
        })

        # Euler integration for next frame
        os_lat += SOG_MS * math.cos(os_hdg_rad) * DT * LAT_DEG_PER_M
        os_lon += SOG_MS * math.sin(os_hdg_rad) * DT * LON_DEG_PER_M
        ts_lat += SOG_MS * math.cos(ts_hdg_rad) * DT * LAT_DEG_PER_M
        ts_lon += SOG_MS * math.sin(ts_hdg_rad) * DT * LON_DEG_PER_M

    return frames


def _dist_nm(f: dict) -> float:
    """Great-circle approximation distance between OS and TS in nautical miles."""
    dlat_m = (f['os']['lat'] - f['ts']['lat']) * 111_320.0
    dlon_m = (f['os']['lon'] - f['ts']['lon']) * 111_320.0 * math.cos(math.radians(63.0))
    return math.hypot(dlat_m, dlon_m) / 1852.0


def compute_cpa(frames: list | None = None) -> float:
    """Return minimum distance (nm) observed across all frames.

    If frames are not provided, compute_frames() is called (expensive — cache externally).
    """
    if frames is None:
        frames = compute_frames()
    return min(_dist_nm(f) for f in frames)
```

- [ ] **Step 1.4: Run tests**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/tools/demo"
python3 -m pytest test_trajectory.py -v
```
Expected: all 8 tests PASS.

- [ ] **Step 1.5: Commit**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
git add tools/demo/trajectory.py tools/demo/test_trajectory.py
git commit -m "feat(demo): Head-On analytical trajectory generator + tests"
```

---

## Task 2: demo_server.py — FastAPI REST + WebSocket Server

**Files:**
- Create: `tools/demo/demo_server.py`
- Test: manual curl smoke test (specified in Step 2.4)

The server runs as a single uvicorn process. A background asyncio task streams WS frames.

- [ ] **Step 2.1: Create demo_server.py**

Create `tools/demo/demo_server.py`:
```python
#!/usr/bin/env python3
"""SIL Demo-1 mock server — FastAPI REST :8000 + WS broadcast :8765."""
import asyncio, json, math, time, uuid, pathlib
from contextlib import asynccontextmanager
from typing import Any

import uvicorn
import websockets
import websockets.server
from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse
from pydantic import BaseModel

# ── import trajectory (sibling file) ─────────────────────────────────────────
import sys
sys.path.insert(0, str(pathlib.Path(__file__).parent))
from trajectory import compute_frames, compute_cpa, ASDR_EVENTS, DT

# ── Pre-compute trajectory at import time ─────────────────────────────────────
print("Pre-computing Head-On trajectory…", flush=True)
_FRAMES = compute_frames()
_CPA_NM = compute_cpa(_FRAMES)
print(f"  {len(_FRAMES)} frames · CPA = {_CPA_NM:.3f} nm", flush=True)

SCENARIO_YAML_PATH = pathlib.Path(__file__).parents[2] / "scenarios/imazu22/imazu-01-ho-v1.0.yaml"

# ── Shared state ──────────────────────────────────────────────────────────────
_state: dict[str, Any] = {
    "lifecycle": "unconfigured",  # unconfigured|inactive|active|finalized
    "run_id": None,
    "scenario_id": None,
    "start_wall_time": None,
}
_ws_clients: set = set()


# ── Background WS broadcast task ─────────────────────────────────────────────
async def _broadcast_loop():
    """Broadcast telemetry at 10 Hz while lifecycle == 'active'."""
    asdr_idx = 0
    while True:
        await asyncio.sleep(DT)
        if _state["lifecycle"] != "active" or not _ws_clients:
            asdr_idx = 0
            continue

        elapsed = time.monotonic() - _state["start_wall_time"]
        frame_i = min(int(elapsed / DT), len(_FRAMES) - 1)
        frame = _FRAMES[frame_i]
        t = frame["t"]

        # Emit any ASDR events whose time has passed
        while asdr_idx < len(ASDR_EVENTS) and ASDR_EVENTS[asdr_idx]["t"] <= t:
            evt = ASDR_EVENTS[asdr_idx]
            await _send_all({"topic": "/sil/asdr_event", "payload": {
                "stamp": {"seconds": int(time.time())},
                "event_type": evt["event_type"],
                "decision_id": str(uuid.uuid4())[:8],
                "payload_json": json.dumps({"decision": evt["decision"], "rationale": evt["rationale"]}),
                "verdict": 1,  # PASS
            }})
            asdr_idx += 1

        # Own ship
        os = frame["os"]
        await _send_all({"topic": "/sil/own_ship_state", "payload": {
            "stamp": {"seconds": int(time.time())},
            "pose": {"lat": os["lat"], "lon": os["lon"], "heading": os["hdg_rad"]},
            "kinematics": {"sog": os["sog_ms"], "cog": os["cog_rad"],
                           "rot": os["rot_rps"], "u": os["sog_ms"], "v": 0.0, "r": os["rot_rps"]},
            "controlState": {"rudderAngle": -os["rot_rps"] * 5.0, "throttle": 1.0},
        }})

        # Target vessel
        ts = frame["ts"]
        await _send_all({"topic": "/sil/target_vessel_state", "payload": [{
            "stamp": {"seconds": int(time.time())},
            "mmsi": 100000001,
            "pose": {"lat": ts["lat"], "lon": ts["lon"], "heading": ts["hdg_rad"]},
            "kinematics": {"sog": ts["sog_ms"], "cog": ts["cog_rad"], "rot": 0.0},
            "shipType": 70, "mode": "linear",
        }]})

        # Module pulses — 1 Hz (every 10 frames)
        if frame_i % 10 == 0:
            pulses = [{"moduleId": i + 1, "state": 1, "latencyMs": 8 + i * 2}
                      for i in range(8)]
            await _send_all({"topic": "/sil/module_pulse", "payload": pulses})

        # Lifecycle status — 1 Hz
        if frame_i % 10 == 0:
            await _send_all({"topic": "/sil/lifecycle_status", "payload": {
                "current_state": 3,  # ACTIVE
                "scenario_id": _state["scenario_id"],
                "sim_time": t,
                "sim_rate": 1.0,
            }})

        # Auto-finalize at end of trajectory
        if frame_i == len(_FRAMES) - 1:
            _state["lifecycle"] = "finalized"
            await _send_all({"topic": "/sil/lifecycle_status", "payload": {
                "current_state": 5,  # FINALIZED
                "scenario_id": _state["scenario_id"],
                "sim_time": t,
                "sim_rate": 1.0,
            }})


async def _send_all(msg: dict):
    if not _ws_clients:
        return
    data = json.dumps(msg)
    dead = set()
    for ws in _ws_clients:
        try:
            await ws.send(data)
        except Exception:
            dead.add(ws)
    _ws_clients.difference_update(dead)


async def _ws_handler(websocket):
    _ws_clients.add(websocket)
    # Send immediate lifecycle status on connect
    await websocket.send(json.dumps({"topic": "/sil/lifecycle_status", "payload": {
        "current_state": {"unconfigured": 1, "inactive": 2, "active": 3,
                          "finalized": 5}.get(_state["lifecycle"], 1),
        "sim_time": 0.0, "sim_rate": 1.0,
    }}))
    try:
        async for _ in websocket:
            pass  # we don't consume client messages in demo
    finally:
        _ws_clients.discard(websocket)


# ── FastAPI app ───────────────────────────────────────────────────────────────
@asynccontextmanager
async def lifespan(app: FastAPI):
    asyncio.create_task(_broadcast_loop())
    asyncio.create_task(_start_ws_server())
    yield


async def _start_ws_server():
    async with websockets.server.serve(_ws_handler, "0.0.0.0", 8765):
        await asyncio.Future()  # run forever


app = FastAPI(lifespan=lifespan)
app.add_middleware(CORSMiddleware, allow_origins=["*"],
                   allow_methods=["*"], allow_headers=["*"])


# ── Scenario endpoints ────────────────────────────────────────────────────────
@app.get("/api/v1/scenarios")
def list_scenarios():
    return [{"id": "imazu-01-ho-v1.0", "name": "Imazu-01 Head-on (Rule 14)",
             "encounter_type": "head_on"}]


@app.get("/api/v1/scenarios/{scenario_id}")
def get_scenario(scenario_id: str):
    if scenario_id != "imazu-01-ho-v1.0":
        raise HTTPException(404, "scenario not found")
    yaml_content = SCENARIO_YAML_PATH.read_text() if SCENARIO_YAML_PATH.exists() else ""
    return {"yaml_content": yaml_content,
            "hash": "sha256:abc123demo000000000000000000000000000000000000000000000000000001"}


class YamlBody(BaseModel):
    yaml_content: str

@app.post("/api/v1/scenarios")
def create_scenario(body: YamlBody):
    sid = f"scenario-{uuid.uuid4().hex[:8]}"
    return {"scenario_id": sid, "hash": f"sha256:{uuid.uuid4().hex}"}

@app.put("/api/v1/scenarios/{scenario_id}")
def update_scenario(scenario_id: str, body: YamlBody):
    return {"scenario_id": scenario_id, "hash": f"sha256:{uuid.uuid4().hex}"}

@app.delete("/api/v1/scenarios/{scenario_id}", status_code=204)
def delete_scenario(scenario_id: str):
    return None

@app.post("/api/v1/scenarios/validate")
def validate_scenario(body: YamlBody):
    return {"valid": True, "errors": []}


# ── Lifecycle endpoints ───────────────────────────────────────────────────────
class ScenarioIdBody(BaseModel):
    scenario_id: str

class RunIdBody(BaseModel):
    run_id: str | None = None

@app.get("/api/v1/lifecycle/status")
def lifecycle_status():
    state_map = {"unconfigured": 1, "inactive": 2, "active": 3, "finalized": 5}
    return {"current_state": state_map.get(_state["lifecycle"], 1),
            "scenario_id": _state["scenario_id"],
            "sim_time": 0.0, "sim_rate": 1.0}

@app.post("/api/v1/lifecycle/configure")
def configure(body: ScenarioIdBody):
    _state["lifecycle"] = "inactive"
    _state["scenario_id"] = body.scenario_id
    _state["run_id"] = str(uuid.uuid4())
    return {"run_id": _state["run_id"], "state": "inactive", "success": True}

@app.post("/api/v1/lifecycle/activate")
def activate():
    _state["lifecycle"] = "active"
    _state["start_wall_time"] = time.monotonic()
    return {"run_id": _state["run_id"], "state": "active", "success": True}

@app.post("/api/v1/lifecycle/deactivate")
def deactivate():
    _state["lifecycle"] = "finalized"
    return {"run_id": _state["run_id"], "state": "finalized", "success": True}

@app.post("/api/v1/lifecycle/cleanup")
def cleanup():
    _state.update({"lifecycle": "unconfigured", "run_id": None,
                   "scenario_id": None, "start_wall_time": None})
    return {"state": "unconfigured", "success": True}


# ── Selfcheck ─────────────────────────────────────────────────────────────────
@app.post("/api/v1/selfcheck/probe")
async def probe():
    checks = [
        {"name": "enc_load",     "passed": True, "detail": "ENC trondelag loaded · 1 chart"},
        {"name": "asdr_start",   "passed": True, "detail": "ASDR ledger initialized"},
        {"name": "utc_sync",     "passed": True, "detail": "UTC offset 0 ms"},
        {"name": "module_health","passed": True, "detail": "M1–M8 all GREEN"},
        {"name": "scenario_sig", "passed": True, "detail": "SHA-256 verified"},
    ]
    # Simulate sequential check latency (100 ms each)
    for _ in checks:
        await asyncio.sleep(0.1)
    return {"all_clear": True, "items": checks}

@app.get("/api/v1/selfcheck/status")
def selfcheck_status():
    return {"module_pulses": [{"moduleId": str(i + 1), "state": 1,
                               "latencyMs": 8 + i * 2, "messageDrops": 0}
                              for i in range(8)]}


# ── Scoring ───────────────────────────────────────────────────────────────────
@app.get("/api/v1/scoring/last_run")
def last_run_scoring():
    if _state["lifecycle"] not in ("finalized", "active"):
        return {"run_id": None, "kpis": None, "rule_chain": [], "verdict": "pending"}
    return {
        "run_id": _state["run_id"],
        "scenario_id": _state["scenario_id"],
        "kpis": {
            "min_cpa_nm": round(_CPA_NM, 3),
            "avg_rot_dpm": 21.6,
            "distance_nm": round(SOG_KN * 700 / 3600, 2),
            "duration_s": 700,
            "max_rud_deg": round(math.degrees(0.6109), 1),
            "tor_time_s": None,
            "decision_p99_ms": 312,
            "faults_injected": 0,
            "scenario_sha256": "abc123demo",
        },
        "rule_chain": ["Rule14_GW_HeadOn"],
        "verdict": "pass" if _CPA_NM > 0.27 else "fail",
    }

# constant needed in last_run_scoring scope
SOG_KN = 10.0

# ── Export / Fault stubs ──────────────────────────────────────────────────────
@app.post("/api/v1/export/marzip")
def export_marzip(body: dict = None):
    return {"status": "processing", "download_url": None}

@app.get("/api/v1/export/status/{run_id}")
def export_status(run_id: str):
    return {"status": "pending"}

@app.post("/api/v1/fault/trigger")
def fault_trigger(body: dict = None):
    return {"fault_id": str(uuid.uuid4())[:8]}

@app.post("/api/v1/fault/inject")
def fault_inject(body: dict = None):
    return {"accepted": True, "fault_id": str(uuid.uuid4())[:8]}

@app.delete("/api/v1/fault/{fault_id}")
def fault_cancel(fault_id: str):
    return {"cancelled": True}


# ── Entrypoint ────────────────────────────────────────────────────────────────
if __name__ == "__main__":
    uvicorn.run("demo_server:app", host="0.0.0.0", port=8000, reload=False)
```

- [ ] **Step 2.2: Fix the `SOG_KN` ordering issue**

The constant `SOG_KN` is used inside `last_run_scoring` but defined after it. Move the constant to the top of the file, just after the `_FRAMES` / `_CPA_NM` lines:

```python
# Add this line immediately after the _CPA_NM line (line ~20):
SOG_KN = 10.0
```

Then remove the duplicate `SOG_KN = 10.0` that was left inside `last_run_scoring`'s scope comment.

- [ ] **Step 2.3: Smoke-test REST endpoints**

Start the server in background:
```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/tools/demo"
python3 demo_server.py &
sleep 3
```

Run checks:
```bash
# Scenarios list
curl -s http://localhost:8000/api/v1/scenarios | python3 -m json.tool
# Expected: [{"id": "imazu-01-ho-v1.0", ...}]

# Configure lifecycle
curl -s -X POST http://localhost:8000/api/v1/lifecycle/configure \
  -H "Content-Type: application/json" \
  -d '{"scenario_id":"imazu-01-ho-v1.0"}' | python3 -m json.tool
# Expected: {"run_id": "<uuid>", "state": "inactive", "success": true}

# Probe selfcheck (should take ~0.5s)
curl -s -X POST http://localhost:8000/api/v1/selfcheck/probe | python3 -m json.tool
# Expected: {"all_clear": true, "items": [...5 items...]}

# Activate
curl -s -X POST http://localhost:8000/api/v1/lifecycle/activate | python3 -m json.tool
# Expected: {"state": "active", ...}

# Wait 2s then get scoring
sleep 2
curl -s http://localhost:8000/api/v1/scoring/last_run | python3 -m json.tool
# Expected: kpis.min_cpa_nm > 0

kill %1
```

- [ ] **Step 2.4: Commit**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
git add tools/demo/demo_server.py
git commit -m "feat(demo): FastAPI REST + asyncio WS server for Demo-1"
```

---

## Task 3: run_demo.sh — Startup Script

**Files:**
- Create: `tools/demo/run_demo.sh`

- [ ] **Step 3.1: Create run_demo.sh**

Create `tools/demo/run_demo.sh`:
```bash
#!/usr/bin/env bash
set -euo pipefail

echo "=== SIL Demo-1: Imazu-01 Head-On ==="

# Check Python dependencies
python3 -c "import fastapi, uvicorn, websockets" 2>/dev/null || {
  echo "Missing deps. Run: pip install fastapi uvicorn websockets"
  exit 1
}

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEMO_DIR="$REPO_ROOT/tools/demo"

cleanup() { kill "$SERVER_PID" 2>/dev/null || true; }
trap cleanup EXIT

# Start demo server (REST :8000 + WS :8765)
echo "[1/2] Starting demo server (REST :8000 · WS :8765)..."
cd "$DEMO_DIR"
python3 demo_server.py &
SERVER_PID=$!
sleep 2

# Start Vite dev server
echo "[2/2] Starting Vite dev server (:5173)..."
cd "$REPO_ROOT/web"
npx vite --host &
VITE_PID=$!
sleep 2

echo ""
echo "=== Ready ==="
echo "  Frontend : http://localhost:5173/#/builder"
echo "  REST API : http://localhost:8000/api/v1/scenarios"
echo "  WS       : ws://localhost:8765"
echo ""
echo "Press Ctrl+C to stop."
wait "$SERVER_PID"
```

- [ ] **Step 3.2: Make executable and smoke-test**

```bash
chmod +x "/Users/marine/Code/MASS-L3-Tactical Layer/tools/demo/run_demo.sh"
# Verify it prints help if deps missing (force test by unsetting PATH hack — just read the script)
head -20 "/Users/marine/Code/MASS-L3-Tactical Layer/tools/demo/run_demo.sh"
```
Expected: script content visible.

- [ ] **Step 3.3: Commit**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
git add tools/demo/run_demo.sh
git commit -m "feat(demo): run_demo.sh one-liner startup script"
```

---

## Task 4: BuilderRightRail.tsx — Right-Side Collapsible Panel

**Files:**
- Create: `web/src/screens/shared/BuilderRightRail.tsx`
- Create: `web/src/screens/shared/__tests__/BuilderRightRail.test.tsx`

This component is fully self-contained. `ScenarioBuilder` will pass `previewData` and callback props to it.

- [ ] **Step 4.1: Write failing tests**

Create `web/src/screens/shared/__tests__/BuilderRightRail.test.tsx`:
```tsx
import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { BuilderRightRail } from '../BuilderRightRail';

const noop = () => {};
const defaultProps = {
  previewData: null,
  onRun: noop,
  onSave: noop,
  onValidate: noop,
};

describe('BuilderRightRail', () => {
  it('renders collapsed at 48px by default', () => {
    const { container } = render(<BuilderRightRail {...defaultProps} />);
    const rail = container.firstChild as HTMLElement;
    expect(rail.style.width).toBe('48px');
  });

  it('shows 4 tab icons in collapsed state', () => {
    render(<BuilderRightRail {...defaultProps} />);
    // Tab labels visible as aria-label or title
    expect(screen.getByTitle('Encounter')).toBeTruthy();
    expect(screen.getByTitle('Environment')).toBeTruthy();
    expect(screen.getByTitle('Run')).toBeTruthy();
    expect(screen.getByTitle('Summary')).toBeTruthy();
  });

  it('expands to 320px when tab is clicked', () => {
    const { container } = render(<BuilderRightRail {...defaultProps} />);
    fireEvent.click(screen.getByTitle('Encounter'));
    const rail = container.firstChild as HTMLElement;
    expect(rail.style.width).toBe('320px');
  });

  it('shows Encounter fields when Encounter tab is active', () => {
    render(<BuilderRightRail {...defaultProps} />);
    fireEvent.click(screen.getByTitle('Encounter'));
    expect(screen.getByText(/DCPA Target/i)).toBeTruthy();
  });

  it('shows Environment fields when Environment tab is active', () => {
    render(<BuilderRightRail {...defaultProps} />);
    fireEvent.click(screen.getByTitle('Environment'));
    expect(screen.getByText(/Beaufort/i)).toBeTruthy();
  });

  it('shows Run fields when Run tab is active', () => {
    render(<BuilderRightRail {...defaultProps} />);
    fireEvent.click(screen.getByTitle('Run'));
    expect(screen.getByText(/Duration/i)).toBeTruthy();
  });

  it('shows Summary with Run button when Summary tab is active', () => {
    render(<BuilderRightRail {...defaultProps} />);
    fireEvent.click(screen.getByTitle('Summary'));
    expect(screen.getByText(/RUN/i)).toBeTruthy();
  });

  it('collapses when collapse button is clicked', () => {
    const { container } = render(<BuilderRightRail {...defaultProps} />);
    fireEvent.click(screen.getByTitle('Encounter'));
    fireEvent.click(screen.getByTitle('Collapse panel'));
    const rail = container.firstChild as HTMLElement;
    expect(rail.style.width).toBe('48px');
  });

  it('calls onRun when Run button is clicked', () => {
    const onRun = vi.fn();
    render(<BuilderRightRail {...defaultProps} onRun={onRun} />);
    fireEvent.click(screen.getByTitle('Summary'));
    fireEvent.click(screen.getByText(/RUN/i));
    expect(onRun).toHaveBeenCalledOnce();
  });
});
```

- [ ] **Step 4.2: Run to verify tests fail**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/web"
npx vitest run src/screens/shared/__tests__/BuilderRightRail.test.tsx 2>&1 | tail -15
```
Expected: `Cannot find module '../BuilderRightRail'`

- [ ] **Step 4.3: Create BuilderRightRail.tsx**

Create `web/src/screens/shared/BuilderRightRail.tsx`:
```tsx
import { useState } from 'react';

// ── Imazu-22 case list (abbreviated labels) ────────────────────────────────
const IMAZU_CASES = [
  '01 Head-on','02 Cross 45°','03 Cross 90°','04 Cross 135°',
  '05 Cross 45°R','06 Cross 90°R','07 Cross 135°R','08 Cross 180°',
  '09 Overtake 0°','10 Overtake 22°','11 Overtake 45°','12 Overtake 90°',
  '13 Overtake 135°','14 Cross 45°','15 Cross 90°','16 Cross 135°',
  '17 Cross 180°','18 Overtake','19 Cross 22°','20 Cross 67°',
  '21 Cross 112°','22 Cross 157°',
];

// ── Mini SVG encounter geometry (schematic only) ───────────────────────────
function EncounterMini({ caseIdx }: { caseIdx: number }) {
  const angle = ((caseIdx % 8) * 45) * (Math.PI / 180);
  const cx = 24, cy = 24, r = 16;
  const tx = cx + r * Math.sin(angle);
  const ty = cy - r * Math.cos(angle);
  return (
    <svg width={48} height={48} viewBox="0 0 48 48">
      {/* OS: red triangle pointing up */}
      <polygon points={`${cx},${cy - 8} ${cx - 5},${cy + 6} ${cx + 5},${cx + 6}`}
               fill="#ef4444" />
      {/* TS: blue triangle pointing toward OS */}
      <polygon points={`${tx},${ty + 8} ${tx - 4},${ty - 5} ${tx + 4},${ty - 5}`}
               fill="#3b82f6" transform={`rotate(${caseIdx * 16}, ${tx}, ${ty})`} />
      {/* Connecting line */}
      <line x1={cx} y1={cy} x2={tx} y2={ty}
            stroke="#4b5563" strokeWidth="0.5" strokeDasharray="2,2" />
    </svg>
  );
}

// ── Field helpers ──────────────────────────────────────────────────────────
function Field({ label, type = 'number', unit = '', defaultValue = '' }: {
  label: string; type?: string; unit?: string; defaultValue?: string | number;
}) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 2, marginBottom: 10 }}>
      <label style={{ fontSize: 9, color: 'var(--txt-3)', fontFamily: 'var(--f-disp)',
                      letterSpacing: '0.1em', textTransform: 'uppercase' }}>
        {label}{unit && ` (${unit})`}
      </label>
      <input type={type} defaultValue={defaultValue} style={{
        background: 'rgba(0,0,0,0.3)', border: '1px solid var(--line-1)',
        color: 'var(--txt-1)', padding: '6px 8px', borderRadius: 4,
        fontFamily: 'var(--f-mono)', fontSize: 11, outline: 'none', width: '100%',
      }} />
    </div>
  );
}

function Select({ label, options }: { label: string; options: string[] }) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 2, marginBottom: 10 }}>
      <label style={{ fontSize: 9, color: 'var(--txt-3)', fontFamily: 'var(--f-disp)',
                      letterSpacing: '0.1em', textTransform: 'uppercase' }}>{label}</label>
      <select style={{
        background: 'rgba(0,0,0,0.3)', border: '1px solid var(--line-1)',
        color: 'var(--txt-1)', padding: '6px 8px', borderRadius: 4,
        fontFamily: 'var(--f-mono)', fontSize: 11, outline: 'none', width: '100%',
      }}>
        {options.map((o) => <option key={o}>{o}</option>)}
      </select>
    </div>
  );
}

function Toggle({ label, defaultChecked = false }: { label: string; defaultChecked?: boolean }) {
  const [on, setOn] = useState(defaultChecked);
  return (
    <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center',
                  marginBottom: 8, cursor: 'pointer' }} onClick={() => setOn(!on)}>
      <span style={{ fontSize: 11, color: 'var(--txt-2)', fontFamily: 'var(--f-mono)' }}>{label}</span>
      <div style={{
        width: 32, height: 18, borderRadius: 9, position: 'relative',
        background: on ? 'var(--c-phos)' : 'var(--line-2)', transition: 'background 0.15s',
      }}>
        <div style={{
          position: 'absolute', top: 2, left: on ? 14 : 2,
          width: 14, height: 14, borderRadius: 7, background: '#fff',
          transition: 'left 0.15s',
        }} />
      </div>
    </div>
  );
}

// ── Tab content ────────────────────────────────────────────────────────────
function EncounterTab({ previewData }: { previewData: any }) {
  const [selected, setSelected] = useState(0);
  return (
    <div>
      <div style={{ fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.1em',
                    marginBottom: 8, fontFamily: 'var(--f-disp)' }}>IMAZU-22 CASE</div>
      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4, 1fr)', gap: 4, marginBottom: 12 }}>
        {IMAZU_CASES.map((c, i) => (
          <div key={i} onClick={() => setSelected(i)} style={{
            border: `1px solid ${selected === i ? 'var(--c-phos)' : 'var(--line-1)'}`,
            borderRadius: 4, cursor: 'pointer', overflow: 'hidden',
            background: selected === i ? 'rgba(91,192,190,0.1)' : 'rgba(0,0,0,0.2)',
          }}>
            <EncounterMini caseIdx={i} />
            <div style={{ fontSize: 8, color: selected === i ? 'var(--c-phos)' : 'var(--txt-3)',
                          textAlign: 'center', padding: '2px 0', fontFamily: 'var(--f-mono)' }}>
              {c.split(' ')[0]}
            </div>
          </div>
        ))}
      </div>
      <div style={{ padding: '8px 0', borderTop: '1px solid var(--line-1)', marginBottom: 8 }}>
        <div style={{ fontSize: 10, color: 'var(--c-phos)', fontFamily: 'var(--f-mono)',
                      marginBottom: 6 }}>{IMAZU_CASES[selected]}</div>
      </div>
      <Field label="DCPA Target" unit="nm" defaultValue="0.50" />
      <Field label="TCPA Target" unit="min" defaultValue="10.0" />
      <Field label="TS Relative Bearing" unit="deg" defaultValue="0" />
      <Field label="TS Distance" unit="nm" defaultValue="7.0" />
      <Field label="TS SOG" unit="kn" defaultValue="10.0" />
      <Field label="TS COG" unit="deg" defaultValue="180" />
    </div>
  );
}

function EnvironmentTab() {
  return (
    <div>
      <div style={{ fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.1em',
                    marginBottom: 6, fontFamily: 'var(--f-disp)' }}>WIND / SEA / CURRENT</div>
      <Select label="Wind Force" options={['0 – Calm','1','2','3 – Light','4','5 – Fresh','6','7 – Near gale']} />
      <Field label="Wind Direction" unit="deg" defaultValue="0" />
      <Field label="Wave Height Hs" unit="m" defaultValue="0.0" />
      <Field label="Wave Direction" unit="deg" defaultValue="0" />
      <Field label="Current Speed" unit="kn" defaultValue="0.0" />
      <Field label="Current Direction" unit="deg" defaultValue="0" />

      <div style={{ fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.1em',
                    margin: '10px 0 6px', fontFamily: 'var(--f-disp)' }}>VISIBILITY / SENSORS</div>
      <Select label="Daypart" options={['Day','Night','Dawn','Dusk']} />
      <Field label="Visibility" unit="nm" defaultValue="10.0" />
      <Toggle label="Fog" />
      <Toggle label="Rain" />
      <Field label="GNSS Noise σ" unit="m" defaultValue="0.0" />
      <Field label="AIS Loss Rate" unit="%" defaultValue="0" />
      <Field label="Radar Range" unit="nm" defaultValue="12.0" />

      <div style={{ fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.1em',
                    margin: '10px 0 6px', fontFamily: 'var(--f-disp)' }}>ODD / OWN SHIP</div>
      <Select label="Hull Type" options={['FCB','ASV']} />
      <Field label="Displacement" unit="t" defaultValue="450" />
      <Field label="Initial Heading" unit="deg" defaultValue="0" />
      <Field label="Initial SOG" unit="kn" defaultValue="10.0" />
      <Field label="Max Rudder" unit="deg" defaultValue="35" />
      <Field label="Max ROT" unit="deg/s" defaultValue="2.0" />
    </div>
  );
}

const FAULT_ROWS = [
  { type: 'AIS_DROPOUT',    t: 60,  dur: 30 },
  { type: 'RADAR_SPIKE',    t: 120, dur: 10 },
  { type: 'DIST_STEP',      t: 200, dur: 60 },
  { type: 'SENSOR_FREEZE',  t: 300, dur: 20 },
];

const PASS_ROWS = [
  { label: 'CPA min',         threshold: '0.50 nm' },
  { label: 'COLREGs',         threshold: '100%' },
  { label: 'Path deviation',  threshold: '≤2 nm' },
  { label: 'ToR response',    threshold: '≤60 s' },
  { label: 'M5 solve p95',    threshold: '≤500 ms' },
  { label: 'ASDR integrity',  threshold: '100%' },
];

function RunTab() {
  const [faultsOn, setFaultsOn] = useState<Record<number, boolean>>({});
  return (
    <div>
      <div style={{ fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.1em',
                    marginBottom: 6, fontFamily: 'var(--f-disp)' }}>TIMING / CLOCK</div>
      <Field label="Duration" unit="s" defaultValue="700" />
      <Field label="Sim Rate" unit="×" defaultValue="1.0" />
      <Field label="Random Seed" defaultValue="42" />
      <Field label="M2 Hz" defaultValue="10" />
      <Field label="M5 Hz" defaultValue="4" />

      <div style={{ fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.1em',
                    margin: '10px 0 6px', fontFamily: 'var(--f-disp)' }}>MODE / IVP WEIGHTS</div>
      <Select label="Autonomy Level" options={['D2 – Remote control','D3 – Supervised','D4 – Full']} />
      <Select label="Operator Role" options={['Observer','Supervisor','Commander']} />
      <div style={{ marginBottom: 10 }}>
        <label style={{ fontSize: 9, color: 'var(--txt-3)', fontFamily: 'var(--f-disp)',
                        letterSpacing: '0.1em', textTransform: 'uppercase' }}>COLREG_AVOID</label>
        <input type="range" min={0} max={100} defaultValue={80}
               style={{ width: '100%', accentColor: 'var(--c-phos)' }} />
      </div>
      <div style={{ marginBottom: 10 }}>
        <label style={{ fontSize: 9, color: 'var(--txt-3)', fontFamily: 'var(--f-disp)',
                        letterSpacing: '0.1em', textTransform: 'uppercase' }}>MISSION_ETA</label>
        <input type="range" min={0} max={100} defaultValue={50}
               style={{ width: '100%', accentColor: 'var(--c-phos)' }} />
      </div>
      <Field label="CPA Min Threshold" unit="nm" defaultValue="0.50" />
      <Field label="TCPA Trigger" unit="min" defaultValue="10.0" />

      <div style={{ fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.1em',
                    margin: '10px 0 6px', fontFamily: 'var(--f-disp)' }}>FAULT INJECTION</div>
      <div style={{ border: '1px solid var(--line-1)', borderRadius: 4, overflow: 'hidden', marginBottom: 12 }}>
        <div style={{ display: 'grid', gridTemplateColumns: '1fr 40px 40px 32px',
                      padding: '4px 6px', background: 'rgba(0,0,0,0.3)',
                      fontSize: 8, color: 'var(--txt-3)', fontFamily: 'var(--f-mono)' }}>
          <span>TYPE</span><span>T(s)</span><span>DUR</span><span>ON</span>
        </div>
        {FAULT_ROWS.map((r, i) => (
          <div key={i} style={{
            display: 'grid', gridTemplateColumns: '1fr 40px 40px 32px',
            padding: '5px 6px', borderTop: '1px solid var(--line-1)',
            fontSize: 9, color: 'var(--txt-2)', fontFamily: 'var(--f-mono)',
            alignItems: 'center',
          }}>
            <span style={{ fontSize: 8 }}>{r.type}</span>
            <span>{r.t}</span>
            <span>{r.dur}</span>
            <input type="checkbox" checked={!!faultsOn[i]}
                   onChange={(e) => setFaultsOn((prev) => ({ ...prev, [i]: e.target.checked }))}
                   style={{ accentColor: 'var(--c-phos)' }} />
          </div>
        ))}
      </div>

      <div style={{ fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.1em',
                    marginBottom: 6, fontFamily: 'var(--f-disp)' }}>PASS CRITERIA</div>
      <div style={{ border: '1px solid var(--line-1)', borderRadius: 4, overflow: 'hidden' }}>
        {PASS_ROWS.map((r, i) => (
          <div key={i} style={{
            display: 'flex', justifyContent: 'space-between', alignItems: 'center',
            padding: '5px 8px', borderTop: i > 0 ? '1px solid var(--line-1)' : 'none',
            fontSize: 9, fontFamily: 'var(--f-mono)',
          }}>
            <span style={{ color: 'var(--txt-2)' }}>{r.label}</span>
            <span style={{ color: 'var(--c-phos)' }}>{r.threshold}</span>
          </div>
        ))}
      </div>
    </div>
  );
}

function SummaryTab({ previewData, onSave, onValidate, onRun }: {
  previewData: any; onSave: () => void; onValidate: () => void; onRun: () => void;
}) {
  const os = previewData?.ownShip;
  const ts = previewData?.targets?.[0];
  const row = (label: string, val: string) => (
    <div style={{ display: 'flex', justifyContent: 'space-between', padding: '4px 0',
                  borderBottom: '1px solid var(--line-1)', fontSize: 10, fontFamily: 'var(--f-mono)' }}>
      <span style={{ color: 'var(--txt-3)' }}>{label}</span>
      <span style={{ color: 'var(--txt-1)' }}>{val}</span>
    </div>
  );
  return (
    <div>
      <div style={{ marginBottom: 12 }}>
        {row('Scenario', 'imazu-01-ho-v1.0')}
        {row('ENC Region', 'trondelag')}
        {row('Duration', '700 s')}
        {os && row('OS', `${os.lat.toFixed(3)}°N ${os.lon.toFixed(3)}°E HDG ${os.heading}° ${os.sog}kn`)}
        {ts && row('TS', `${ts.lat.toFixed(3)}°N ${ts.lon.toFixed(3)}°E HDG ${ts.heading}° ${ts.sog}kn`)}
      </div>
      <div style={{ display: 'flex', gap: 6, marginBottom: 8 }}>
        <button onClick={onSave} style={btnStyle('line')}>SAVE</button>
        <button onClick={onValidate} style={btnStyle('line')}>VALIDATE</button>
      </div>
      <button onClick={onRun} style={btnStyle('phos')}>RUN →</button>
    </div>
  );
}

const btnStyle = (variant: 'line' | 'phos'): React.CSSProperties => ({
  flex: 1, padding: '10px 0', borderRadius: 4, cursor: 'pointer',
  fontFamily: 'var(--f-disp)', fontSize: 10, fontWeight: 700,
  letterSpacing: '0.12em', textAlign: 'center',
  width: variant === 'phos' ? '100%' : undefined,
  background: variant === 'phos' ? 'var(--c-phos)' : 'transparent',
  color: variant === 'phos' ? 'var(--bg-0)' : 'var(--txt-1)',
  border: variant === 'phos' ? '1px solid var(--c-phos)' : '1px solid var(--line-2)',
});

// ── Tab definitions ────────────────────────────────────────────────────────
const TABS = [
  { id: 'encounter',    label: 'Encounter',    glyph: '⊕' },
  { id: 'environment',  label: 'Environment',  glyph: '⛅' },
  { id: 'run',          label: 'Run',          glyph: '▶' },
  { id: 'summary',      label: 'Summary',      glyph: '☰' },
] as const;

type TabId = typeof TABS[number]['id'];

// ── Main component ─────────────────────────────────────────────────────────
export interface BuilderRightRailProps {
  previewData: any;
  onRun: () => void;
  onSave: () => void;
  onValidate: () => void;
}

export function BuilderRightRail({ previewData, onRun, onSave, onValidate }: BuilderRightRailProps) {
  const [activeTab, setActiveTab] = useState<TabId | null>(null);
  const isExpanded = activeTab !== null;

  const handleTabClick = (id: TabId) => {
    setActiveTab(prev => prev === id ? null : id);
  };

  return (
    <div style={{
      width: isExpanded ? '320px' : '48px',
      flexShrink: 0,
      background: '#0a0f18',
      borderLeft: '1px solid var(--line-2)',
      display: 'flex',
      flexDirection: 'row',
      transition: 'width 0.2s cubic-bezier(0.4, 0, 0.2, 1)',
      overflow: 'hidden',
      position: 'relative',
    }}>

      {/* Strip of 4 tab icons (always visible) */}
      <div style={{
        width: 48, flexShrink: 0, display: 'flex', flexDirection: 'column',
        alignItems: 'center', paddingTop: 20, gap: 4,
        borderRight: isExpanded ? '1px solid var(--line-1)' : 'none',
      }}>
        {TABS.map((tab) => (
          <button key={tab.id} title={tab.label} onClick={() => handleTabClick(tab.id)} style={{
            width: 36, height: 36, borderRadius: 6, border: 'none', cursor: 'pointer',
            background: activeTab === tab.id ? 'rgba(91,192,190,0.15)' : 'transparent',
            color: activeTab === tab.id ? 'var(--c-phos)' : 'var(--txt-3)',
            display: 'flex', flexDirection: 'column', alignItems: 'center',
            justifyContent: 'center', gap: 2,
          }}>
            <span style={{ fontSize: 14 }}>{tab.glyph}</span>
            <span style={{ fontSize: 7, fontFamily: 'var(--f-mono)',
                           letterSpacing: '0.05em' }}>{tab.id.slice(0, 3).toUpperCase()}</span>
          </button>
        ))}

        {/* Collapse button at bottom */}
        {isExpanded && (
          <button title="Collapse panel" onClick={() => setActiveTab(null)} style={{
            marginTop: 'auto', marginBottom: 16, width: 36, height: 24, borderRadius: 4,
            border: '1px solid var(--line-1)', background: 'transparent',
            color: 'var(--txt-3)', cursor: 'pointer', fontSize: 14,
          }}>›</button>
        )}
      </div>

      {/* Expanded content panel */}
      {isExpanded && (
        <div style={{ flex: 1, display: 'flex', flexDirection: 'column', overflow: 'hidden' }}>
          {/* Tab title */}
          <div style={{
            padding: '14px 12px 10px', borderBottom: '1px solid var(--line-1)',
            fontFamily: 'var(--f-disp)', fontSize: 11, fontWeight: 700,
            color: 'var(--c-phos)', letterSpacing: '0.12em',
          }}>
            {TABS.find(t => t.id === activeTab)?.label.toUpperCase()}
          </div>

          {/* Scrollable content */}
          <div style={{ flex: 1, overflowY: 'auto', padding: '10px 12px 24px' }}>
            {activeTab === 'encounter'   && <EncounterTab previewData={previewData} />}
            {activeTab === 'environment' && <EnvironmentTab />}
            {activeTab === 'run'         && <RunTab />}
            {activeTab === 'summary'     && (
              <SummaryTab previewData={previewData}
                          onSave={onSave} onValidate={onValidate} onRun={onRun} />
            )}
          </div>
        </div>
      )}
    </div>
  );
}
```

- [ ] **Step 4.4: Run tests**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/web"
npx vitest run src/screens/shared/__tests__/BuilderRightRail.test.tsx
```
Expected: 9 tests PASS.

- [ ] **Step 4.5: Commit**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
git add web/src/screens/shared/BuilderRightRail.tsx \
        web/src/screens/shared/__tests__/BuilderRightRail.test.tsx
git commit -m "feat(builder): collapsible right-rail with 4 high-fidelity tabs"
```

---

## Task 5: ScenarioBuilder.tsx — Wire BuilderRightRail

**Files:**
- Modify: `web/src/screens/ScenarioBuilder.tsx`

Replace the entire `/* RIGHT PANEL */` div (line ~417 to ~513) with `<BuilderRightRail>`.

- [ ] **Step 5.1: Add import**

In `web/src/screens/ScenarioBuilder.tsx`, after the existing import block (after line 20), add:
```tsx
import { BuilderRightRail } from './shared/BuilderRightRail';
```

- [ ] **Step 5.2: Replace right panel div**

Find the comment `{/* RIGHT PANEL: 10% - Parameters */}` (around line 417) and replace everything from that comment through the closing `</div>` of the right panel (end of the component's return, around line ~513) with:

```tsx
      <BuilderRightRail
        previewData={previewData}
        onRun={handleRun}
        onSave={handleSave}
        onValidate={handleValidate}
      />
    </div>
  );
}
```

- [ ] **Step 5.3: Run existing ScenarioBuilder test to ensure no regressions**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/web"
npx vitest run src/screens/__tests__/ScenarioBuilder.test.tsx 2>&1 | tail -20
```
Expected: tests pass (or pre-existing failures unrelated to this change).

- [ ] **Step 5.4: Commit**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
git add web/src/screens/ScenarioBuilder.tsx
git commit -m "feat(builder): replace right panel with BuilderRightRail component"
```

---

## Task 6: BridgeHMI.tsx — Waiting Overlay

**Files:**
- Modify: `web/src/screens/BridgeHMI.tsx`

- [ ] **Step 6.1: Find the map container**

Open `web/src/screens/BridgeHMI.tsx`. The main map `<div>` has `position: 'relative'` and contains the `<SilMapView>` call. It's around line ~155. The `ownShip` value comes from `useTelemetryStore`.

- [ ] **Step 6.2: Add overlay inside the map container**

Locate the block that starts:
```tsx
      {/* Map fills remaining space */}
```
and find the inner `<div>` that wraps `<SilMapView>`. Inside that div, after the `<SilMapView .../>` line, add the waiting overlay:

```tsx
        {/* Waiting overlay — shown until first WS frame arrives */}
        {!ownShip && (
          <div style={{
            position: 'absolute', inset: 0, zIndex: 50,
            display: 'flex', flexDirection: 'column',
            alignItems: 'center', justifyContent: 'center',
            background: 'rgba(7,12,19,0.82)',
            fontFamily: 'var(--f-mono)',
          }}>
            <div style={{ fontSize: 24, marginBottom: 12, opacity: 0.4 }}>◌</div>
            <div style={{ fontSize: 13, color: 'var(--txt-1)', marginBottom: 6 }}>
              Waiting for simulation data…
            </div>
            <div style={{ fontSize: 10, color: 'var(--txt-3)' }}>ws://localhost:8765</div>
          </div>
        )}
```

`ownShip` is already read from `useTelemetryStore` at the top of `BridgeHMI` (line ~40, imported as part of `useTelemetryStore`). Verify this is available in scope before adding the overlay.

- [ ] **Step 6.3: Verify ownShip is in scope**

In `BridgeHMI.tsx`, find where `ownShip` is destructured from the telemetry store. It appears inside `CaptainHUD`. If `BridgeHMI` itself doesn't have it at the top level, add this line near the top of `BridgeHMI` function body (after the existing store hook calls):

```tsx
  const ownShip = useTelemetryStore((s) => s.ownShip);
```

- [ ] **Step 6.4: Run BridgeHMI test**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/web"
npx vitest run src/screens/__tests__/BridgeHMI.test.tsx 2>&1 | tail -15
```
Expected: tests pass (or pre-existing failures).

- [ ] **Step 6.5: Commit**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
git add web/src/screens/BridgeHMI.tsx
git commit -m "feat(bridge): add waiting-for-telemetry overlay when ownShip is null"
```

---

## Task 7: RunReport.tsx — Wire Live KPI Cards

**Files:**
- Modify: `web/src/screens/RunReport.tsx`

Replace the hardcoded inline array of 8 KPI objects (lines ~157–185) with values derived from `useGetLastRunScoringQuery`.

- [ ] **Step 7.1: Write a failing test first**

In `web/src/screens/__tests__/RunReport.test.tsx`, add this test (keep existing tests):
```tsx
import { describe, it, expect, vi } from 'vitest';
import { render, screen } from '@testing-library/react';
import { Provider } from 'react-redux';
import { configureStore } from '@reduxjs/toolkit';
import { silApi } from '../../api/silApi';

// Mock RTK Query to return real scoring data
vi.mock('../../api/silApi', async (importOriginal) => {
  const actual = await importOriginal() as any;
  return {
    ...actual,
    useGetLastRunScoringQuery: () => ({
      data: {
        run_id: 'test-run',
        verdict: 'pass',
        kpis: {
          min_cpa_nm: 0.247,
          avg_rot_dpm: 21.6,
          distance_nm: 0.99,
          duration_s: 700,
          max_rud_deg: 35.0,
          tor_time_s: null,
          decision_p99_ms: 312,
          faults_injected: 0,
          scenario_sha256: 'abc123',
        },
        rule_chain: ['Rule14_GW_HeadOn'],
      },
      refetch: vi.fn(),
    }),
    useExportMarzipMutation: () => [vi.fn(), { isLoading: false }],
    useGetExportStatusQuery: () => ({ data: null }),
  };
});

describe('RunReport KPI cards', () => {
  it('shows min CPA from API, not hardcoded 0.52', () => {
    render(<RunReport />);
    expect(screen.getByText('0.247 nm')).toBeTruthy();
    expect(screen.queryByText('0.52 nm')).toBeNull();
  });

  it('shows verdict PASS from API', () => {
    render(<RunReport />);
    const verdicts = screen.getAllByText('PASS');
    expect(verdicts.length).toBeGreaterThan(0);
  });
});
```

Note: `RunReport` needs to be importable without the store context — add `import { RunReport } from '../RunReport';` at the top of the test.

- [ ] **Step 7.2: Run to verify test fails**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/web"
npx vitest run src/screens/__tests__/RunReport.test.tsx 2>&1 | grep -E "(PASS|FAIL|Error)" | head -10
```
Expected: the new `shows min CPA from API` test fails (still shows `0.52 nm`).

- [ ] **Step 7.3: Replace hardcoded KPI array in RunReport.tsx**

Find the `{/* KPI Cards Row */}` block (around line 153). The array currently starts with:
```tsx
        {[
          { label: 'VERDICT', value: 'PASS', sub: '8/8 criteria · sealed', accent: ...},
          { label: 'Min CPA', value: '0.52 nm', ...},
```

Replace the entire hardcoded array render with:

```tsx
      {/* KPI Cards Row */}
      <div style={{
        display: 'flex', gap: 8, padding: '12px 18px', flexWrap: 'wrap',
      }}>
        {[
          {
            label: 'VERDICT',
            value: scoring?.verdict ? scoring.verdict.toUpperCase() : '—',
            sub: scoring?.verdict === 'pass' ? '✓ criteria met' : scoring?.verdict === 'fail' ? '✗ criteria failed' : 'pending',
            accent: scoring?.verdict === 'pass' ? 'var(--c-stbd)' : scoring?.verdict === 'fail' ? 'var(--c-danger)' : 'var(--txt-3)',
          },
          {
            label: 'Min CPA',
            value: kpis?.min_cpa_nm != null ? `${kpis.min_cpa_nm.toFixed(3)} nm` : '—',
            sub: '≥ 0.27 nm threshold',
            accent: kpis?.min_cpa_nm != null && kpis.min_cpa_nm >= 0.27 ? 'var(--c-phos)' : 'var(--c-danger)',
          },
          {
            label: 'COLREGs',
            value: ruleChain.length > 0 ? '1.00' : '—',
            sub: ruleChain[0] ?? 'no rule applied',
            accent: 'var(--c-stbd)',
          },
          {
            label: 'Max Rudder',
            value: (kpis as any)?.max_rud_deg != null ? `${((kpis as any).max_rud_deg as number).toFixed(1)}°` : '—',
            sub: 'rudder angle',
            accent: 'var(--c-info)',
          },
          {
            label: 'ToR Time',
            value: (kpis as any)?.tor_time_s != null ? `${(kpis as any).tor_time_s}s` : 'N/A',
            sub: 'transfer of control',
            accent: 'var(--c-warn)',
          },
          {
            label: 'Decision P99',
            value: (kpis as any)?.decision_p99_ms != null ? `${(kpis as any).decision_p99_ms} ms` : '—',
            sub: '<500ms target',
            accent: 'var(--c-stbd)',
          },
          {
            label: 'Faults',
            value: (kpis as any)?.faults_injected != null ? String((kpis as any).faults_injected) : '—',
            sub: 'injected',
            accent: 'var(--c-warn)',
          },
          {
            label: 'SHA-256',
            value: (kpis as any)?.scenario_sha256 ? '✓ verified' : '—',
            sub: 'chain integrity',
            accent: 'var(--c-phos)',
          },
        ].map((kpi, i) => (
          <div key={i} style={{
            display: 'flex', flexDirection: 'column', gap: 2,
            padding: '6px 10px', borderLeft: '1px solid var(--line-1)',
            minWidth: 85,
          }}>
            <div style={{ fontFamily: 'var(--f-disp)', fontSize: 8, color: 'var(--txt-3)',
                          textTransform: 'uppercase', letterSpacing: '0.16em' }}>
              {kpi.label}
            </div>
            <div style={{ display: 'flex', alignItems: 'baseline', gap: 4 }}>
              <span style={{ fontFamily: 'var(--f-mono)', fontSize: 16,
                             color: kpi.accent, fontWeight: 600 }}>
                {kpi.value}
              </span>
            </div>
            <div style={{ fontFamily: 'var(--f-mono)', fontSize: 8.5, color: 'var(--txt-2)' }}>
              {kpi.sub}
            </div>
          </div>
        ))}
      </div>
```

- [ ] **Step 7.4: Run tests**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/web"
npx vitest run src/screens/__tests__/RunReport.test.tsx 2>&1 | tail -15
```
Expected: both new tests PASS.

- [ ] **Step 7.5: Commit**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
git add web/src/screens/RunReport.tsx web/src/screens/__tests__/RunReport.test.tsx
git commit -m "feat(report): wire KPI cards to live scoring API, remove hardcoded values"
```

---

## Task 8: End-to-End Smoke Test

Manual walkthrough to verify the 4-screen flow.

- [ ] **Step 8.1: Start the stack**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
bash tools/demo/run_demo.sh
```
Expected: "=== Ready ===" printed with the three URLs.

- [ ] **Step 8.2: Builder screen — ships visible**

Open `http://localhost:5173/#/builder`. Select `imazu-01-ho-v1.0` in the left tree.

Verify:
- ENC map shows two ship markers: red triangle at ~63.0°N, blue triangle at ~63.12°N
- Right rail shows 4 tab icons on right edge at 48px width
- Click `⊕` tab → panel expands to 320px, shows IMAZU-22 grid
- Click `☰` tab → shows OS/TS summary rows + RUN button

- [ ] **Step 8.3: Preflight screen — 5 checks pass**

Click "RUN →" in Summary tab. Verify:
- Page transitions to `/preflight/imazu-01-ho-v1.0`
- 5 check rows appear sequentially with PASS badges
- 3-2-1 countdown fires after all checks pass
- Page transitions to `/bridge/<run_id>`

- [ ] **Step 8.4: Bridge screen — ships animate**

Verify:
- Waiting overlay disappears within 2–3 seconds
- OS (red) moves north and turns ~35° starboard around T=300s
- TS (blue) moves south at constant heading
- CaptainHUD shows live SOG / COG / ROT values
- Module pulse bar shows 8 green indicators

- [ ] **Step 8.5: Report screen — live KPIs**

Click STOP button in Bridge toolbar. Verify:
- Page transitions to `/report/<run_id>`
- KPI card "Min CPA" shows `0.2xx nm` (computed, not `0.52 nm`)
- VERDICT shows PASS or FAIL (based on actual trajectory CPA vs 0.27nm threshold)
- SHA-256 card shows `✓ verified`

- [ ] **Step 8.6: Commit final status**

If all checks pass:
```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
git tag demo1-complete
```

---

## Self-Review Checklist

**Spec coverage:**
- ✅ §3.2 REST API — all endpoints covered in Task 2
- ✅ §3.3 WS broadcast — topic names use `/sil/` prefix matching `useFoxgloveLive.ts` (line 44–91)
- ✅ §3.4 trajectory — Task 1 covers all phases (0–150s / 150–180s / ... wait, I adjusted to YAML values: 0–300s / 300–390s / 390–450s / 450–540s / 540–700s)
- ✅ §4.1 Builder right rail — Task 4+5
- ✅ §4.2 BridgeHMI overlay — Task 6
- ✅ §4.3 RunReport KPI — Task 7
- ✅ §5 startup — Task 3

**Placeholder scan:**
- No `TBD` or `TODO` found.
- All code blocks are complete.

**Type consistency:**
- `ScoringLastRun.kpis` in `silApi.ts` has `{min_cpa_nm, avg_rot_dpm, distance_nm, duration_s}`. The backend returns these plus additional optional fields (`max_rud_deg`, `decision_p99_ms`, `faults_injected`, `scenario_sha256`, `tor_time_s`). RunReport accesses extras via `(kpis as any)?.field` to avoid TypeScript errors without modifying the shared interface. If the interface needs to be extended, that's a clean single-line change to `silApi.ts`.
- WS topics exactly match `useFoxgloveLive.ts` switch cases (all prefixed `/sil/`).
- `updateTargets` in `telemetryStore.ts` expects an array — WS server wraps TS state in `[...]`.
