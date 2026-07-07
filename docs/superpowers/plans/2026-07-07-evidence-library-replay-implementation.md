# Evidence Library Replay Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the first working Screen 04 Evidence Library vertical slice: data entry from frontend runs and background probe folders, SQLite ingest, session selection, and time-axis replay from indexed evidence.

**Architecture:** Add a backend `evidence_library` package that owns machine config, SQLite schema, root scanning, ingest, replay queries, and decision-frame queries. Keep existing `/api/v1/evidence/session/*` lifecycle routes compatible; after frontend finalize, enqueue ingest into the new index and route Screen 04 to `#/evaluator/{evidence_id}`. Replace Screen 04 mock replay data with `GET /api/v1/evidence-library/.../replay` and synchronize chart, timeline, ledger, and chain inspector through one `currentTimeSec`.

**Tech Stack:** FastAPI, Python stdlib `sqlite3`, pytest/httpx ASGI tests, React, RTK Query, Zustand route state, Vitest/Testing Library.

## Global Constraints

- New Screen 04 library/query endpoints live under `/api/v1/evidence-library`.
- Existing evidence session lifecycle endpoints under `/api/v1/evidence/session/*` remain compatible and are not replaced.
- `evidence_id = sha256(root_id + canonical_resolved_session_path)`.
- `session_id` remains stored for display and compatibility.
- Gate values are imported from existing artifacts. They are not recomputed.
- If a mapped source is absent, store `UNKNOWN`; do not synthesize a PASS.
- Frontend simulation finalization may call the new ingest service after the existing finalize path succeeds.
- Screen 04 queries SQLite by default. It must not scan all `trace_eval` folders on every page load.
- Machine-local override path is `~/.config/mass-l3/evidence_library.json`.
- Machine-local SQLite path is `~/.config/mass-l3/evidence_index.sqlite`.
- Use `MASS_L3_CONFIG_HOME` when set.
- Effective runtime retention policy is `keep` for this plan; `compress_after_ingest` remains configured but inactive until gzip-compatible readers and restore/export are implemented.
- Artifact serving must be path-safe and only serve indexed artifacts.
- External roots default to `trusted=false`, `allow_retention_mutation=false`, and `follow_symlinks=false`.
- The default replay view must prioritize chart playback.
- The full chain inspector opens when user clicks a FAILED/WARN module, a gate, or an event mark.

---

## Current Design Answer

Yes. Current Spec supports both:

- Data entry:
  - frontend runs enter through existing `/api/v1/evidence/session/start` and `/api/v1/evidence/session/{session_id}/finalize`, then new ingest writes SQLite.
  - background probes enter through configured roots and `POST /api/v1/evidence-library/rescan`.
  - roots include `{repo_root}/runs/trace_eval` and `{repo_root}/.worktrees/*/runs/trace_eval`.
- Timeline playback:
  - replay data comes from `GET /api/v1/evidence-library/sessions/{evidence_id}/scenarios/{scenario_id}/replay`.
  - trajectory chart reads `trajectory_downsample`.
  - timeline reads indexed `events`.
  - chain inspector reads `decision_frame(evidence_id, scenario_id, sim_t)`.

Current code has UI shell only. `SimulationEvaluator.tsx` owns `currentTimeSec`, and `TrajectoryReplay` plus `TimelineSixLane` already accept scrub/play state, but they still use mock trajectory and live-last-run ASDR/scoring endpoints. This plan wires real indexed evidence into that shell.

## File Structure

Backend files:

- Create `src/sil_orchestrator/evidence_library/__init__.py`: package exports.
- Create `src/sil_orchestrator/evidence_library/config.py`: machine config loading, default config, root expansion.
- Create `src/sil_orchestrator/evidence_library/store.py`: SQLite connection, schema, row helpers.
- Create `src/sil_orchestrator/evidence_library/ingest.py`: manifest discovery, artifact parsing, JSONL/JSONL.gz ingest, gate import, event/state extraction.
- Create `src/sil_orchestrator/evidence_library/service.py`: application service functions used by routes and existing finalize path.
- Create `src/sil_orchestrator/evidence_library/routes.py`: `/api/v1/evidence-library` FastAPI router.
- Modify `src/sil_orchestrator/main.py`: include new router.
- Modify `src/sil_orchestrator/evidence_routes.py`: after successful finalize, enqueue ingest and return `evidence_id` when available.
- Create `src/sil_orchestrator/tests/test_evidence_library_config_store.py`: config/schema tests.
- Create `src/sil_orchestrator/tests/test_evidence_library_ingest.py`: ingest/replay/decision-frame tests.
- Create `src/sil_orchestrator/tests/test_evidence_library_routes.py`: route tests.
- Modify `src/sil_orchestrator/tests/test_evidence_routes.py`: frontend finalize returns `evidence_id` without breaking old response shape.

Frontend files:

- Modify `web/src/api/silApi.ts`: evidence-library types and RTK endpoints.
- Modify `web/src/App.tsx`: pass optional `evidenceId` to `SimulationEvaluator`.
- Modify `web/src/store/scenarioStore.ts`: store current `evidenceId` returned by finalize.
- Modify `web/src/screens/SimulationEvaluator.tsx`: render Evidence Library when no bound evidence id; render Replay Detail when bound.
- Create `web/src/screens/evaluator/EvidenceLibraryView.tsx`: filterable session list, rescan, settings summary.
- Create `web/src/screens/evaluator/ReplayDetailView.tsx`: data-driven replay shell.
- Create `web/src/screens/evaluator/ChainInspector.tsx`: time-aligned module/gate drilldown.
- Modify `web/src/screens/shared/TrajectoryReplay.tsx`: accept real vessel trajectory data and event marks.
- Modify `web/src/screens/shared/TimelineSixLane.tsx`: accept selected event callback.
- Modify `web/src/screens/__tests__/SimulationEvaluator.test.tsx`: route-state behavior tests.
- Create `web/src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx`: library selection tests.
- Create `web/src/screens/evaluator/__tests__/ReplayDetailView.test.tsx`: timeline playback tests.

## Task 1: Backend Config And SQLite Foundation

**Files:**
- Create: `src/sil_orchestrator/evidence_library/__init__.py`
- Create: `src/sil_orchestrator/evidence_library/config.py`
- Create: `src/sil_orchestrator/evidence_library/store.py`
- Test: `src/sil_orchestrator/tests/test_evidence_library_config_store.py`

**Interfaces:**
- Produces: `EvidenceLibraryConfig`, `EvidenceRootConfig`, `load_effective_config(repo_root: Path | None = None) -> EvidenceLibraryConfig`
- Produces: `open_database(config: EvidenceLibraryConfig) -> sqlite3.Connection`
- Produces: `initialize_schema(conn: sqlite3.Connection) -> None`
- Produces: `compute_evidence_id(root_id: str, session_path: Path) -> str`

- [ ] **Step 1: Write failing config/store tests**

Create `src/sil_orchestrator/tests/test_evidence_library_config_store.py`:

```python
from __future__ import annotations

import sqlite3
from pathlib import Path

from sil_orchestrator.evidence_library.config import load_effective_config
from sil_orchestrator.evidence_library.store import (
    compute_evidence_id,
    initialize_schema,
    open_database,
)


def test_load_effective_config_uses_mass_l3_config_home(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    repo.mkdir()
    config_home = tmp_path / "config-home"
    monkeypatch.setenv("MASS_L3_CONFIG_HOME", str(config_home))

    config = load_effective_config(repo_root=repo)

    assert config.config_home == config_home
    assert config.database_path == config_home / "evidence_index.sqlite"
    assert [root.root_id for root in config.roots] == ["primary", "worktrees"]
    assert config.roots[0].path_glob == str(repo / "runs" / "trace_eval")
    assert config.roots[1].path_glob == str(repo / ".worktrees" / "*" / "runs" / "trace_eval")
    assert config.raw_trace_policy == "compress_after_ingest"
    assert config.effective_retention_policy == "keep"


def test_schema_has_replay_tables(tmp_path, monkeypatch):
    config_home = tmp_path / "config-home"
    monkeypatch.setenv("MASS_L3_CONFIG_HOME", str(config_home))
    config = load_effective_config(repo_root=tmp_path / "repo")

    conn = open_database(config)
    initialize_schema(conn)

    rows = conn.execute(
        "select name from sqlite_master where type='table' order by name"
    ).fetchall()
    names = {row[0] for row in rows}
    assert {
        "artifacts",
        "events",
        "gate_results",
        "roots",
        "scenarios",
        "sessions",
        "state_segments",
        "trajectory_downsample",
        "trajectory_samples",
    }.issubset(names)


def test_compute_evidence_id_uses_root_and_resolved_path(tmp_path):
    session_dir = tmp_path / "runs" / "trace_eval" / "same-session"
    session_dir.mkdir(parents=True)

    first = compute_evidence_id("primary", session_dir)
    second = compute_evidence_id("worktree-a", session_dir)

    assert len(first) == 64
    assert len(second) == 64
    assert first != second
```

- [ ] **Step 2: Run tests and confirm failure**

Run:

```bash
pytest src/sil_orchestrator/tests/test_evidence_library_config_store.py -q
```

Expected: FAIL with `ModuleNotFoundError: No module named 'sil_orchestrator.evidence_library'`.

- [ ] **Step 3: Add config and store implementation**

Create `src/sil_orchestrator/evidence_library/__init__.py`:

```python
"""Evidence Library backend for Screen 04 replay and probe evidence indexing."""
```

Create `src/sil_orchestrator/evidence_library/config.py`:

```python
from __future__ import annotations

import json
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Any


@dataclass(frozen=True)
class EvidenceRootConfig:
    root_id: str
    label: str
    source: str
    path_glob: str
    enabled: bool = True
    trusted: bool = False
    allow_retention_mutation: bool = False
    follow_symlinks: bool = False


@dataclass(frozen=True)
class EvidenceLibraryConfig:
    config_home: Path
    database_path: Path
    roots: list[EvidenceRootConfig]
    raw_trace_policy: str
    effective_retention_policy: str


def _repo_root(repo_root: Path | None) -> Path:
    if repo_root is not None:
        return repo_root.resolve()
    return Path(__file__).resolve().parents[3]


def _config_home() -> Path:
    explicit = os.getenv("MASS_L3_CONFIG_HOME")
    if explicit:
        return Path(explicit).expanduser()
    return Path.home() / ".config" / "mass-l3"


def _default_config(repo_root: Path) -> dict[str, Any]:
    return {
        "database_path": "{config_home}/evidence_index.sqlite",
        "raw_trace_policy": "compress_after_ingest",
        "roots": [
            {
                "root_id": "primary",
                "label": "Primary checkout trace_eval",
                "source": "background_probe",
                "path_glob": "{repo_root}/runs/trace_eval",
                "enabled": True,
                "trusted": True,
                "allow_retention_mutation": False,
                "follow_symlinks": False,
            },
            {
                "root_id": "worktrees",
                "label": "Worktree trace_eval folders",
                "source": "background_probe",
                "path_glob": "{repo_root}/.worktrees/*/runs/trace_eval",
                "enabled": True,
                "trusted": True,
                "allow_retention_mutation": False,
                "follow_symlinks": False,
            },
        ],
    }


def _merge_machine_override(base: dict[str, Any], machine_path: Path) -> dict[str, Any]:
    if not machine_path.exists():
        return base
    raw = json.loads(machine_path.read_text())
    merged = dict(base)
    for key, value in raw.items():
        if key == "roots":
            merged["roots"] = value
        else:
            merged[key] = value
    return merged


def _expand(value: str, *, repo_root: Path, config_home: Path) -> str:
    return (
        value.replace("{repo_root}", str(repo_root))
        .replace("{config_home}", str(config_home))
    )


def load_effective_config(repo_root: Path | None = None) -> EvidenceLibraryConfig:
    repo = _repo_root(repo_root)
    home = _config_home()
    base = _default_config(repo)
    merged = _merge_machine_override(base, home / "evidence_library.json")
    database_path = Path(_expand(str(merged["database_path"]), repo_root=repo, config_home=home)).expanduser()
    roots = [
        EvidenceRootConfig(
            root_id=str(item["root_id"]),
            label=str(item.get("label") or item["root_id"]),
            source=str(item.get("source") or "background_probe"),
            path_glob=_expand(str(item["path_glob"]), repo_root=repo, config_home=home),
            enabled=bool(item.get("enabled", True)),
            trusted=bool(item.get("trusted", False)),
            allow_retention_mutation=bool(item.get("allow_retention_mutation", False)),
            follow_symlinks=bool(item.get("follow_symlinks", False)),
        )
        for item in merged.get("roots", [])
    ]
    raw_trace_policy = str(merged.get("raw_trace_policy", "compress_after_ingest"))
    return EvidenceLibraryConfig(
        config_home=home,
        database_path=database_path,
        roots=roots,
        raw_trace_policy=raw_trace_policy,
        effective_retention_policy="keep",
    )
```

