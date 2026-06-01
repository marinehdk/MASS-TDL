# Scenario Trace Fast-Verify Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a fast, deterministic scenario verification loop: Python edit → `npm run sil:restart` (30s, no rebuild) → `npm run test:trace` → structured diagnostic JSON in terminal.

**Architecture:** `DebugTraceWriter` in `sil_topic_bridge.py` subscribes to 7 key topics and writes JSONL to the shared volume. A new `debug_routes.py` in the orchestrator exposes three REST endpoints that read that file. A pytest harness (`tools/sil/test_scenario_trace.py`) drives the scenario and asserts on `/debug/summary`. A fixed `name: mass-l3-sil` in docker-compose.yml and hot-reload volume mounts eliminate container drift and rebuild churn.

**Tech Stack:** Docker Compose v2 (`name:` directive), FastAPI, rclpy, pytest, Python stdlib (`json`, `threading`, `collections`, `pathlib`)

---

## File Map

| Action | Path |
|---|---|
| **Modify** | `docker-compose.yml` — add `name:`, hot-reload mounts |
| **Modify** | `package.json` — add 6 npm scripts |
| **Modify** | `tools/sil/_e2e_helpers.py` — fix `CONTAINER` constant |
| **Modify** | `docker/sil_topic_bridge.py` — add `DebugTraceWriter` class + `record()` calls + veto subscription |
| **Create** | `src/sil_orchestrator/routers/debug_routes.py` |
| **Modify** | `src/sil_orchestrator/main.py` — register debug router |
| **Create** | `tests/sil_orchestrator/test_debug_routes.py` |
| **Create** | `tools/sil/conftest.py` — `--scenario` CLI option |
| **Create** | `tools/sil/test_scenario_trace.py` — E2E trace assertions |

---

## Task 1: Stabilize docker-compose.yml

**Files:**
- Modify: `docker-compose.yml:1-34`

- [ ] **Step 1: Add project name and hot-reload volume mounts**

Replace the top of `docker-compose.yml` (from the opening comment through the `sil-nodes` block). Exact diff:

```yaml
# NOTE: dev services are run natively on the host to avoid port conflicts.

name: mass-l3-sil

services:
  sil-orchestrator:
    build:
      context: .
      dockerfile: docker/sil_orchestrator.Dockerfile
    ports:
      - "8000:8000"
    volumes:
      - ./scenarios:/var/sil/scenarios
      - ./runs:/var/sil/runs
      - ./exports:/var/sil/exports
      - ./certs:/certs:ro
      - ./src/sil_orchestrator:/opt/sil/sil_orchestrator   # hot-reload: no rebuild for orchestrator Python changes
    environment:
      - ROS_DOMAIN_ID=0
      - RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
    network_mode: host

  sil-nodes:
    build:
      context: .
      dockerfile: docker/sil_nodes.Dockerfile
    volumes:
      - ./scenarios:/var/sil/scenarios
      - ./runs:/var/sil/runs
      - ./src:/opt/ws/src
      - ./docker:/opt/ws/docker   # hot-reload: no rebuild for bridge/mock Python changes
    command: /opt/ws/sil_entrypoint.sh
    environment:
      - SIL_RUN_DIR=/var/sil/runs
      - ROS_DOMAIN_ID=0
      - RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
      - SIL_L3_ENABLE=1               # Set 0 to bypass L3 kernel (sim-only mode)
    network_mode: host
```