Create `src/sil_orchestrator/evidence_library/store.py`:

```python
from __future__ import annotations

import hashlib
import sqlite3
from pathlib import Path

from .config import EvidenceLibraryConfig


SCHEMA_SQL = """
create table if not exists roots (
  root_id text primary key,
  label text not null,
  source text not null,
  path_glob text not null,
  enabled integer not null,
  trusted integer not null,
  allow_retention_mutation integer not null,
  follow_symlinks integer not null,
  available integer not null default 0,
  stale integer not null default 0,
  last_rescan_at text
);
create table if not exists sessions (
  evidence_id text primary key,
  session_id text not null,
  source text not null,
  suite text not null,
  root_id text not null,
  worktree_name text,
  branch text,
  session_path text not null,
  created_at text,
  ended_at text,
  status text,
  valid_data integer not null default 0,
  scenario_count integer not null default 0,
  ingest_status text not null,
  ingest_error text,
  raw_trace_policy text not null,
  latest_mtime real not null default 0
);
create table if not exists scenarios (
  evidence_id text not null,
  session_id text not null,
  scenario_id text not null,
  run_id text,
  verdict text,
  overall_pass integer,
  first_failure text,
  first_failed_gate text,
  first_failed_module text,
  role text,
  cpa_floor_m real,
  min_cpa_m real,
  min_cpa_nm real,
  returned_to_route integer,
  route_return_required integer,
  route_corridor_ok integer,
  stability_pass integer,
  compliance_verdict text,
  primary key (evidence_id, scenario_id)
);
create table if not exists artifacts (
  artifact_id integer primary key autoincrement,
  evidence_id text not null,
  session_id text not null,
  scenario_id text,
  kind text not null,
  path text not null,
  relative_path text not null,
  sha256 text,
  mtime real,
  compressed integer not null default 0,
  available integer not null default 1
);
create table if not exists trajectory_samples (
  evidence_id text not null,
  session_id text not null,
  scenario_id text not null,
  vessel_id text not null,
  vessel_role text not null,
  sim_t real not null,
  wall_t real,
  lat real,
  lon real,
  heading_deg real,
  sog_kn real,
  rot_deg_s real,
  source_topic text,
  sample_seq integer not null,
  primary key (evidence_id, scenario_id, vessel_id, sim_t, sample_seq)
);
create index if not exists idx_trajectory_window on trajectory_samples(evidence_id, scenario_id, sim_t);
create index if not exists idx_trajectory_vessel on trajectory_samples(evidence_id, scenario_id, vessel_id, sim_t);
create table if not exists trajectory_downsample (
  evidence_id text not null,
  session_id text not null,
  scenario_id text not null,
  vessel_id text not null,
  vessel_role text not null,
  sim_t real not null,
  wall_t real,
  lat real,
  lon real,
  heading_deg real,
  sog_kn real,
  rot_deg_s real,
  source_topic text,
  sample_seq integer not null,
  level integer not null,
  primary key (evidence_id, scenario_id, vessel_id, level, sim_t, sample_seq)
);
create table if not exists state_segments (
  evidence_id text not null,
  session_id text not null,
  scenario_id text not null,
  module text not null,
  field text not null,
  start_t real not null,
  end_t real not null,
  value_json text not null,
  source_topic text
);
create table if not exists events (
  event_id integer primary key autoincrement,
  evidence_id text not null,
  session_id text not null,
  scenario_id text not null,
  sim_t real not null,
  wall_t real,
  module text not null,
  event_type text not null,
  severity text not null,
  payload_json text not null,
  source_topic text
);
create index if not exists idx_events_time on events(evidence_id, scenario_id, sim_t);
create table if not exists gate_results (
  evidence_id text not null,
  session_id text not null,
  scenario_id text not null,
  gate_id text not null,
  status text not null,
  temporal_scope text not null,
  precedence_rank integer not null,
  conflict_group text,
  payload_json text not null,
  source text not null
);
"""


def compute_evidence_id(root_id: str, session_path: Path) -> str:
    canonical = session_path.resolve()
    return hashlib.sha256(f"{root_id}:{canonical}".encode("utf-8")).hexdigest()


def open_database(config: EvidenceLibraryConfig) -> sqlite3.Connection:
    config.database_path.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(config.database_path)
    conn.row_factory = sqlite3.Row
    conn.execute("pragma foreign_keys = on")
    return conn


def initialize_schema(conn: sqlite3.Connection) -> None:
    conn.executescript(SCHEMA_SQL)
    conn.commit()
```

- [ ] **Step 4: Run tests and confirm pass**

Run:

```bash
pytest src/sil_orchestrator/tests/test_evidence_library_config_store.py -q
```

Expected: `3 passed`.

- [ ] **Step 5: Commit Task 1**

```bash
git add src/sil_orchestrator/evidence_library/__init__.py src/sil_orchestrator/evidence_library/config.py src/sil_orchestrator/evidence_library/store.py src/sil_orchestrator/tests/test_evidence_library_config_store.py
git commit -m "feat(evidence): add library config and schema"
```

## Task 2: Ingest Existing Probe Evidence Into SQLite

**Files:**
- Create: `src/sil_orchestrator/evidence_library/ingest.py`
- Test: `src/sil_orchestrator/tests/test_evidence_library_ingest.py`

**Interfaces:**
- Consumes: `EvidenceRootConfig`, `EvidenceLibraryConfig`, `compute_evidence_id`
- Produces: `IngestResult(evidence_id: str, session_id: str, scenario_count: int, trajectory_count: int, event_count: int)`
- Produces: `ingest_session(conn: sqlite3.Connection, root: EvidenceRootConfig, session_path: Path, raw_trace_policy: str = "keep", force: bool = False) -> IngestResult`
- Produces: `query_replay(conn: sqlite3.Connection, evidence_id: str, scenario_id: str) -> dict[str, Any]`
- Produces: `query_decision_frame(conn: sqlite3.Connection, evidence_id: str, scenario_id: str, sim_t: float) -> dict[str, Any]`

- [ ] **Step 1: Write failing ingest/replay tests**

Create `src/sil_orchestrator/tests/test_evidence_library_ingest.py`:

```python
from __future__ import annotations

import json
import sqlite3
from pathlib import Path

from sil_orchestrator.evidence_library.config import EvidenceRootConfig
from sil_orchestrator.evidence_library.ingest import (
    ingest_session,
    query_decision_frame,
    query_replay,
)
from sil_orchestrator.evidence_library.store import initialize_schema


def _write_fixture_session(root: Path, session_name: str = "20260707_132000_single_colreg-rule14-ho") -> Path:
    session = root / session_name
    session.mkdir(parents=True)
    manifest = {
        "session_name": session_name,
        "source": "frontend",
        "suite": "single",
        "created_at": "2026-07-07T13:20:00Z",
        "ended_at": "2026-07-07T13:25:00Z",
        "status": "completed",
        "valid_data": True,
        "scenarios": [
            {
                "scenario_id": "colreg-rule14-ho",
                "run_id": "run-test",
                "status": "completed",
                "valid_data": True,
                "trace_path": "colreg-rule14-ho.trace_current.jsonl",
                "report_path": "colreg-rule14-ho.json",
                "png_path": "colreg-rule14-ho_trajectory_dashboard.png",
            }
        ],
    }
    (session / "manifest.json").write_text(json.dumps(manifest))
    report = {
        "verdict": {"overall_pass": False},
        "layers": {
            "L4_colregs_compliance": {"passed": False, "reason": "phase semantic failed"},
            "L7_stability": {"passed": True},
        },
        "kpis": {"min_cpa_m": 450.0, "min_cpa_nm": 0.243},
    }
    (session / "colreg-rule14-ho.json").write_text(json.dumps(report))
    batch = {
        "results": [
            {
                "scenario": "colreg-rule14-ho",
                "overall_pass": False,
                "cpa_ok": False,
                "stability_pass": True,
                "returned_to_route": False,
                "route_corridor_ok": True,
                "compliance_verdict": "FAIL",
                "phase_semantics": {"phase_semantics_ok": False},
                "domain_gates": {"risk_gate_ok": False, "seamanship_gate_ok": True},
            }
        ]
    }
    (session / "batch_summary.json").write_text(json.dumps(batch))
    (session / "colreg-rule14-ho.artifact_consistency.json").write_text(json.dumps({"g_art_ok": True}))
    (session / "colreg-rule14-ho_trajectory_dashboard.png").write_bytes(b"png")
    rows = [
        {"sim_t": 0.0, "wall_t": 10.0, "topic": "/sil/own_ship_state", "lat": 0.0, "lon": 0.0, "heading_deg": 0.0, "sog_kn": 8.0},
        {"sim_t": 1.0, "wall_t": 11.0, "topic": "/l3/m2/world_state", "primary_target_id": "T01", "target_id": "T01", "target_lat": 0.01, "target_lon": 0.0, "cpa_m": 450.0, "tcpa_s": 127.0, "confidence": 0.9},
        {"sim_t": 2.0, "wall_t": 12.0, "topic": "/l3/m6/colregs", "rule": "Rule14", "role": "give_way", "preferred_direction": "starboard", "phase": "active", "release_predicted": False},
        {"sim_t": 3.0, "wall_t": 13.0, "topic": "/l3/m5/trajectory", "solver_status": "VALID", "plan_status": "NORMAL", "route_hash": "abc", "waypoint_count": 4},
        {"sim_t": 4.0, "wall_t": 14.0, "topic": "/l4/guidance", "execution_state": "DEFERRED", "reason": "avoidance_active"},
        {"sim_t": 5.0, "wall_t": 15.0, "topic": "/l3/asdr/record", "module": "M5", "event_type": "PLAN_READY", "severity": "info", "payload": {"plan_id": "abc"}},
    ]
    with (session / "colreg-rule14-ho.trace_current.jsonl").open("w") as f:
        for row in rows:
            f.write(json.dumps(row) + "\n")
    return session


def _conn() -> sqlite3.Connection:
    conn = sqlite3.connect(":memory:")
    conn.row_factory = sqlite3.Row
    initialize_schema(conn)
    return conn


def test_ingest_session_builds_replay_and_gate_rows(tmp_path):
    root_path = tmp_path / "runs" / "trace_eval"
    session = _write_fixture_session(root_path)
    root = EvidenceRootConfig(
        root_id="primary",
        label="Primary",
        source="background_probe",
        path_glob=str(root_path),
        trusted=True,
    )
    conn = _conn()

    result = ingest_session(conn, root, session)

    assert result.session_id == session.name
    assert result.scenario_count == 1
    assert result.trajectory_count >= 2
    assert result.event_count >= 1
    replay = query_replay(conn, result.evidence_id, "colreg-rule14-ho")
    assert replay["session"]["evidence_id"] == result.evidence_id
    assert replay["scenario"]["scenario_id"] == "colreg-rule14-ho"
    assert replay["scenario"]["overall_pass"] is False
    assert replay["duration_s"] == 5.0
    assert replay["trajectory"][0]["vessel_id"] == "OWN"
    assert replay["events"][0]["event_type"] in {"PLAN_READY", "GATE_RESULT"}
    gates = {gate["gate_id"]: gate["status"] for gate in replay["gates"]}
    assert gates["G-SEM"] == "FAIL"
    assert gates["G-ART"] == "PASS"


def test_decision_frame_returns_time_aligned_module_facts(tmp_path):
    root_path = tmp_path / "runs" / "trace_eval"
    session = _write_fixture_session(root_path)
    root = EvidenceRootConfig(root_id="primary", label="Primary", source="background_probe", path_glob=str(root_path), trusted=True)
    conn = _conn()
    result = ingest_session(conn, root, session)

    frame = query_decision_frame(conn, result.evidence_id, "colreg-rule14-ho", 3.5)

    assert frame["chain"]["M2"]["facts"]["primary_target_id"] == "T01"
    assert frame["chain"]["M6"]["facts"]["rule"] == "Rule14"
    assert frame["chain"]["M5"]["facts"]["solver_status"] == "VALID"
    assert frame["chain"]["L4"]["facts"] == {}
    assert frame["gates"][0]["temporal_scope"] in {"final_run_verdict", "artifact_consistency"}
```