- [ ] **Step 2: Verify project name is applied**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer" && docker compose config | grep "^name:"
```
Expected output: `name: mass-l3-sil`

- [ ] **Step 3: Restart stack and confirm container names**

```bash
npm run sys:stop && npm run sys:start && sleep 10 && docker compose ps --format "table {{.Name}}"
```
Expected: container names start with `mass-l3-sil-` (e.g. `mass-l3-sil-sil-nodes-1`).

- [ ] **Step 4: Smoke-test hot-reload (sil_topic_bridge.py)**

Add a harmless log line at the top of `SilTopicBridge.__init__` in `docker/sil_topic_bridge.py`:
```python
self.get_logger().info("[sil_topic_bridge] HOT-RELOAD-TEST")
```
Then restart (no build) and verify log appears:
```bash
docker compose restart sil-nodes && sleep 15 && docker compose logs sil-nodes --tail=30 | grep HOT-RELOAD-TEST
```
Expected: line containing `HOT-RELOAD-TEST`. Revert the log line after confirming.

- [ ] **Step 5: Commit**

```bash
git add docker-compose.yml
git commit -m "infra: fix compose project name to mass-l3-sil + add hot-reload volume mounts"
```

---

## Task 2: Update package.json scripts + fix _e2e_helpers.py container name

**Files:**
- Modify: `package.json:11-20`
- Modify: `tools/sil/_e2e_helpers.py:14`

- [ ] **Step 1: Add npm scripts to package.json**

In `package.json`, replace the `"scripts"` block with:

```json
"scripts": {
  "sys:start": "docker compose up -d && pm2 start ecosystem.config.cjs",
  "sys:stop": "pm2 delete ecosystem.config.cjs && docker compose down",
  "sys:status": "pm2 list && docker compose ps",
  "sys:restart": "pm2 restart ecosystem.config.cjs && docker compose restart",
  "sil:restart": "docker compose restart sil-nodes sil-orchestrator",
  "sil:restart:orch": "docker compose restart sil-orchestrator",
  "sil:rebuild": "docker compose build sil-nodes && docker compose up -d",
  "sil:rebuild:orch": "docker compose build sil-orchestrator && docker compose up -d sil-orchestrator",
  "sil:prune": "docker image prune -f && docker builder prune -f --keep-storage 5GB",
  "sil:logs": "docker compose logs --tail=100 -f sil-nodes sil-orchestrator",
  "dev:backend": "PYTHONPATH=src python3 -m uvicorn sil_orchestrator.main:app --port 8000 --host 127.0.0.1",
  "dev:frontend": "npm run dev --prefix web",
  "graph:update": "graphify update . && npm run graph:prune",
  "graph:prune": "cd graphify-out && LATEST=$(ls -dt 2026-*/ 2>/dev/null | head -1 | tr -d '/') && [ -n \"$LATEST\" ] && ls -d 2026-* | grep -v \"^${LATEST}$\" | xargs rm -rf; true",
  "test:trace": "pytest tools/sil/test_scenario_trace.py -v -s",
  "test": "echo \"Error: no test specified\" && exit 1"
}
```

- [ ] **Step 2: Fix CONTAINER constant in _e2e_helpers.py**

In `tools/sil/_e2e_helpers.py`, change line 14:
```python
CONTAINER = "mass-l3-sil-sil-nodes-1"
```

- [ ] **Step 3: Verify npm scripts are registered**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer" && npm run sil:restart -- --dry-run 2>&1 || docker compose restart sil-nodes sil-orchestrator --dry-run 2>&1 | head -5; npm run 2>&1 | grep "sil:"
```
Expected: `sil:restart`, `sil:rebuild`, `sil:prune`, `sil:logs` listed.

- [ ] **Step 4: Commit**

```bash
git add package.json tools/sil/_e2e_helpers.py
git commit -m "infra: add sil:restart/rebuild/prune npm scripts; fix hardcoded container name"
```

---

## Task 3: Add DebugTraceWriter to sil_topic_bridge.py

**Files:**
- Modify: `docker/sil_topic_bridge.py`

- [ ] **Step 1: Add stdlib imports**

In `docker/sil_topic_bridge.py`, change the stdlib import block (lines 18-21) from:
```python
import math
import signal
import threading
import time
```
to:
```python
import collections
import json
import math
import os
import signal
import threading
import time
from pathlib import Path
```

- [ ] **Step 2: Add CheckerVetoNotification import**

In `docker/sil_topic_bridge.py`, change the `l3_external_msgs` import block (lines 40-45) from:
```python
from l3_external_msgs.msg import (
    PlannedRoute,
    FilteredOwnShipState,
    TrackedTargetArray,
    EnvironmentState as L3EnvironmentState,
)
```
to:
```python
from l3_external_msgs.msg import (
    PlannedRoute,
    FilteredOwnShipState,
    TrackedTargetArray,
    EnvironmentState as L3EnvironmentState,
    CheckerVetoNotification,
)
```

- [ ] **Step 3: Add DebugTraceWriter class before SilTopicBridge**

Insert the following class definition immediately before line 167 (`# ── Bridge node ──`):

```python
# ── Debug trace writer ────────────────────────────────────

class DebugTraceWriter:
    """Ring-buffer JSONL writer for key L3 interface topics.

    Appends to /var/sil/runs/trace_current.jsonl (shared volume).
    Thread-safe; flushes every 2s. Call reset() on scenario ACTIVE to truncate.
    """

    FLUSH_INTERVAL_S = 2.0
    MAX_BUF = 2000

    def __init__(self, node: "SilTopicBridge") -> None:
        self._node = node
        self._lock = threading.Lock()
        self._buf: collections.deque = collections.deque(maxlen=self.MAX_BUF)
        self._file = None
        self._flush_timer: threading.Timer | None = None
        run_dir = Path(os.environ.get("SIL_RUN_DIR", "/var/sil/runs"))
        self._trace_path = run_dir / "trace_current.jsonl"
        self.reset()

    def reset(self) -> None:
        """Truncate trace file and restart flush timer. Call on scenario ACTIVE."""
        with self._lock:
            if self._file is not None:
                try:
                    self._file.close()
                except Exception:
                    pass
            self._buf.clear()
            try:
                self._trace_path.parent.mkdir(parents=True, exist_ok=True)
                self._file = open(self._trace_path, "w")
            except Exception as exc:
                self._node.get_logger().error(
                    f"[DebugTraceWriter] cannot open {self._trace_path}: {exc}")
                self._file = None
        self._schedule_flush()

    def record(self, topic: str, data: dict, sim_t: float) -> None:
        """Append one record to in-memory ring buffer."""
        entry = {"sim_t": round(sim_t, 3), "topic": topic}
        entry.update(data)
        with self._lock:
            self._buf.append(json.dumps(entry, default=str))

    def _schedule_flush(self) -> None:
        if self._flush_timer is not None:
            self._flush_timer.cancel()
        t = threading.Timer(self.FLUSH_INTERVAL_S, self._flush)
        t.daemon = True
        t.start()
        self._flush_timer = t

    def _flush(self) -> None:
        with self._lock:
            if self._file and self._buf:
                try:
                    lines = list(self._buf)
                    self._buf.clear()
                    self._file.write("\n".join(lines) + "\n")
                    self._file.flush()
                except Exception as exc:
                    self._node.get_logger().warning(
                        f"[DebugTraceWriter] flush error: {exc}")
        self._schedule_flush()

    def close(self) -> None:
        if self._flush_timer:
            self._flush_timer.cancel()
        with self._lock:
            if self._file:
                try:
                    if self._buf:
                        self._file.write("\n".join(self._buf) + "\n")
                    self._file.flush()
                    self._file.close()
                except Exception:
                    pass
                self._file = None
```

- [ ] **Step 4: Instantiate DebugTraceWriter in SilTopicBridge.__init__**

At the end of `SilTopicBridge.__init__`, just before the closing of the method (after line 316, the `self._sub_lifecycle` subscription), add:

```python
        # ── Debug trace writer ────────────────────────────────
        self._trace_writer = DebugTraceWriter(node=self)

        # ── Checker veto subscription (debug trace) ───────────
        self._sub_veto = self.create_subscription(
            CheckerVetoNotification, "/l3/checker/veto",
            self._on_checker_veto, rq)
```

- [ ] **Step 5: Add trace reset call in _on_lifecycle_status**

In `_on_lifecycle_status` (line 349), in the `else:` branch (state == ACTIVE, around line 376), add one line after the `try:` block's successful path:

Find the `else:` block:
```python
        else:
            # ACTIVE state: dynamically read initial parameters to ensure we use the new scenario values
            try:
                init_heading = self.get_parameter("ownship_initial_heading_deg").value
                init_sog = self.get_parameter("ownship_initial_sog_kn").value
                if self._target_heading_deg != init_heading or self._target_sog_kn != init_sog:
                    self._target_heading_deg = init_heading
                    self._target_sog_kn = init_sog
                    self.get_logger().info(
                        f"[BRIDGE] Simulation active. Updated initial parameters: "
                        f"heading={self._target_heading_deg}°, SOG={self._target_sog_kn} kn"
                    )
            except Exception as exc:
                self.get_logger().warn(f"Failed to read updated parameters from server: {exc}")
```

Replace with:
```python
        else:
            # ACTIVE state: dynamically read initial parameters to ensure we use the new scenario values
            self._trace_writer.reset()
            try:
                init_heading = self.get_parameter("ownship_initial_heading_deg").value
                init_sog = self.get_parameter("ownship_initial_sog_kn").value
                if self._target_heading_deg != init_heading or self._target_sog_kn != init_sog:
                    self._target_heading_deg = init_heading
                    self._target_sog_kn = init_sog
                    self.get_logger().info(
                        f"[BRIDGE] Simulation active. Updated initial parameters: "
                        f"heading={self._target_heading_deg}°, SOG={self._target_sog_kn} kn"
                    )
            except Exception as exc:
                self.get_logger().warn(f"Failed to read updated parameters from server: {exc}")
```

- [ ] **Step 6: Add record() call in _on_mission_goal**

In `_on_mission_goal` (line 513), after `self._record_pulse(M3)` (line 515), add:

```python
        self._trace_writer.record("/l3/m3/mission_goal", {
            "fsm_state": int(msg.fsm_state),
            "task_validity": int(msg.task_validity) if hasattr(msg, "task_validity") else -1,
            "target_wp_lat": float(msg.current_target_wp.latitude),
            "target_wp_lon": float(msg.current_target_wp.longitude),
        }, self._get_sim_time())
```

- [ ] **Step 7: Add record() call in _on_behavior_plan**

In `_on_behavior_plan` (line 394), after `self._last_behavior_plan = msg` (line 396), add:

```python
        self._trace_writer.record("/l3/m4/behavior_plan", {
            "behavior": int(msg.behavior),
            "heading_min_deg": float(msg.heading_min_deg),
            "heading_max_deg": float(msg.heading_max_deg),
            "avoidance_active": self._avoidance_active,
            "target_heading_deg": self._avoidance_target_heading_deg,
        }, self._get_sim_time())
```

- [ ] **Step 8: Add record() call in _on_avoidance_plan**

In `_on_avoidance_plan` (line 616), after `self._record_pulse(M5)` (line 617), add:

```python
        _wp0 = msg.waypoints[0] if msg.waypoints else None
        self._trace_writer.record("/l3/m5/avoidance_plan", {
            "n_waypoints": len(msg.waypoints),
            "solver_status": "VALID" if (_wp0 and abs(_wp0.turn_radius_m) > 1e-6) else "EMPTY",
            "wp0_turn_radius_m": float(_wp0.turn_radius_m) if _wp0 else 0.0,
            "wp0_target_speed_kn": float(_wp0.target_speed_kn) if _wp0 else 0.0,
        }, self._get_sim_time())
```