- [ ] **Step 2: Run tests and confirm failure**

Run:

```bash
pytest src/sil_orchestrator/tests/test_evidence_library_ingest.py -q
```

Expected: FAIL with `ModuleNotFoundError: No module named 'sil_orchestrator.evidence_library.ingest'`.

- [ ] **Step 3: Implement ingestion, replay, and decision-frame query**

Create `src/sil_orchestrator/evidence_library/ingest.py` with these definitions and exact behavior:

```python
from __future__ import annotations

import gzip
import json
import sqlite3
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

from .config import EvidenceRootConfig
from .store import compute_evidence_id


@dataclass(frozen=True)
class IngestResult:
    evidence_id: str
    session_id: str
    scenario_count: int
    trajectory_count: int
    event_count: int


def _read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text())


def _iter_jsonl(path: Path) -> Iterable[tuple[int, dict[str, Any]]]:
    opener = gzip.open if path.suffix == ".gz" else open
    with opener(path, "rt") as f:
        for index, line in enumerate(f):
            text = line.strip()
            if text:
                yield index, json.loads(text)


def _sha256(path: Path) -> str:
    import hashlib

    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def _bool_status(value: Any) -> str:
    if value is True:
        return "PASS"
    if value is False:
        return "FAIL"
    return "UNKNOWN"


def _source_status(payload: dict[str, Any]) -> str:
    if "passed" in payload:
        return _bool_status(payload.get("passed"))
    if "overall_pass" in payload:
        return _bool_status(payload.get("overall_pass"))
    return "UNKNOWN"


def _scenario_from_batch(batch: dict[str, Any], scenario_id: str) -> dict[str, Any]:
    rows = batch.get("results") or batch.get("scenarios") or []
    for row in rows:
        if row.get("scenario") == scenario_id or row.get("scenario_id") == scenario_id:
            return row
    return {}


def _gate_rows(
    evidence_id: str,
    session_id: str,
    scenario_id: str,
    report: dict[str, Any],
    batch_row: dict[str, Any],
    artifact_consistency: dict[str, Any],
) -> list[tuple[Any, ...]]:
    rows: list[tuple[Any, ...]] = []

    def add(gate_id: str, status: str, temporal_scope: str, rank: int, payload: dict[str, Any], source: str) -> None:
        rows.append((
            evidence_id,
            session_id,
            scenario_id,
            gate_id,
            status,
            temporal_scope,
            rank,
            None,
            json.dumps(payload, sort_keys=True),
            source,
        ))

    layer_map = {
        "L1_scenario_validity": "G-SCN",
        "L2_safety_floor": "G-SEP",
        "L3_dynamic_risk": "G-SEP",
        "L4_colregs_compliance": "G-SEM",
        "L5_route_recovery": "G-REL",
        "L6_seamanship": "G-REL",
        "L7_stability": "G-ACT",
    }
    for layer_id, payload in (report.get("layers") or {}).items():
        add(layer_map.get(layer_id, layer_id), _source_status(payload), "final_run_verdict", 20, payload, "TraceEvaluationReport")

    if batch_row:
        add("G-SEP", _bool_status(batch_row.get("cpa_ok")), "final_run_verdict", 10, batch_row, "batch_summary")
        phase = batch_row.get("phase_semantics") or {}
        add("G-SEM", _bool_status(phase.get("phase_semantics_ok")), "final_run_verdict", 10, batch_row, "batch_summary")
        add("G-ACT", _bool_status(batch_row.get("stability_pass")), "final_run_verdict", 10, batch_row, "batch_summary")
        seamanship = (batch_row.get("domain_gates") or {}).get("seamanship_gate_ok")
        add("G-REL", _bool_status(seamanship if seamanship is not None else batch_row.get("returned_to_route")), "final_run_verdict", 10, batch_row, "batch_summary")

    if artifact_consistency:
        add("G-ART", _bool_status(artifact_consistency.get("g_art_ok")), "artifact_consistency", 5, artifact_consistency, "artifact_consistency")
    else:
        add("G-ART", "UNKNOWN", "artifact_consistency", 5, {}, "artifact_consistency")
    return rows


def _insert_artifact(conn: sqlite3.Connection, evidence_id: str, session_id: str, scenario_id: str | None, kind: str, session_path: Path, path: Path) -> None:
    if not path.exists():
        return
    conn.execute(
        """
        insert into artifacts(evidence_id, session_id, scenario_id, kind, path, relative_path, sha256, mtime, compressed, available)
        values (?, ?, ?, ?, ?, ?, ?, ?, ?, 1)
        """,
        (
            evidence_id,
            session_id,
            scenario_id,
            kind,
            str(path.resolve()),
            str(path.relative_to(session_path)),
            _sha256(path),
            path.stat().st_mtime,
            1 if path.suffix == ".gz" else 0,
        ),
    )


def _trajectory_rows(evidence_id: str, session_id: str, scenario_id: str, trace_path: Path) -> tuple[list[tuple[Any, ...]], list[tuple[Any, ...]], list[tuple[Any, ...]]]:
    trajectory: list[tuple[Any, ...]] = []
    events: list[tuple[Any, ...]] = []
    states: list[tuple[Any, ...]] = []
    last_state: dict[tuple[str, str], tuple[float, Any, str]] = {}
    final_t = 0.0

    def close_state(key: tuple[str, str], end_t: float) -> None:
        start_t, value, topic = last_state[key]
        states.append((evidence_id, session_id, scenario_id, key[0], key[1], start_t, end_t, json.dumps(value, sort_keys=True), topic))

    def set_state(module: str, field: str, sim_t: float, value: Any, topic: str) -> None:
        key = (module, field)
        previous = last_state.get(key)
        if previous is not None and previous[1] == value:
            return
        if previous is not None:
            close_state(key, sim_t)
        last_state[key] = (sim_t, value, topic)

    for seq, row in _iter_jsonl(trace_path):
        topic = str(row.get("topic") or row.get("source_topic") or "")
        sim_t = float(row.get("sim_t", row.get("t", 0.0)) or 0.0)
        wall_t = row.get("wall_t")
        final_t = max(final_t, sim_t)
        if topic == "/sil/own_ship_state":
            trajectory.append((evidence_id, session_id, scenario_id, "OWN", "ownship", sim_t, wall_t, row.get("lat"), row.get("lon"), row.get("heading_deg"), row.get("sog_kn"), row.get("rot_deg_s"), topic, seq))
        if topic == "/l3/m2/world_state":
            target_id = str(row.get("target_id") or row.get("primary_target_id") or "T01")
            trajectory.append((evidence_id, session_id, scenario_id, target_id, "target", sim_t, wall_t, row.get("target_lat"), row.get("target_lon"), row.get("target_heading_deg"), row.get("target_sog_kn"), None, topic, seq))
            for field in ("primary_target_id", "cpa_m", "tcpa_s", "confidence"):
                if field in row:
                    set_state("M2", field, sim_t, row[field], topic)
        if topic == "/l3/m6/colregs":
            for field in ("rule", "role", "preferred_direction", "phase", "release_predicted"):
                if field in row:
                    set_state("M6", field, sim_t, row[field], topic)
        if topic == "/l3/m5/trajectory":
            for field in ("solver_status", "plan_status", "route_hash", "waypoint_count"):
                if field in row:
                    set_state("M5", field, sim_t, row[field], topic)
        if topic == "/l4/guidance":
            for field in ("execution_state", "accepted", "rejected", "degraded", "reason"):
                if field in row:
                    set_state("L4", field, sim_t, row[field], topic)
        if topic == "/l3/asdr/record":
            events.append((
                evidence_id,
                session_id,
                scenario_id,
                sim_t,
                wall_t,
                str(row.get("module") or "ASDR"),
                str(row.get("event_type") or row.get("k") or "ASDR"),
                str(row.get("severity") or "info"),
                json.dumps(row.get("payload") or row, sort_keys=True),
                topic,
            ))
    for key in list(last_state):
        close_state(key, final_t)
    return trajectory, events, states


def ingest_session(conn: sqlite3.Connection, root: EvidenceRootConfig, session_path: Path, raw_trace_policy: str = "keep", force: bool = False) -> IngestResult:
    session_path = session_path.resolve()
    manifest = _read_json(session_path / "manifest.json")
    session_id = str(manifest.get("session_name") or session_path.name)
    evidence_id = compute_evidence_id(root.root_id, session_path)
    scenarios = manifest.get("scenarios") or []
    latest_mtime = max((p.stat().st_mtime for p in session_path.glob("*") if p.is_file()), default=(session_path / "manifest.json").stat().st_mtime)
    batch_path = session_path / "batch_summary.json"
    batch = _read_json(batch_path) if batch_path.exists() else {}

    with conn:
        for table in ("scenarios", "artifacts", "trajectory_samples", "trajectory_downsample", "state_segments", "events", "gate_results"):
            conn.execute(f"delete from {table} where evidence_id = ?", (evidence_id,))
        conn.execute(
            """
            insert or replace into sessions(
              evidence_id, session_id, source, suite, root_id, worktree_name, branch, session_path,
              created_at, ended_at, status, valid_data, scenario_count, ingest_status, ingest_error,
              raw_trace_policy, latest_mtime
            ) values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 'ok', null, ?, ?)
            """,
            (
                evidence_id,
                session_id,
                str(manifest.get("source") or root.source),
                str(manifest.get("suite") or "unknown"),
                root.root_id,
                None,
                None,
                str(session_path),
                manifest.get("created_at"),
                manifest.get("ended_at"),
                manifest.get("status"),
                1 if manifest.get("valid_data") else 0,
                len(scenarios),
                raw_trace_policy,
                latest_mtime,
            ),
        )
        _insert_artifact(conn, evidence_id, session_id, None, "manifest", session_path, session_path / "manifest.json")
        _insert_artifact(conn, evidence_id, session_id, None, "batch_summary", session_path, batch_path)

        trajectory_count = 0
        event_count = 0
        for scenario in scenarios:
            scenario_id = str(scenario["scenario_id"])
            report_path = session_path / str(scenario.get("report_path") or f"{scenario_id}.json")
            trace_path = session_path / str(scenario.get("trace_path") or f"{scenario_id}.trace_current.jsonl")
            if not trace_path.exists() and (trace_path.with_suffix(trace_path.suffix + ".gz")).exists():
                trace_path = trace_path.with_suffix(trace_path.suffix + ".gz")
            art_path = session_path / f"{scenario_id}.artifact_consistency.json"
            report = _read_json(report_path) if report_path.exists() else {}
            art = _read_json(art_path) if art_path.exists() else {}
            batch_row = _scenario_from_batch(batch, scenario_id)
            overall = batch_row.get("overall_pass")
            if overall is None:
                overall = (report.get("verdict") or {}).get("overall_pass")
            conn.execute(
                """
                insert into scenarios(evidence_id, session_id, scenario_id, run_id, verdict, overall_pass, first_failure, first_failed_gate, first_failed_module, role, cpa_floor_m, min_cpa_m, min_cpa_nm, returned_to_route, route_return_required, route_corridor_ok, stability_pass, compliance_verdict)
                values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    evidence_id,
                    session_id,
                    scenario_id,
                    scenario.get("run_id"),
                    "PASS" if overall is True else "FAIL" if overall is False else "UNKNOWN",
                    1 if overall is True else 0 if overall is False else None,
                    None,
                    None,
                    None,
                    batch_row.get("role"),
                    batch_row.get("cpa_floor_m"),
                    (report.get("kpis") or {}).get("min_cpa_m") or batch_row.get("min_cpa_m"),
                    (report.get("kpis") or {}).get("min_cpa_nm") or batch_row.get("min_cpa_nm"),
                    1 if batch_row.get("returned_to_route") is True else 0 if batch_row.get("returned_to_route") is False else None,
                    1 if batch_row.get("route_return_required") is True else 0 if batch_row.get("route_return_required") is False else None,
                    1 if batch_row.get("route_corridor_ok") is True else 0 if batch_row.get("route_corridor_ok") is False else None,
                    1 if batch_row.get("stability_pass") is True else 0 if batch_row.get("stability_pass") is False else None,
                    batch_row.get("compliance_verdict"),
                ),
            )
            _insert_artifact(conn, evidence_id, session_id, scenario_id, "trace_report", session_path, report_path)
            _insert_artifact(conn, evidence_id, session_id, scenario_id, "trace_jsonl_gz" if trace_path.suffix == ".gz" else "trace_jsonl", session_path, trace_path)
            _insert_artifact(conn, evidence_id, session_id, scenario_id, "artifact_consistency", session_path, art_path)
            _insert_artifact(conn, evidence_id, session_id, scenario_id, "trajectory_dashboard_png", session_path, session_path / str(scenario.get("png_path") or f"{scenario_id}_trajectory_dashboard.png"))
            if trace_path.exists():
                trajectory, events, states = _trajectory_rows(evidence_id, session_id, scenario_id, trace_path)
                conn.executemany("insert into trajectory_samples values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", trajectory)
                downsample = [row + (0,) for index, row in enumerate(trajectory) if index % 1 == 0]
                conn.executemany("insert into trajectory_downsample values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", downsample)
                conn.executemany("insert into events(evidence_id, session_id, scenario_id, sim_t, wall_t, module, event_type, severity, payload_json, source_topic) values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", events)
                conn.executemany("insert into state_segments values (?, ?, ?, ?, ?, ?, ?, ?, ?)", states)
                trajectory_count += len(trajectory)
                event_count += len(events)
            gates = _gate_rows(evidence_id, session_id, scenario_id, report, batch_row, art)
            conn.executemany("insert into gate_results values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", gates)
            for gate in gates:
                conn.execute(
                    "insert into events(evidence_id, session_id, scenario_id, sim_t, wall_t, module, event_type, severity, payload_json, source_topic) values (?, ?, ?, 0, null, 'GATE', 'GATE_RESULT', ?, ?, ?)",
                    (evidence_id, session_id, scenario_id, "crit" if gate[4] == "FAIL" else "info", gate[8], gate[9]),
                )
                event_count += 1
    return IngestResult(evidence_id=evidence_id, session_id=session_id, scenario_count=len(scenarios), trajectory_count=trajectory_count, event_count=event_count)


def _rows(conn: sqlite3.Connection, sql: str, args: tuple[Any, ...]) -> list[dict[str, Any]]:
    return [dict(row) for row in conn.execute(sql, args).fetchall()]


def query_replay(conn: sqlite3.Connection, evidence_id: str, scenario_id: str) -> dict[str, Any]:
    session = dict(conn.execute("select * from sessions where evidence_id = ?", (evidence_id,)).fetchone())
    scenario = dict(conn.execute("select * from scenarios where evidence_id = ? and scenario_id = ?", (evidence_id, scenario_id)).fetchone())
    trajectory = _rows(conn, "select * from trajectory_downsample where evidence_id = ? and scenario_id = ? order by sim_t, sample_seq", (evidence_id, scenario_id))
    events = _rows(conn, "select * from events where evidence_id = ? and scenario_id = ? order by sim_t, event_id", (evidence_id, scenario_id))
    gates = _rows(conn, "select * from gate_results where evidence_id = ? and scenario_id = ? order by precedence_rank, gate_id", (evidence_id, scenario_id))
    artifacts = _rows(conn, "select artifact_id, kind, relative_path, available from artifacts where evidence_id = ? order by kind", (evidence_id,))
    duration_s = max([float(row["sim_t"]) for row in trajectory + events] or [0.0])
    return {"session": session, "scenario": scenario, "duration_s": duration_s, "trajectory": trajectory, "events": events, "gates": gates, "artifacts": artifacts}


def query_decision_frame(conn: sqlite3.Connection, evidence_id: str, scenario_id: str, sim_t: float) -> dict[str, Any]:
    modules = ["M2", "M6", "M4", "M5", "L4", "M7"]
    chain = {module: {"status": "UNKNOWN", "status_source": "diagnostic_availability", "facts": {}} for module in modules}
    rows = _rows(
        conn,
        "select * from state_segments where evidence_id = ? and scenario_id = ? and start_t <= ? and end_t >= ? order by module, field",
        (evidence_id, scenario_id, sim_t, sim_t),
    )
    for row in rows:
        module = row["module"]
        if module in chain:
            chain[module]["facts"][row["field"]] = json.loads(row["value_json"])
            chain[module]["status"] = "OK"
    gates = _rows(conn, "select gate_id, status, temporal_scope, payload_json, source from gate_results where evidence_id = ? and scenario_id = ? order by precedence_rank, gate_id", (evidence_id, scenario_id))
    nearby = _rows(conn, "select * from events where evidence_id = ? and scenario_id = ? and sim_t between ? and ? order by sim_t", (evidence_id, scenario_id, sim_t - 5.0, sim_t + 5.0))
    own = conn.execute(
        "select * from trajectory_samples where evidence_id = ? and scenario_id = ? and vessel_id = 'OWN' order by abs(sim_t - ?) limit 1",
        (evidence_id, scenario_id, sim_t),
    ).fetchone()
    own_ship = dict(own) if own else {}
    return {"evidence_id": evidence_id, "scenario_id": scenario_id, "sim_t": sim_t, "own_ship": own_ship, "targets": [], "chain": chain, "gates": gates, "nearby_events": nearby}
```