- [ ] **Step 9: Add record() call in _on_own_ship_state**

In `_on_own_ship_state` (line 422), after `self._last_ownship_raw = msg` (line 424), add:

```python
        self._trace_writer.record("/sil/own_ship_state", {
            "heading_deg": round(math.degrees(msg.heading), 2),
            "sog_kn": round(msg.sog * 1.94384, 2),
            "lat": msg.lat,
            "lon": msg.lon,
            "rot_deg_s": round(math.degrees(msg.rot), 3),
        }, self._get_sim_time())
```

- [ ] **Step 10: Add _on_checker_veto callback**

Append the following method to `SilTopicBridge` (anywhere after `_on_lifecycle_status`):

```python
    def _on_checker_veto(self, msg: CheckerVetoNotification) -> None:
        self._trace_writer.record("/l3/checker/veto", {
            "checker_layer": str(msg.checker_layer),
            "vetoed_module": str(msg.vetoed_module),
            "veto_reason_class": int(msg.veto_reason_class),
            "veto_reason_detail": str(msg.veto_reason_detail),
            "fallback_provided": bool(msg.fallback_provided),
            "confidence": float(msg.confidence),
        }, self._get_sim_time())
```

- [ ] **Step 11: Verify trace file is created on scenario start**

```bash
npm run sil:restart && sleep 20
# Start a scenario via REST:
curl -sk -X POST https://localhost:8000/api/v1/lifecycle/configure \
  -H "Content-Type: application/json" -d '{"scenario_id":"colreg-rule14-ho"}' | python3 -m json.tool
curl -sk -X POST https://localhost:8000/api/v1/lifecycle/activate | python3 -m json.tool
sleep 15
ls -lh runs/trace_current.jsonl && head -3 runs/trace_current.jsonl
```
Expected: file exists, each line is valid JSON with `"sim_t"` and `"topic"` fields.

- [ ] **Step 12: Cleanup scenario**

```bash
curl -sk -X POST https://localhost:8000/api/v1/lifecycle/cleanup | python3 -m json.tool
```

- [ ] **Step 13: Commit**

```bash
git add docker/sil_topic_bridge.py
git commit -m "feat(bridge): add DebugTraceWriter — JSONL trace for M3/M4/M5/oss/veto topics"
```

---

## Task 4: Add debug_routes.py + register in main.py

**Files:**
- Create: `src/sil_orchestrator/routers/debug_routes.py`
- Create: `tests/sil_orchestrator/test_debug_routes.py`
- Modify: `src/sil_orchestrator/main.py:28` (add import + include_router)

- [ ] **Step 1: Create routers/ directory**

```bash
mkdir -p "/Users/marine/Code/MASS-L3-Tactical Layer/src/sil_orchestrator/routers"
touch "/Users/marine/Code/MASS-L3-Tactical Layer/src/sil_orchestrator/routers/__init__.py"
```

- [ ] **Step 2: Write the failing test first**

Create `tests/sil_orchestrator/test_debug_routes.py`:

```python
"""Unit tests for /api/v1/debug/* endpoints.

Uses a temp JSONL fixture — no ROS2 or running containers needed.
"""
from __future__ import annotations

import json
import importlib
from pathlib import Path

import pytest
from fastapi.testclient import TestClient


# ── Fixture helpers ─────────────────────────────────────────

SAMPLE_RECORDS = [
    {"sim_t": 10.0, "topic": "/l3/m3/mission_goal", "fsm_state": 3, "task_validity": 0,
     "target_wp_lat": 0.0, "target_wp_lon": 0.0},
    {"sim_t": 20.0, "topic": "/l3/m4/behavior_plan", "behavior": 0,
     "heading_min_deg": 350.0, "heading_max_deg": 10.0, "avoidance_active": False,
     "target_heading_deg": None},
    {"sim_t": 250.0, "topic": "/l3/m4/behavior_plan", "behavior": 1,
     "heading_min_deg": 5.0, "heading_max_deg": 45.0, "avoidance_active": True,
     "target_heading_deg": 33.0},
    {"sim_t": 250.5, "topic": "/l3/m5/avoidance_plan", "n_waypoints": 3,
     "solver_status": "VALID", "wp0_turn_radius_m": 250.0, "wp0_target_speed_kn": 8.0},
    {"sim_t": 300.0, "topic": "/sil/own_ship_state", "heading_deg": 33.2,
     "sog_kn": 8.1, "lat": 60.12, "lon": 5.01, "rot_deg_s": 0.5},
    {"sim_t": 520.0, "topic": "/l3/m4/behavior_plan", "behavior": 0,
     "heading_min_deg": 350.0, "heading_max_deg": 10.0, "avoidance_active": False,
     "target_heading_deg": None},
    {"sim_t": 600.0, "topic": "/l3/m3/mission_goal", "fsm_state": 3, "task_validity": 1,
     "target_wp_lat": 60.15, "target_wp_lon": 5.02},
]


@pytest.fixture
def trace_file(tmp_path, monkeypatch):
    """Write SAMPLE_RECORDS to a temp JSONL and point debug_routes at it."""
    p = tmp_path / "trace_current.jsonl"
    p.write_text("\n".join(json.dumps(r) for r in SAMPLE_RECORDS) + "\n")

    import sil_orchestrator.routers.debug_routes as dr
    monkeypatch.setattr(dr, "_TRACE_FILE", p)
    return p


@pytest.fixture
def client(trace_file):
    from fastapi import FastAPI
    import sil_orchestrator.routers.debug_routes as dr
    importlib.reload(dr)
    monkeypatch_app = FastAPI()
    monkeypatch_app.include_router(dr.router)
    return TestClient(monkeypatch_app)


# ── Tests ───────────────────────────────────────────────────

def test_trace_returns_records(client, trace_file, monkeypatch):
    import sil_orchestrator.routers.debug_routes as dr
    monkeypatch.setattr(dr, "_TRACE_FILE", trace_file)
    r = client.get("/api/v1/debug/trace?last_n=10")
    assert r.status_code == 200
    body = r.json()
    assert body["count"] == len(SAMPLE_RECORDS)
    assert body["records"][0]["topic"] == "/l3/m3/mission_goal"


def test_trace_last_n_limits(client, trace_file, monkeypatch):
    import sil_orchestrator.routers.debug_routes as dr
    monkeypatch.setattr(dr, "_TRACE_FILE", trace_file)
    r = client.get("/api/v1/debug/trace?last_n=2")
    assert r.json()["count"] == 2
    # Last 2 records should be the last items
    assert r.json()["records"][-1]["sim_t"] == 600.0


def test_snapshot_returns_latest_per_topic(client, trace_file, monkeypatch):
    import sil_orchestrator.routers.debug_routes as dr
    monkeypatch.setattr(dr, "_TRACE_FILE", trace_file)
    r = client.get("/api/v1/debug/snapshot")
    assert r.status_code == 200
    topics = r.json()["topics"]
    # Latest M4 behavior_plan should be the TRANSIT one at sim_t=520
    m4 = topics.get("/l3/m4/behavior_plan", {})
    assert m4.get("behavior") == 0
    assert m4.get("sim_t") == 520.0
    # Latest M3 should have task_validity=1 (sim_t=600)
    m3 = topics.get("/l3/m3/mission_goal", {})
    assert m3.get("task_validity") == 1


def test_summary_m4_phase_timeline(client, trace_file, monkeypatch):
    import sil_orchestrator.routers.debug_routes as dr
    monkeypatch.setattr(dr, "_TRACE_FILE", trace_file)
    r = client.get("/api/v1/debug/summary")
    assert r.status_code == 200
    phases = r.json()["m4_phase_timeline"]
    phase_names = [p["phase"] for p in phases]
    assert "TRANSIT" in phase_names
    assert "BEHAVIOR_1" in phase_names
    # Should end in TRANSIT (route return)
    assert phase_names[-1] == "TRANSIT"


def test_summary_m5_solver_stats(client, trace_file, monkeypatch):
    import sil_orchestrator.routers.debug_routes as dr
    monkeypatch.setattr(dr, "_TRACE_FILE", trace_file)
    r = client.get("/api/v1/debug/summary")
    stats = r.json()["m5_solver_stats"]
    assert stats["VALID"] == 1
    assert stats["convergence_rate_pct"] == 100.0


def test_summary_m3_task_validity_timeline(client, trace_file, monkeypatch):
    import sil_orchestrator.routers.debug_routes as dr
    monkeypatch.setattr(dr, "_TRACE_FILE", trace_file)
    r = client.get("/api/v1/debug/summary")
    timeline = r.json()["m3_task_validity_timeline"]
    # First entry: task_validity=0 starting at sim_t=10
    assert timeline[0]["task_validity"] == 0
    assert timeline[0]["from_sim_t"] == 10.0
    # Second entry: task_validity=1 at sim_t=600
    assert any(e["task_validity"] == 1 for e in timeline)


def test_summary_max_heading(client, trace_file, monkeypatch):
    import sil_orchestrator.routers.debug_routes as dr
    monkeypatch.setattr(dr, "_TRACE_FILE", trace_file)
    r = client.get("/api/v1/debug/summary")
    assert r.json()["max_heading_deg"] == 33.2


def test_trace_empty_when_no_file(tmp_path, monkeypatch):
    import sil_orchestrator.routers.debug_routes as dr
    monkeypatch.setattr(dr, "_TRACE_FILE", tmp_path / "nonexistent.jsonl")
    from fastapi import FastAPI
    from fastapi.testclient import TestClient
    app = FastAPI()
    app.include_router(dr.router)
    c = TestClient(app)
    r = c.get("/api/v1/debug/trace")
    assert r.status_code == 200
    assert r.json()["count"] == 0
```