- [ ] **Step 4: Run tests and confirm pass**

Run:

```bash
pytest src/sil_orchestrator/tests/test_evidence_library_ingest.py -q
```

Expected: `2 passed`.

- [ ] **Step 5: Commit Task 2**

```bash
git add src/sil_orchestrator/evidence_library/ingest.py src/sil_orchestrator/tests/test_evidence_library_ingest.py
git commit -m "feat(evidence): ingest replay evidence into sqlite"
```

## Task 3: Backend Evidence Library Routes And Frontend Finalize Entry

**Files:**
- Create: `src/sil_orchestrator/evidence_library/service.py`
- Create: `src/sil_orchestrator/evidence_library/routes.py`
- Modify: `src/sil_orchestrator/main.py`
- Modify: `src/sil_orchestrator/evidence_routes.py`
- Test: `src/sil_orchestrator/tests/test_evidence_library_routes.py`
- Test: `src/sil_orchestrator/tests/test_evidence_routes.py`

**Interfaces:**
- Consumes: `ingest_session`, `query_replay`, `query_decision_frame`
- Produces: `rescan_all(repo_root: Path | None = None, force: bool = False) -> dict[str, Any]`
- Produces: `ingest_frontend_session(session_dir: Path) -> str | None`
- Produces: FastAPI endpoints under `/api/v1/evidence-library`

- [ ] **Step 1: Write failing route tests**

Create `src/sil_orchestrator/tests/test_evidence_library_routes.py`:

```python
from __future__ import annotations

import json
from pathlib import Path

import pytest
from fastapi import FastAPI
from httpx import ASGITransport, AsyncClient

from sil_orchestrator.evidence_library import routes


def _session(root: Path) -> Path:
    session = root / "20260707_132000_single_colreg-rule14-ho"
    session.mkdir(parents=True)
    (session / "manifest.json").write_text(json.dumps({
        "session_name": session.name,
        "source": "cli",
        "suite": "single",
        "created_at": "2026-07-07T13:20:00Z",
        "status": "completed",
        "valid_data": True,
        "scenarios": [{
            "scenario_id": "colreg-rule14-ho",
            "trace_path": "colreg-rule14-ho.trace_current.jsonl",
            "report_path": "colreg-rule14-ho.json",
            "valid_data": True,
        }],
    }))
    (session / "colreg-rule14-ho.json").write_text(json.dumps({"verdict": {"overall_pass": True}, "layers": {}}))
    (session / "colreg-rule14-ho.trace_current.jsonl").write_text(
        json.dumps({"sim_t": 0, "topic": "/sil/own_ship_state", "lat": 0, "lon": 0}) + "\n"
    )
    return session


@pytest.mark.asyncio
async def test_rescan_sessions_replay_and_decision_frame(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    root = repo / "runs" / "trace_eval"
    _session(root)
    config_home = tmp_path / "config"
    monkeypatch.setenv("MASS_L3_CONFIG_HOME", str(config_home))
    monkeypatch.setattr(routes, "REPO_ROOT", repo)
    app = FastAPI()
    app.include_router(routes.router)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        rescan = await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        assert rescan.status_code == 200
        assert rescan.json()["ingested"] == 1

        listed = await client.get("/api/v1/evidence-library/sessions")
        assert listed.status_code == 200
        evidence_id = listed.json()["sessions"][0]["evidence_id"]

        replay = await client.get(f"/api/v1/evidence-library/sessions/{evidence_id}/scenarios/colreg-rule14-ho/replay")
        assert replay.status_code == 200
        assert replay.json()["duration_s"] == 0.0

        frame = await client.get(f"/api/v1/evidence-library/sessions/{evidence_id}/scenarios/colreg-rule14-ho/decision-frame?sim_t=0")
        assert frame.status_code == 200
        assert frame.json()["evidence_id"] == evidence_id
```

Append this test to `src/sil_orchestrator/tests/test_evidence_routes.py`:

```python
@pytest.mark.asyncio
async def test_finalize_returns_evidence_id_when_library_ingest_succeeds(tmp_path, monkeypatch):
    monkeypatch.setattr(evidence_routes, "RUN_DIR", tmp_path / "runs")
    monkeypatch.setattr(evidence_routes, "TRACE_EVAL_DIR", tmp_path / "runs" / "trace_eval")
    monkeypatch.setattr(
        evidence_routes,
        "generate_trajectory_dashboard",
        lambda **kwargs: kwargs["output_png"].write_text("png") or kwargs["output_png"],
    )
    monkeypatch.setattr(
        evidence_routes,
        "ingest_frontend_session",
        lambda session_dir: "abc123",
    )
    _write_trace(tmp_path / "runs" / "trace_current.jsonl")
    app = FastAPI()
    app.include_router(evidence_routes.router)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        start = await client.post("/api/v1/evidence/session/start", json={
            "source": "frontend",
            "suite": "frontend",
            "scenario_id": "colreg-rule14-ho",
        })
        session_id = start.json()["session_id"]
        fin = await client.post(f"/api/v1/evidence/session/{session_id}/finalize", json={
            "scenario_id": "colreg-rule14-ho",
            "status": "completed",
            "run_id": "run-test",
        })
        assert fin.status_code == 200
        assert fin.json()["evidence_id"] == "abc123"
```

- [ ] **Step 2: Run tests and confirm failure**

Run:

```bash
pytest src/sil_orchestrator/tests/test_evidence_library_routes.py src/sil_orchestrator/tests/test_evidence_routes.py -q
```

Expected: route test fails because `sil_orchestrator.evidence_library.routes` does not exist or route paths return 404.

- [ ] **Step 3: Add service and route modules**

Create `src/sil_orchestrator/evidence_library/service.py`:

```python
from __future__ import annotations

import sqlite3
from pathlib import Path
from typing import Any

from .config import EvidenceLibraryConfig, EvidenceRootConfig, load_effective_config
from .ingest import ingest_session, query_decision_frame, query_replay
from .store import initialize_schema, open_database


def open_initialized(config: EvidenceLibraryConfig | None = None) -> sqlite3.Connection:
    cfg = config or load_effective_config()
    conn = open_database(cfg)
    initialize_schema(conn)
    return conn


def _root_rows(config: EvidenceLibraryConfig) -> list[dict[str, Any]]:
    return [
        {
            "root_id": root.root_id,
            "label": root.label,
            "source": root.source,
            "path_glob": root.path_glob,
            "enabled": root.enabled,
            "trusted": root.trusted,
            "allow_retention_mutation": root.allow_retention_mutation,
            "follow_symlinks": root.follow_symlinks,
        }
        for root in config.roots
    ]


def rescan_all(repo_root: Path | None = None, force: bool = False) -> dict[str, Any]:
    config = load_effective_config(repo_root=repo_root)
    conn = open_initialized(config)
    ingested = 0
    errors: list[dict[str, str]] = []
    for root in config.roots:
        if not root.enabled:
            continue
        for root_path in sorted(Path().glob(root.path_glob) if not Path(root.path_glob).is_absolute() else __import__("glob").glob(root.path_glob)):
            base = Path(root_path)
            if not base.exists():
                continue
            for manifest in sorted(base.glob("*/manifest.json")):
                try:
                    ingest_session(conn, root, manifest.parent, raw_trace_policy=config.effective_retention_policy, force=force)
                    ingested += 1
                except Exception as exc:
                    errors.append({"path": str(manifest.parent), "error": str(exc)})
    return {"ingested": ingested, "errors": errors}


def ingest_frontend_session(session_dir: Path) -> str | None:
    config = load_effective_config()
    conn = open_initialized(config)
    root = EvidenceRootConfig(
        root_id="frontend",
        label="Frontend evidence sessions",
        source="frontend",
        path_glob=str(session_dir.parent),
        enabled=True,
        trusted=True,
        allow_retention_mutation=False,
        follow_symlinks=False,
    )
    result = ingest_session(conn, root, session_dir, raw_trace_policy=config.effective_retention_policy, force=True)
    return result.evidence_id


def list_sessions(limit: int = 200) -> list[dict[str, Any]]:
    conn = open_initialized()
    rows = conn.execute(
        "select * from sessions order by coalesce(created_at, ended_at, session_id) desc limit ?",
        (max(1, min(limit, 500)),),
    ).fetchall()
    return [dict(row) for row in rows]


def get_replay(evidence_id: str, scenario_id: str) -> dict[str, Any]:
    return query_replay(open_initialized(), evidence_id, scenario_id)


def get_decision_frame(evidence_id: str, scenario_id: str, sim_t: float) -> dict[str, Any]:
    return query_decision_frame(open_initialized(), evidence_id, scenario_id, sim_t)


def get_config_payload(repo_root: Path | None = None) -> dict[str, Any]:
    config = load_effective_config(repo_root=repo_root)
    return {
        "config_home": str(config.config_home),
        "database_path": str(config.database_path),
        "raw_trace_policy": config.raw_trace_policy,
        "effective_retention_policy": config.effective_retention_policy,
        "roots": _root_rows(config),
    }
```

Create `src/sil_orchestrator/evidence_library/routes.py`:

```python
from __future__ import annotations

from pathlib import Path

from fastapi import APIRouter

from .service import get_config_payload, get_decision_frame, get_replay, list_sessions, rescan_all

REPO_ROOT = Path(__file__).resolve().parents[3]

router = APIRouter(prefix="/api/v1/evidence-library", tags=["evidence-library"])


@router.get("/config")
async def get_config():
    return get_config_payload(repo_root=REPO_ROOT)


@router.get("/roots")
async def get_roots():
    payload = get_config_payload(repo_root=REPO_ROOT)
    return {"roots": payload["roots"]}


@router.post("/rescan")
async def rescan(request: dict):
    return rescan_all(repo_root=REPO_ROOT, force=bool(request.get("force", False)))


@router.get("/sessions")
async def sessions(limit: int = 200):
    return {"sessions": list_sessions(limit=limit)}


@router.get("/sessions/{evidence_id}/scenarios/{scenario_id}/replay")
async def replay(evidence_id: str, scenario_id: str):
    return get_replay(evidence_id, scenario_id)


@router.get("/sessions/{evidence_id}/scenarios/{scenario_id}/decision-frame")
async def decision_frame(evidence_id: str, scenario_id: str, sim_t: float):
    return get_decision_frame(evidence_id, scenario_id, sim_t)
```

Modify `src/sil_orchestrator/main.py`:

```python
from sil_orchestrator.evidence_library.routes import router as evidence_library_router
```

Add include near existing evidence router:

```python
app.include_router(evidence_library_router)
```

Modify `src/sil_orchestrator/evidence_routes.py` imports:

```python
from sil_orchestrator.evidence_library.service import ingest_frontend_session
```

Modify `finalize_session` after `manifest = mgr.finalize(...)` and before return:

```python
    evidence_id = None
    try:
        evidence_id = ingest_frontend_session(session.session_dir)
    except Exception:
        evidence_id = None
```

Modify final return:

```python
    return {"discarded": False, "evidence_id": evidence_id, **manifest}
```

- [ ] **Step 4: Run route tests and confirm pass**

Run:

```bash
pytest src/sil_orchestrator/tests/test_evidence_library_routes.py src/sil_orchestrator/tests/test_evidence_routes.py -q
```

Expected: all tests in both files pass.

- [ ] **Step 5: Commit Task 3**

```bash
git add src/sil_orchestrator/evidence_library/service.py src/sil_orchestrator/evidence_library/routes.py src/sil_orchestrator/main.py src/sil_orchestrator/evidence_routes.py src/sil_orchestrator/tests/test_evidence_library_routes.py src/sil_orchestrator/tests/test_evidence_routes.py
git commit -m "feat(evidence): expose evidence library routes"
```

## Task 4: Frontend API Types, Route Param, And Evidence Id Store

**Files:**
- Modify: `web/src/api/silApi.ts`
- Modify: `web/src/App.tsx`
- Modify: `web/src/store/scenarioStore.ts`
- Test: `web/src/screens/__tests__/SimulationEvaluator.test.tsx`

**Interfaces:**
- Produces RTK hooks: `useGetEvidenceLibrarySessionsQuery`, `useRescanEvidenceLibraryMutation`, `useGetEvidenceReplayQuery`, `useGetDecisionFrameQuery`
- Produces store actions: `setEvidenceId(evidenceId: string | null): void`
- Produces prop: `SimulationEvaluator({ evidenceId?: string })`

- [ ] **Step 1: Write failing frontend route/store test**

Replace `web/src/screens/__tests__/SimulationEvaluator.test.tsx` mock shape with added hooks and add this test:

```tsx
it('renders evidence library when no evidence id is bound', () => {
  render(<SimulationEvaluator />);
  expect(screen.getByText('Evidence Library')).toBeInTheDocument();
});

it('renders replay detail when evidence id is bound', () => {
  render(<SimulationEvaluator evidenceId="evidence-123" />);
  expect(screen.getByTestId('trajectory-replay')).toBeInTheDocument();
  expect(screen.getByText('colreg-rule14-ho')).toBeInTheDocument();
});
```

Update the `vi.mock('../../api/silApi', ...)` block with these extra exports:

```tsx
  useGetEvidenceLibrarySessionsQuery: vi.fn(() => ({
    data: { sessions: [] },
    isLoading: false,
    refetch: vi.fn(),
  })),
  useRescanEvidenceLibraryMutation: () => [vi.fn().mockResolvedValue({ ingested: 0 }), { isLoading: false }],
  useGetEvidenceReplayQuery: vi.fn(() => ({
    data: {
      session: { evidence_id: 'evidence-123', session_id: 'session-123' },
      scenario: { scenario_id: 'colreg-rule14-ho', verdict: 'PASS', overall_pass: true },
      duration_s: 10,
      trajectory: [
        { vessel_id: 'OWN', vessel_role: 'ownship', sim_t: 0, lat: 0, lon: 0, heading_deg: 0, sog_kn: 8 },
        { vessel_id: 'OWN', vessel_role: 'ownship', sim_t: 10, lat: 0.01, lon: 0, heading_deg: 0, sog_kn: 8 },
      ],
      events: [],
      gates: [],
      artifacts: [],
    },
    isLoading: false,
  })),
  useGetDecisionFrameQuery: vi.fn(() => ({ data: null, isLoading: false })),
```