- [ ] **Step 3: Run tests to verify they fail**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer" && pytest tests/sil_orchestrator/test_debug_routes.py -v 2>&1 | head -30
```
Expected: `ModuleNotFoundError: No module named 'sil_orchestrator.routers.debug_routes'`

- [ ] **Step 4: Implement debug_routes.py**

Create `src/sil_orchestrator/routers/debug_routes.py`:

```python
"""Debug REST endpoints — scenario trace for in-conversation diagnostics.

GET /api/v1/debug/trace?last_n=500   raw JSONL records
GET /api/v1/debug/snapshot           latest record per topic
GET /api/v1/debug/summary            derived: M3/M4/M5 timelines + trajectory stats
"""
from __future__ import annotations

import json
from collections import Counter
from pathlib import Path

from fastapi import APIRouter, Query

from sil_orchestrator.config import RUN_DIR

router = APIRouter()

_TRACE_FILE: Path = RUN_DIR / "trace_current.jsonl"


def _tail_jsonl(n: int) -> list[dict]:
    """Return last n parsed records from _TRACE_FILE."""
    if not _TRACE_FILE.exists():
        return []
    lines = _TRACE_FILE.read_text(errors="replace").splitlines()
    tail = lines[-n:] if len(lines) > n else lines
    out: list[dict] = []
    for line in tail:
        line = line.strip()
        if line:
            try:
                out.append(json.loads(line))
            except json.JSONDecodeError:
                pass
    return out


@router.get("/api/v1/debug/trace")
async def debug_trace(last_n: int = Query(default=500, ge=1, le=10000)):
    records = _tail_jsonl(last_n)
    return {"records": records, "count": len(records)}


@router.get("/api/v1/debug/snapshot")
async def debug_snapshot():
    records = _tail_jsonl(5000)
    latest: dict[str, dict] = {}
    for rec in reversed(records):
        t = rec.get("topic", "")
        if t and t not in latest:
            latest[t] = rec
    max_sim_t = max((r.get("sim_t", 0.0) for r in records), default=0.0)
    return {"sim_t": max_sim_t, "topics": latest}


@router.get("/api/v1/debug/summary")
async def debug_summary():
    records = _tail_jsonl(10000)
    if not records:
        return {"error": "no trace data — start and run a scenario first"}

    # ── M3 task_validity timeline ─────────────────────────────
    m3 = [r for r in records if r.get("topic") == "/l3/m3/mission_goal"]
    m3_timeline: list[dict] = []
    for r in m3:
        tv = r.get("task_validity", -1)
        t = r.get("sim_t", 0.0)
        if not m3_timeline or m3_timeline[-1]["task_validity"] != tv:
            if m3_timeline:
                m3_timeline[-1]["to_sim_t"] = t
            m3_timeline.append({
                "task_validity": tv,
                "from_sim_t": t,
                "to_sim_t": None,
                "target_wp_lat": r.get("target_wp_lat"),
                "target_wp_lon": r.get("target_wp_lon"),
            })
    if m3_timeline and m3:
        m3_timeline[-1]["to_sim_t"] = m3[-1].get("sim_t")

    # ── M4 behavior phase timeline ────────────────────────────
    m4 = [r for r in records if r.get("topic") == "/l3/m4/behavior_plan"]
    m4_timeline: list[dict] = []
    for r in m4:
        behavior_id = r.get("behavior", -1)
        phase = "TRANSIT" if behavior_id == 0 else f"BEHAVIOR_{behavior_id}"
        t = r.get("sim_t", 0.0)
        if not m4_timeline or m4_timeline[-1]["phase"] != phase:
            if m4_timeline:
                m4_timeline[-1]["to_sim_t"] = t
            m4_timeline.append({"phase": phase, "from_sim_t": t, "to_sim_t": None})
    if m4_timeline and m4:
        m4_timeline[-1]["to_sim_t"] = m4[-1].get("sim_t")

    # ── M5 solver stats ───────────────────────────────────────
    m5 = [r for r in records if r.get("topic") == "/l3/m5/avoidance_plan"]
    counter: Counter = Counter(r.get("solver_status", "UNKNOWN") for r in m5)
    total = sum(counter.values())
    conv_rate = round(counter.get("VALID", 0) / total * 100, 1) if total else 0.0

    # ── Own ship trajectory (≤20 samples) ────────────────────
    oss = [r for r in records if r.get("topic") == "/sil/own_ship_state"]
    step = max(1, len(oss) // 20)
    traj = [
        {"sim_t": r["sim_t"], "lat": r.get("lat"), "lon": r.get("lon"),
         "hdg_deg": r.get("heading_deg"), "sog_kn": r.get("sog_kn")}
        for r in oss[::step]
    ]
    headings = [r.get("heading_deg", 0.0) for r in oss]
    max_hdg = max(headings, default=0.0)
    max_hdg_t = next((r["sim_t"] for r in oss if r.get("heading_deg") == max_hdg), 0.0)

    # ── Veto events ───────────────────────────────────────────
    veto_events = [r for r in records if r.get("topic") == "/l3/checker/veto"]

    return {
        "sim_duration_s": max((r.get("sim_t", 0.0) for r in records), default=0.0),
        "total_records": len(records),
        "m3_task_validity_timeline": m3_timeline,
        "m4_phase_timeline": m4_timeline,
        "m5_solver_stats": {**dict(counter), "total": total, "convergence_rate_pct": conv_rate},
        "own_ship_trajectory_sampled": traj,
        "max_heading_deg": round(max_hdg, 1),
        "max_heading_sim_t": max_hdg_t,
        "veto_events": veto_events,
    }
```

- [ ] **Step 5: Run tests — expect PASS**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer" && pytest tests/sil_orchestrator/test_debug_routes.py -v
```
Expected: all 8 tests PASS.

- [ ] **Step 6: Register debug router in main.py**

In `src/sil_orchestrator/main.py`, after the existing router imports (line 28), add:

```python
from sil_orchestrator.routers.debug_routes import router as debug_router
```

Then find the block where routers are added to `app` (look for `app.include_router`) and add:

```python
app.include_router(debug_router)
```

alongside the other `include_router` calls. If `include_router` isn't used (routes are added directly), instead add at the end of the import section and call it after `app = FastAPI(...)`.

To find the exact location:
```bash
grep -n "include_router\|add_route\|app\.router" /Users/marine/Code/MASS-L3-Tactical\ Layer/src/sil_orchestrator/main.py | head -20
```

- [ ] **Step 7: Verify debug endpoints via curl (orchestrator must be running)**

```bash
curl -sk https://localhost:8000/api/v1/debug/trace?last_n=5 | python3 -m json.tool | head -20
curl -sk https://localhost:8000/api/v1/debug/snapshot | python3 -m json.tool | head -30
curl -sk https://localhost:8000/api/v1/debug/summary | python3 -m json.tool
```
Expected: JSON responses; `trace` returns `{"records": [...], "count": N}`; `summary` shows M3/M4/M5 timelines.

- [ ] **Step 8: Commit**

```bash
git add src/sil_orchestrator/routers/ tests/sil_orchestrator/test_debug_routes.py src/sil_orchestrator/main.py
git commit -m "feat(orchestrator): add /debug/trace|snapshot|summary REST endpoints"
```

---

## Task 5: pytest trace harness

**Files:**
- Create: `tools/sil/conftest.py`
- Create: `tools/sil/test_scenario_trace.py`

- [ ] **Step 1: Create tools/sil/conftest.py**

```python
# tools/sil/conftest.py
"""Shared pytest options for tools/sil/ integration tests."""


def pytest_addoption(parser):
    try:
        parser.addoption(
            "--scenario",
            default="colreg-rule14-ho",
            help="Scenario ID to run for trace/e2e tests",
        )
    except ValueError:
        pass  # already registered by another conftest
```

- [ ] **Step 2: Create tools/sil/test_scenario_trace.py**

```python
"""Scenario trace test — behavioral assertions via REST debug probe.

Requires: npm run sys:start (stack must be running).

Usage:
    pytest tools/sil/test_scenario_trace.py -v -s
    pytest tools/sil/test_scenario_trace.py -v -s --scenario imazu-01-ho
"""
from __future__ import annotations

import os
import sys

import pytest

# Allow imports from project root (tools/sil/_e2e_helpers, etc.)
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

from tools.sil._e2e_helpers import _get, _post, _wait_until_sim_t  # noqa: E402

SIM_END_T = 700        # sim-seconds to wait before asserting
WAIT_TIMEOUT = 1200    # wall-clock timeout (seconds)


@pytest.fixture(scope="module")
def scenario_id(request):
    return request.config.getoption("--scenario")


@pytest.fixture(scope="module")
def summary(scenario_id):
    """Run scenario end-to-end and return /debug/summary.

    Cleanup runs even if assertions fail (yield fixture).
    """
    _post("/lifecycle/cleanup")
    cfg = _post("/lifecycle/configure", {"scenario_id": scenario_id})
    assert cfg.get("success"), f"configure failed: {cfg}"
    act = _post("/lifecycle/activate")
    assert act.get("success"), f"activate failed: {act}"
    _wait_until_sim_t(SIM_END_T, timeout_wall_s=WAIT_TIMEOUT)
    result = _get("/debug/summary")
    yield result
    _post("/lifecycle/cleanup")


class TestScenarioTrace:
    """Behavioral gate tests for a full scenario run.

    Each test assertion includes diagnostic context so failures are
    immediately actionable without opening Foxglove or running docker exec.
    """

    def test_m3_task_validity_ever_valid(self, summary):
        """M3 must publish task_validity=1 (VALID) at some point."""
        timeline = summary.get("m3_task_validity_timeline", [])
        valid = [e for e in timeline if e.get("task_validity") == 1]
        assert valid, (
            "M3 task_validity never reached VALID (1) — likely K1 root cause "
            "(stuck target WP at (0,0)). "
            f"Full M3 timeline: {timeline}"
        )

    def test_m4_entered_non_transit(self, summary):
        """M4 must leave TRANSIT at least once (avoidance behavior triggered)."""
        phases = summary.get("m4_phase_timeline", [])
        non_transit = [p for p in phases if p["phase"] != "TRANSIT"]
        assert non_transit, (
            "M4 never left TRANSIT — avoidance behavior never triggered. "
            f"Full M4 phase timeline: {phases}"
        )

    def test_m4_non_transit_duration_min_10s(self, summary):
        """Non-TRANSIT avoidance phase must last ≥ 10 sim-seconds."""
        phases = summary.get("m4_phase_timeline", [])
        total = sum(
            (p.get("to_sim_t") or 0.0) - p["from_sim_t"]
            for p in phases
            if p["phase"] != "TRANSIT" and p.get("to_sim_t") is not None
        )
        assert total >= 10.0, (
            f"Non-TRANSIT avoidance lasted only {total:.1f}s — too brief for COLREGs compliance. "
            f"Phase timeline: {phases}"
        )

    def test_starboard_turn_magnitude(self, summary):
        """Own-ship max heading must be in [20, 55] deg (COLREGs Rule 14 starboard)."""
        max_hdg = summary.get("max_heading_deg", 0.0)
        traj = summary.get("own_ship_trajectory_sampled", [])
        assert 20.0 <= max_hdg <= 55.0, (
            f"Max heading {max_hdg:.1f}° outside Rule-14 starboard range [20, 55]. "
            f"Trajectory sample: {traj[:5]}"
        )

    def test_m5_delivers_valid_plans(self, summary):
        """M5 must deliver at least one VALID avoidance plan."""
        stats = summary.get("m5_solver_stats", {})
        valid_count = stats.get("VALID", 0)
        total = stats.get("total", 0)
        assert valid_count > 0, (
            f"M5 delivered 0 VALID plans out of {total} total. "
            f"All statuses: {stats} — MPC NLP likely infeasible (cold-start issue)."
        )

    def test_route_return_after_avoidance(self, summary):
        """M4 must return to TRANSIT after avoidance (route-return complete)."""
        phases = summary.get("m4_phase_timeline", [])
        names = [p["phase"] for p in phases]
        assert names and names[-1] == "TRANSIT", (
            f"M4 did not return to TRANSIT after avoidance. "
            f"Final phase sequence: {names}"
        )

    def test_no_m7_veto_events(self, summary):
        """M7 checker must not have raised any veto during the scenario."""
        vetos = summary.get("veto_events", [])
        assert not vetos, (
            f"Unexpected M7 veto events ({len(vetos)} total): {vetos[:3]}"
        )
```

- [ ] **Step 3: Verify test file is discovered**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer" && pytest tools/sil/test_scenario_trace.py --collect-only 2>&1 | head -20
```
Expected: 7 tests collected under `TestScenarioTrace`.

- [ ] **Step 4: Run the full scenario trace (stack must be running)**

```bash
npm run test:trace
```
Expected with current known bugs:
- `test_m3_task_validity_ever_valid` — **FAIL** (task_validity stuck at 0, K1 bug)
- `test_m4_entered_non_transit` — depends on avoidance fix state
- `test_m5_delivers_valid_plans` — **FAIL** (MPC infeasible, known bug)

The failing tests confirm the bugs are being detected. Pass/fail output is the new baseline.

- [ ] **Step 5: Run against imazu-01-ho to verify harness works**

```bash
pytest tools/sil/test_scenario_trace.py -v -s --scenario imazu-01-ho 2>&1 | tail -20
```

- [ ] **Step 6: Commit**

```bash
git add tools/sil/conftest.py tools/sil/test_scenario_trace.py
git commit -m "feat(test): add scenario trace harness — pytest assertions via REST debug probe"
```

---

## Self-Review Checklist

**Spec coverage:**

| Spec section | Covered by |
|---|---|
| Fixed project name `mass-l3-sil` | Task 1, Step 1 |
| Hot-reload: `./docker:/opt/ws/docker` | Task 1, Step 1 |
| Hot-reload: `./src/sil_orchestrator:/opt/sil/sil_orchestrator` | Task 1, Step 1 |
| npm sil:restart/rebuild/prune/logs | Task 2, Step 1 |
| Fix `CONTAINER` hardcode | Task 2, Step 2 |
| DebugTraceWriter class | Task 3, Steps 2-4 |
| 7 topic subscriptions + record() calls | Task 3, Steps 5-10 |
| Reset on ACTIVE lifecycle | Task 3, Step 5 |
| `GET /debug/trace` | Task 4 |
| `GET /debug/snapshot` | Task 4 |
| `GET /debug/summary` (M3/M4/M5 timelines) | Task 4 |
| pytest trace harness + --scenario option | Task 5 |
| Veto event tracking | Task 3 Step 10 + Task 4 |
| SOP: Python edit → restart → test | Captured in Task 1 Step 4 (smoke test) |

**No placeholders found.** All steps have exact code.

**Type consistency confirmed:** `DebugTraceWriter.record()` signature used consistently in all `_on_*` callbacks. `_TRACE_FILE` monkeypatching uses the same module-level variable in test and implementation. `_tail_jsonl` is the single function used by all three endpoints.