- [ ] **Step 2: Run frontend test and confirm failure**

Run:

```bash
cd web && npm test -- --run src/screens/__tests__/SimulationEvaluator.test.tsx
```

Expected: FAIL because `SimulationEvaluator` does not accept `evidenceId` and Evidence Library text is absent.

- [ ] **Step 3: Add RTK evidence-library types and hooks**

Modify `web/src/api/silApi.ts` before `export const silApi = createApi`:

```typescript
export interface EvidenceLibrarySession {
  evidence_id: string;
  session_id: string;
  source: string;
  suite: string;
  root_id: string;
  worktree_name?: string | null;
  branch?: string | null;
  session_path: string;
  created_at?: string | null;
  ended_at?: string | null;
  status?: string | null;
  valid_data: number | boolean;
  scenario_count: number;
  ingest_status: string;
  ingest_error?: string | null;
}

export interface EvidenceLibrarySessionsResponse {
  sessions: EvidenceLibrarySession[];
}

export interface EvidenceReplayTrajectoryPoint {
  vessel_id: string;
  vessel_role: string;
  sim_t: number;
  wall_t?: number | null;
  lat?: number | null;
  lon?: number | null;
  heading_deg?: number | null;
  sog_kn?: number | null;
  rot_deg_s?: number | null;
  source_topic?: string | null;
  sample_seq?: number;
}

export interface EvidenceReplayEvent {
  event_id?: number;
  sim_t: number;
  wall_t?: number | null;
  module: string;
  event_type: string;
  severity: string;
  payload_json: string;
  source_topic?: string | null;
}

export interface EvidenceGateResult {
  gate_id: string;
  status: string;
  temporal_scope: string;
  payload_json: string;
  source: string;
}

export interface EvidenceReplayResponse {
  session: EvidenceLibrarySession;
  scenario: {
    scenario_id: string;
    verdict?: string | null;
    overall_pass?: boolean | number | null;
    min_cpa_nm?: number | null;
  };
  duration_s: number;
  trajectory: EvidenceReplayTrajectoryPoint[];
  events: EvidenceReplayEvent[];
  gates: EvidenceGateResult[];
  artifacts: Array<{ artifact_id: number; kind: string; relative_path: string; available: number | boolean }>;
}

export interface EvidenceDecisionFrame {
  evidence_id: string;
  scenario_id: string;
  sim_t: number;
  chain: Record<string, { status: string; status_source: string; facts: Record<string, unknown> }>;
  gates: EvidenceGateResult[];
  nearby_events: EvidenceReplayEvent[];
}
```

Add endpoints inside `endpoints: (builder) => ({`:

```typescript
    getEvidenceLibrarySessions: builder.query<EvidenceLibrarySessionsResponse, void>({
      query: () => '/evidence-library/sessions',
    }),

    rescanEvidenceLibrary: builder.mutation<{ ingested: number; errors: Array<{ path: string; error: string }> }, { force?: boolean }>({
      query: (body) => ({
        url: '/evidence-library/rescan',
        method: 'POST',
        body,
      }),
    }),

    getEvidenceReplay: builder.query<EvidenceReplayResponse, { evidenceId: string; scenarioId: string }>({
      query: ({ evidenceId, scenarioId }) =>
        `/evidence-library/sessions/${encodeURIComponent(evidenceId)}/scenarios/${encodeURIComponent(scenarioId)}/replay`,
    }),

    getDecisionFrame: builder.query<EvidenceDecisionFrame, { evidenceId: string; scenarioId: string; simT: number }>({
      query: ({ evidenceId, scenarioId, simT }) =>
        `/evidence-library/sessions/${encodeURIComponent(evidenceId)}/scenarios/${encodeURIComponent(scenarioId)}/decision-frame?sim_t=${encodeURIComponent(simT)}`,
    }),
```

Add hook exports:

```typescript
  useGetEvidenceLibrarySessionsQuery,
  useRescanEvidenceLibraryMutation,
  useGetEvidenceReplayQuery,
  useGetDecisionFrameQuery,
```

- [ ] **Step 4: Add route/store plumbing**

Modify `web/src/store/scenarioStore.ts` interface:

```typescript
  evidenceId: string | null;
  setEvidenceId: (evidenceId: string | null) => void;
```

Modify store body:

```typescript
  evidenceId: null,
  setEvidenceId: (evidenceId) => set({ evidenceId }),
  reset: () => set({ scenarioId: null, runId: null, evidenceId: null, scenarioHash: null, lifecycleState: null, yamlValid: true, yamlError: null }),
```

Modify `web/src/App.tsx` render:

```tsx
        {route.screen === 'evaluator'  && <SimulationEvaluator evidenceId={route.runId} />}
```

- [ ] **Step 5: Run frontend test and confirm pass**

Run:

```bash
cd web && npm test -- --run src/screens/__tests__/SimulationEvaluator.test.tsx
```

Expected: tests in `SimulationEvaluator.test.tsx` pass.

- [ ] **Step 6: Commit Task 4**

```bash
git add web/src/api/silApi.ts web/src/App.tsx web/src/store/scenarioStore.ts web/src/screens/__tests__/SimulationEvaluator.test.tsx
git commit -m "feat(evidence): add frontend evidence api bindings"
```

## Task 5: Evidence Library Entry View

**Files:**
- Create: `web/src/screens/evaluator/EvidenceLibraryView.tsx`
- Modify: `web/src/screens/SimulationEvaluator.tsx`
- Test: `web/src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx`

**Interfaces:**
- Consumes: `EvidenceLibrarySession`, `useGetEvidenceLibrarySessionsQuery`, `useRescanEvidenceLibraryMutation`
- Produces: `EvidenceLibraryView({ onOpen }: { onOpen: (evidenceId: string) => void })`

- [ ] **Step 1: Write failing Evidence Library test**

Create `web/src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx`:

```tsx
import { describe, expect, it, vi } from 'vitest';
import { fireEvent, render, screen } from '@testing-library/react';
import { EvidenceLibraryView } from '../EvidenceLibraryView';

vi.mock('../../../api/silApi', () => ({
  useGetEvidenceLibrarySessionsQuery: () => ({
    data: {
      sessions: [
        {
          evidence_id: 'ev-1',
          session_id: '20260707_132000_single_colreg-rule14-ho',
          source: 'cli',
          suite: 'single',
          root_id: 'worktrees',
          worktree_name: 'colregs-nlp-cpa-fix',
          branch: 'codex/colregs-nlp-cpa-fix',
          session_path: '/tmp/runs/trace_eval/session',
          created_at: '2026-07-07T13:20:00Z',
          status: 'completed',
          valid_data: true,
          scenario_count: 1,
          ingest_status: 'ok',
        },
      ],
    },
    isLoading: false,
    refetch: vi.fn(),
  }),
  useRescanEvidenceLibraryMutation: () => [vi.fn(), { isLoading: false }],
}));

describe('EvidenceLibraryView', () => {
  it('opens selected evidence session', () => {
    const onOpen = vi.fn();
    render(<EvidenceLibraryView onOpen={onOpen} />);
    expect(screen.getByText('Evidence Library')).toBeInTheDocument();
    expect(screen.getByText('20260707_132000_single_colreg-rule14-ho')).toBeInTheDocument();
    fireEvent.click(screen.getByText('Open Replay'));
    expect(onOpen).toHaveBeenCalledWith('ev-1');
  });
});
```

- [ ] **Step 2: Run test and confirm failure**

Run:

```bash
cd web && npm test -- --run src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx
```

Expected: FAIL because `EvidenceLibraryView` does not exist.

- [ ] **Step 3: Implement EvidenceLibraryView**

Create `web/src/screens/evaluator/EvidenceLibraryView.tsx`:

```tsx
import React from 'react';
import {
  useGetEvidenceLibrarySessionsQuery,
  useRescanEvidenceLibraryMutation,
  type EvidenceLibrarySession,
} from '../../api/silApi';

interface EvidenceLibraryViewProps {
  onOpen: (evidenceId: string) => void;
}

const statusColor = (status?: string | null) => {
  if (status === 'ok' || status === 'completed') return 'var(--c-stbd)';
  if (status === 'failed' || status === 'error') return 'var(--c-danger)';
  return 'var(--txt-3)';
};

export function EvidenceLibraryView({ onOpen }: EvidenceLibraryViewProps) {
  const { data, isLoading, refetch } = useGetEvidenceLibrarySessionsQuery();
  const [rescan, rescanState] = useRescanEvidenceLibraryMutation();
  const sessions = data?.sessions ?? [];

  const handleRescan = async () => {
    await rescan({ force: false });
    refetch();
  };

  return (
    <div style={{ height: '100%', display: 'grid', gridTemplateColumns: '280px 1fr', background: 'var(--bg-0)', color: 'var(--txt-1)' }}>
      <aside style={{ borderRight: '1px solid var(--line-1)', padding: 16, display: 'flex', flexDirection: 'column', gap: 12 }}>
        <h1 style={{ fontFamily: 'var(--f-disp)', fontSize: 18, margin: 0 }}>Evidence Library</h1>
        <button onClick={handleRescan} disabled={rescanState.isLoading} style={{ border: '1px solid var(--c-phos)', color: 'var(--c-phos)', background: 'transparent', padding: '8px 10px', cursor: 'pointer' }}>
          {rescanState.isLoading ? 'Rescanning' : 'Rescan'}
        </button>
        <div style={{ fontFamily: 'var(--f-mono)', fontSize: 11, color: 'var(--txt-3)' }}>
          Sessions: {sessions.length}
        </div>
      </aside>
      <main style={{ padding: 16, overflow: 'auto' }}>
        {isLoading ? (
          <div>Loading evidence</div>
        ) : (
          <table style={{ width: '100%', borderCollapse: 'collapse', fontFamily: 'var(--f-mono)', fontSize: 12 }}>
            <thead>
              <tr style={{ color: 'var(--txt-3)' }}>
                <th align="left">Session</th>
                <th align="left">Source</th>
                <th align="left">Root</th>
                <th align="left">Worktree</th>
                <th align="left">Status</th>
                <th align="left">Action</th>
              </tr>
            </thead>
            <tbody>
              {sessions.map((session: EvidenceLibrarySession) => (
                <tr key={session.evidence_id} style={{ borderTop: '1px solid var(--line-1)' }}>
                  <td style={{ padding: '10px 6px' }}>{session.session_id}</td>
                  <td>{session.source}</td>
                  <td>{session.root_id}</td>
                  <td>{session.worktree_name || session.branch || '-'}</td>
                  <td style={{ color: statusColor(session.ingest_status) }}>{session.ingest_status}</td>
                  <td>
                    <button onClick={() => onOpen(session.evidence_id)} style={{ border: '1px solid var(--line-2)', background: 'transparent', color: 'var(--txt-1)', padding: '4px 8px', cursor: 'pointer' }}>
                      Open Replay
                    </button>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </main>
    </div>
  );
}
```

Modify `web/src/screens/SimulationEvaluator.tsx` signature and top-level branch:

```tsx
import { EvidenceLibraryView } from './evaluator/EvidenceLibraryView';

interface SimulationEvaluatorProps {
  evidenceId?: string;
}

export function SimulationEvaluator({ evidenceId }: SimulationEvaluatorProps) {
  if (!evidenceId) {
    return <EvidenceLibraryView onOpen={(id) => { window.location.hash = `#/evaluator/${id}`; }} />;
  }
```

- [ ] **Step 4: Run library tests and confirm pass**

Run:

```bash
cd web && npm test -- --run src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx src/screens/__tests__/SimulationEvaluator.test.tsx
```

Expected: both test files pass.

- [ ] **Step 5: Commit Task 5**

```bash
git add web/src/screens/evaluator/EvidenceLibraryView.tsx web/src/screens/SimulationEvaluator.tsx web/src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx
git commit -m "feat(evidence): add evidence library view"
```

## Task 6: Replay Detail Uses Indexed Trajectory And Timeline Data

**Files:**
- Create: `web/src/screens/evaluator/ReplayDetailView.tsx`
- Modify: `web/src/screens/SimulationEvaluator.tsx`
- Modify: `web/src/screens/shared/TrajectoryReplay.tsx`
- Modify: `web/src/screens/shared/TimelineSixLane.tsx`
- Test: `web/src/screens/evaluator/__tests__/ReplayDetailView.test.tsx`

**Interfaces:**
- Consumes: `useGetEvidenceReplayQuery({ evidenceId, scenarioId })`
- Produces: `ReplayDetailView({ evidenceId, scenarioId }: { evidenceId: string; scenarioId: string })`
- Produces: `TrajectoryReplay` prop `points: EvidenceReplayTrajectoryPoint[]`
- Produces: `TimelineSixLane` prop `onEventSelect?: (event: TimelineEvent) => void`

- [ ] **Step 1: Write failing replay detail test**

Create `web/src/screens/evaluator/__tests__/ReplayDetailView.test.tsx`:

```tsx
import { describe, expect, it, vi } from 'vitest';
import { fireEvent, render, screen } from '@testing-library/react';
import { ReplayDetailView } from '../ReplayDetailView';

vi.mock('../../../api/silApi', () => ({
  useGetEvidenceReplayQuery: () => ({
    data: {
      session: { evidence_id: 'ev-1', session_id: 'session-1' },
      scenario: { scenario_id: 'colreg-rule14-ho', verdict: 'FAIL', overall_pass: false, min_cpa_nm: 0.243 },
      duration_s: 10,
      trajectory: [
        { vessel_id: 'OWN', vessel_role: 'ownship', sim_t: 0, lat: 0, lon: 0, heading_deg: 0, sog_kn: 8 },
        { vessel_id: 'OWN', vessel_role: 'ownship', sim_t: 10, lat: 0.01, lon: 0, heading_deg: 10, sog_kn: 8 },
        { vessel_id: 'T01', vessel_role: 'target', sim_t: 0, lat: 0.01, lon: 0.01, heading_deg: 180, sog_kn: 8 },
      ],
      events: [
        { event_id: 1, sim_t: 5, module: 'M5', event_type: 'PLAN_READY', severity: 'info', payload_json: '{}' },
      ],
      gates: [
        { gate_id: 'G-SEM', status: 'FAIL', temporal_scope: 'final_run_verdict', payload_json: '{}', source: 'TraceEvaluationReport' },
      ],
      artifacts: [],
    },
    isLoading: false,
  }),
  useGetDecisionFrameQuery: () => ({ data: null, isLoading: false }),
}));

describe('ReplayDetailView', () => {
  it('renders replay data and scrubs timeline', () => {
    render(<ReplayDetailView evidenceId="ev-1" scenarioId="colreg-rule14-ho" />);
    expect(screen.getByText('session-1')).toBeInTheDocument();
    expect(screen.getByText('G-SEM')).toBeInTheDocument();
    fireEvent.change(screen.getByLabelText('Replay time'), { target: { value: '5' } });
    expect(screen.getByText('T+00:05')).toBeInTheDocument();
  });
});
```

- [ ] **Step 2: Run test and confirm failure**

Run:

```bash
cd web && npm test -- --run src/screens/evaluator/__tests__/ReplayDetailView.test.tsx
```

Expected: FAIL because `ReplayDetailView` does not exist.

- [ ] **Step 3: Make TrajectoryReplay data-driven**

Modify `web/src/screens/shared/TrajectoryReplay.tsx` props:

```tsx
import type { EvidenceReplayTrajectoryPoint } from '../../api/silApi';

interface TrajectoryReplayProps {
  durationSec: number;
  currentTimeSec: number;
  onTimeChange?: (t: number) => void;
  points?: EvidenceReplayTrajectoryPoint[];
}
```

Add helpers above component:

```tsx
const project = (lat: number, lon: number, bounds: { minLat: number; maxLat: number; minLon: number; maxLon: number }): [number, number] => {
  const lonSpan = Math.max(0.000001, bounds.maxLon - bounds.minLon);
  const latSpan = Math.max(0.000001, bounds.maxLat - bounds.minLat);
  return [
    40 + ((lon - bounds.minLon) / lonSpan) * 400,
    320 - ((lat - bounds.minLat) / latSpan) * 280,
  ];
};
```

Inside component, replace simulated point construction with data branch:

```tsx
  const dataPoints = points ?? [];
  const bounds = useMemo(() => {
    const valid = dataPoints.filter((p) => typeof p.lat === 'number' && typeof p.lon === 'number');
    if (valid.length === 0) return { minLat: 0, maxLat: 1, minLon: 0, maxLon: 1 };
    const lats = valid.map((p) => p.lat as number);
    const lons = valid.map((p) => p.lon as number);
    return {
      minLat: Math.min(...lats),
      maxLat: Math.max(...lats),
      minLon: Math.min(...lons),
      maxLon: Math.max(...lons),
    };
  }, [dataPoints]);
  const ownshipPts = useMemo(() => {
    if (dataPoints.length > 0) {
      return dataPoints
        .filter((p) => p.vessel_id === 'OWN' && typeof p.lat === 'number' && typeof p.lon === 'number')
        .sort((a, b) => a.sim_t - b.sim_t)
        .map((p) => project(p.lat as number, p.lon as number, bounds));
    }
    const N = 60;
    return Array.from({ length: N }, (_, i) => {
      const u = i / (N - 1);
      const x = 80 + u * 360 + (u > 0.4 && u < 0.7 ? 50 * Math.sin((u - 0.4) * Math.PI / 0.3) : 0);
      const y = 320 - u * 280;
      return [x, y] as [number, number];
    });
  }, [dataPoints, bounds]);
  const t01Pts = useMemo(() => {
    if (dataPoints.length > 0) {
      return dataPoints
        .filter((p) => p.vessel_id !== 'OWN' && typeof p.lat === 'number' && typeof p.lon === 'number')
        .sort((a, b) => a.sim_t - b.sim_t)
        .map((p) => project(p.lat as number, p.lon as number, bounds));
    }
    return ownshipPts.map(([x, y], i) => [x + 120 - i * 1.6, y - 80 + i * 1.0] as [number, number]);
  }, [dataPoints, ownshipPts, bounds]);
```

Add accessible label to range input:

```tsx
          aria-label="Replay time"
```

- [ ] **Step 4: Add event selection support to TimelineSixLane**

Modify `web/src/screens/shared/TimelineSixLane.tsx` props:

```tsx
  onEventSelect?: (event: TimelineEvent) => void;
```

Modify component signature:

```tsx
  events, durationSec, currentTimeSec, onScrub, onEventSelect,
```

Add click handler on event node:

```tsx
                    onClick={(e) => {
                      e.stopPropagation();
                      onScrub(evt.t);
                      onEventSelect?.(evt);
                    }}
```

- [ ] **Step 5: Implement ReplayDetailView and use it from SimulationEvaluator**

Create `web/src/screens/evaluator/ReplayDetailView.tsx`:

```tsx
import React, { useMemo, useState } from 'react';
import {
  useGetEvidenceReplayQuery,
  type EvidenceReplayEvent,
} from '../../api/silApi';
import { TrajectoryReplay } from '../shared/TrajectoryReplay';
import { TimelineSixLane, type TimelineEvent } from '../shared/TimelineSixLane';

interface ReplayDetailViewProps {
  evidenceId: string;
  scenarioId: string;
}

const toTimelineEvent = (event: EvidenceReplayEvent): TimelineEvent => ({
  t: event.sim_t,
  k: event.event_type,
  sev: event.severity === 'crit' || event.severity === 'warn' ? event.severity : 'info',
  m: event.module,
  d: event.payload_json,
});

export function ReplayDetailView({ evidenceId, scenarioId }: ReplayDetailViewProps) {
  const [currentTimeSec, setCurrentTimeSec] = useState(0);
  const { data, isLoading } = useGetEvidenceReplayQuery({ evidenceId, scenarioId });
  const timelineEvents = useMemo(() => (data?.events ?? []).map(toTimelineEvent), [data?.events]);
  const durationSec = Math.max(0, data?.duration_s ?? 0);

  if (isLoading || !data) {
    return <div style={{ padding: 16 }}>Loading replay</div>;
  }

  return (
    <div style={{ height: '100%', display: 'grid', gridTemplateColumns: '1fr 360px', gap: 12, padding: 16, background: 'var(--bg-0)' }}>
      <main style={{ minWidth: 0, display: 'flex', flexDirection: 'column', gap: 12 }}>
        <div style={{ display: 'flex', justifyContent: 'space-between', color: 'var(--txt-1)', fontFamily: 'var(--f-mono)' }}>
          <span>{data.session.session_id}</span>
          <span>{data.scenario.scenario_id}</span>
        </div>
        <div className="glass-panel" style={{ flex: 1, overflow: 'hidden' }}>
          <TrajectoryReplay
            durationSec={durationSec}
            currentTimeSec={currentTimeSec}
            onTimeChange={setCurrentTimeSec}
            points={data.trajectory}
          />
        </div>
        <div className="glass-panel" style={{ height: 180, overflow: 'hidden' }}>
          <TimelineSixLane
            events={timelineEvents}
            durationSec={durationSec}
            currentTimeSec={currentTimeSec}
            onScrub={setCurrentTimeSec}
          />
        </div>
      </main>
      <aside style={{ display: 'flex', flexDirection: 'column', gap: 8, color: 'var(--txt-1)' }}>
        {data.gates.map((gate) => (
          <button key={`${gate.gate_id}-${gate.source}`} style={{ textAlign: 'left', border: '1px solid var(--line-2)', background: 'var(--bg-1)', color: 'var(--txt-1)', padding: 8 }}>
            <strong>{gate.gate_id}</strong> {gate.status}
          </button>
        ))}
      </aside>
    </div>
  );
}
```

Modify `web/src/screens/SimulationEvaluator.tsx`:

```tsx
import { ReplayDetailView } from './evaluator/ReplayDetailView';
```

Replace evidence-id branch body:

```tsx
  if (evidenceId) {
    return <ReplayDetailView evidenceId={evidenceId} scenarioId={scenarioId || 'colreg-rule14-ho'} />;
  }
```

- [ ] **Step 6: Run replay tests and confirm pass**

Run:

```bash
cd web && npm test -- --run src/screens/evaluator/__tests__/ReplayDetailView.test.tsx src/screens/__tests__/SimulationEvaluator.test.tsx
```

Expected: both test files pass.

- [ ] **Step 7: Commit Task 6**

```bash
git add web/src/screens/evaluator/ReplayDetailView.tsx web/src/screens/SimulationEvaluator.tsx web/src/screens/shared/TrajectoryReplay.tsx web/src/screens/shared/TimelineSixLane.tsx web/src/screens/evaluator/__tests__/ReplayDetailView.test.tsx
git commit -m "feat(evidence): replay indexed trajectory on timeline"
```

## Task 7: Chain Inspector Opens From Failed Gate Or Timeline Event

**Files:**
- Create: `web/src/screens/evaluator/ChainInspector.tsx`
- Modify: `web/src/screens/evaluator/ReplayDetailView.tsx`
- Test: `web/src/screens/evaluator/__tests__/ReplayDetailView.test.tsx`

**Interfaces:**
- Consumes: `useGetDecisionFrameQuery({ evidenceId, scenarioId, simT })`
- Produces: `ChainInspector({ evidenceId, scenarioId, simT, onClose })`

- [ ] **Step 1: Extend replay test for inspector opening**

Append to `ReplayDetailView.test.tsx`:

```tsx
it('opens chain inspector from failed gate', () => {
  render(<ReplayDetailView evidenceId="ev-1" scenarioId="colreg-rule14-ho" />);
  fireEvent.click(screen.getByText('G-SEM'));
  expect(screen.getByText('Decision Frame')).toBeInTheDocument();
});
```

Update API mock:

```tsx
  useGetDecisionFrameQuery: () => ({
    data: {
      evidence_id: 'ev-1',
      scenario_id: 'colreg-rule14-ho',
      sim_t: 0,
      chain: {
        M2: { status: 'OK', status_source: 'diagnostic_availability', facts: { primary_target_id: 'T01' } },
        M6: { status: 'WARN', status_source: 'diagnostic_availability', facts: { rule: 'Rule14', role: 'give_way' } },
        M5: { status: 'OK', status_source: 'diagnostic_availability', facts: { solver_status: 'VALID' } },
      },
      gates: [{ gate_id: 'G-SEM', status: 'FAIL', temporal_scope: 'final_run_verdict', payload_json: '{}', source: 'TraceEvaluationReport' }],
      nearby_events: [],
    },
    isLoading: false,
  }),
```

- [ ] **Step 2: Run test and confirm failure**

Run:

```bash
cd web && npm test -- --run src/screens/evaluator/__tests__/ReplayDetailView.test.tsx
```

Expected: FAIL because `Decision Frame` is absent after gate click.

- [ ] **Step 3: Implement ChainInspector**

Create `web/src/screens/evaluator/ChainInspector.tsx`:

```tsx
import React from 'react';
import { useGetDecisionFrameQuery } from '../../api/silApi';

interface ChainInspectorProps {
  evidenceId: string;
  scenarioId: string;
  simT: number;
  onClose: () => void;
}

export function ChainInspector({ evidenceId, scenarioId, simT, onClose }: ChainInspectorProps) {
  const { data, isLoading } = useGetDecisionFrameQuery({ evidenceId, scenarioId, simT });

  return (
    <div style={{ position: 'absolute', right: 16, top: 16, bottom: 16, width: 420, background: 'var(--bg-1)', border: '1px solid var(--line-2)', zIndex: 20, padding: 12, overflow: 'auto', color: 'var(--txt-1)' }}>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
        <h2 style={{ margin: 0, fontFamily: 'var(--f-disp)', fontSize: 14 }}>Decision Frame</h2>
        <button onClick={onClose} style={{ background: 'transparent', border: '1px solid var(--line-2)', color: 'var(--txt-1)' }}>Close</button>
      </div>
      <div style={{ fontFamily: 'var(--f-mono)', fontSize: 11, color: 'var(--txt-3)', margin: '8px 0' }}>
        T+{Math.floor(simT / 60).toString().padStart(2, '0')}:{Math.floor(simT % 60).toString().padStart(2, '0')}
      </div>
      {isLoading || !data ? (
        <div>Loading decision frame</div>
      ) : (
        <>
          {Object.entries(data.chain).map(([module, row]) => (
            <section key={module} style={{ borderTop: '1px solid var(--line-1)', padding: '8px 0' }}>
              <strong>{module}</strong> <span>{row.status}</span>
              <pre style={{ whiteSpace: 'pre-wrap', fontSize: 11 }}>{JSON.stringify(row.facts, null, 2)}</pre>
            </section>
          ))}
          <section style={{ borderTop: '1px solid var(--line-1)', padding: '8px 0' }}>
            <strong>Gates</strong>
            {data.gates.map((gate) => (
              <div key={`${gate.gate_id}-${gate.source}`} style={{ fontFamily: 'var(--f-mono)', fontSize: 11 }}>
                {gate.gate_id} {gate.status} {gate.source}
              </div>
            ))}
          </section>
        </>
      )}
    </div>
  );
}
```

- [ ] **Step 4: Wire inspector into ReplayDetailView**

Modify imports:

```tsx
import { ChainInspector } from './ChainInspector';
```

Add state:

```tsx
  const [inspectorTime, setInspectorTime] = useState<number | null>(null);
```

Modify gate button:

```tsx
          <button
            key={`${gate.gate_id}-${gate.source}`}
            onClick={() => setInspectorTime(currentTimeSec)}
            style={{ textAlign: 'left', border: '1px solid var(--line-2)', background: 'var(--bg-1)', color: 'var(--txt-1)', padding: 8 }}
          >
```

Modify timeline:

```tsx
            onEventSelect={(event) => setInspectorTime(event.t)}
```

Add inspector before closing top-level div:

```tsx
      {inspectorTime != null && (
        <ChainInspector
          evidenceId={evidenceId}
          scenarioId={scenarioId}
          simT={inspectorTime}
          onClose={() => setInspectorTime(null)}
        />
      )}
```

- [ ] **Step 5: Run inspector test and confirm pass**

Run:

```bash
cd web && npm test -- --run src/screens/evaluator/__tests__/ReplayDetailView.test.tsx
```

Expected: test file passes.

- [ ] **Step 6: Commit Task 7**

```bash
git add web/src/screens/evaluator/ChainInspector.tsx web/src/screens/evaluator/ReplayDetailView.tsx web/src/screens/evaluator/__tests__/ReplayDetailView.test.tsx
git commit -m "feat(evidence): add replay chain inspector"
```

## Task 8: End-To-End Verification For Data Entry And Timeline Playback

**Files:**
- Modify: `src/sil_orchestrator/tests/test_evidence_library_routes.py`
- Modify: `web/src/screens/evaluator/__tests__/ReplayDetailView.test.tsx`
- No production file changes unless a test exposes a defect from earlier tasks.

**Interfaces:**
- Consumes all previous task interfaces.
- Produces verified vertical slice: rescan data entry, session list, replay API, decision-frame API, and frontend timeline scrub.

- [ ] **Step 1: Add route integration assertion for frontend data entry**

Append to `src/sil_orchestrator/tests/test_evidence_library_routes.py`:

```python
@pytest.mark.asyncio
async def test_direct_session_rescan_is_stable_when_called_twice(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    root = repo / "runs" / "trace_eval"
    _session(root)
    config_home = tmp_path / "config"
    monkeypatch.setenv("MASS_L3_CONFIG_HOME", str(config_home))
    monkeypatch.setattr(routes, "REPO_ROOT", repo)
    app = FastAPI()
    app.include_router(routes.router)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        first = await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        second = await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        assert first.status_code == 200
        assert second.status_code == 200
        listed = await client.get("/api/v1/evidence-library/sessions")
        assert len(listed.json()["sessions"]) == 1
```

- [ ] **Step 2: Run backend vertical-slice tests**

Run:

```bash
pytest \
  src/sil_orchestrator/tests/test_evidence_library_config_store.py \
  src/sil_orchestrator/tests/test_evidence_library_ingest.py \
  src/sil_orchestrator/tests/test_evidence_library_routes.py \
  src/sil_orchestrator/tests/test_evidence_routes.py \
  -q
```

Expected: all selected backend tests pass.

- [ ] **Step 3: Run frontend vertical-slice tests**

Run:

```bash
cd web && npm test -- --run \
  src/screens/__tests__/SimulationEvaluator.test.tsx \
  src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx \
  src/screens/evaluator/__tests__/ReplayDetailView.test.tsx
```

Expected: all selected frontend tests pass.

- [ ] **Step 4: Run frontend build**

Run:

```bash
cd web && npm run build
```

Expected: Vite build completes with exit code 0.

- [ ] **Step 5: Manual browser smoke**

Start backend and frontend with the repo's normal local commands:

```bash
source scripts/local-a4000-env.sh
cd web
ORCH_PORT=18000 FOX_PORT=18765 npm run dev -- --host 0.0.0.0
```

Open:

```text
http://127.0.0.1:5173/#/evaluator
```

Expected:

- Evidence Library opens with no bound evidence id.
- `Rescan` indexes at least one fixture or local `runs/trace_eval` session when present.
- Selecting `Open Replay` navigates to `#/evaluator/<evidence_id>`.
- Replay Detail shows sea chart, timeline, gate rail, and current time.
- Dragging scrubber changes current time on chart and timeline.
- Clicking a failed gate opens Decision Frame.

- [ ] **Step 6: Commit verification test adjustments**

```bash
git add src/sil_orchestrator/tests/test_evidence_library_routes.py web/src/screens/evaluator/__tests__/ReplayDetailView.test.tsx
git commit -m "test(evidence): cover replay data entry flow"
```

## Self-Review

Spec coverage:

- Data entry from frontend finalize: Task 3.
- Data entry from background probe folders: Task 3 rescan route.
- SQLite schema and identity: Task 1.
- Existing probe artifact ingest: Task 2.
- Gate import without recomputation: Task 2.
- Replay endpoint: Task 3.
- Decision-frame endpoint: Task 2 and Task 3.
- Evidence Library UI: Task 5.
- Replay Detail chart and time-axis playback: Task 6.
- Chain inspector on gate/event click: Task 7.
- Verification: Task 8.

Separate future plans:

- Editable machine config UI and persistent `PUT /config`.
- Root health stale-check without deep scan.
- Retention mutation, gzip restore/export, prune, archive/export, and storage maintenance.
- Artifact download route with path-safe serving.
- Larger visual polish pass after backend data is real.

Plan hygiene: run the standard writing-plans red-flag scan before execution. Expected result: no matches.

Type consistency:

- Backend uses `evidence_id`, `scenario_id`, `sim_t` consistently across ingest, replay, decision frame, and routes.
- Frontend uses `evidenceId`, `scenarioId`, `simT` consistently in RTK hooks.
- `TrajectoryReplay` owns playback controls; `ReplayDetailView` owns `currentTimeSec`.
- `TimelineSixLane` event selection returns a `TimelineEvent`, then `ReplayDetailView` opens `ChainInspector`.
