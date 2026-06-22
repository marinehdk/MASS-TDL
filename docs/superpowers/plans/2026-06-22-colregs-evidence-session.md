# COLREGs Evidence Session Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a unified JSONL-first evidence session system that creates timestamp-prefixed folders and trajectory dashboard PNGs for single probe, clean 8-probe, clean 12-probe, and frontend-launched simulations.

**Architecture:** Add a small orchestrator-safe evidence session library used by both CLI runner code and FastAPI evidence routes. Keep `docker/sil_topic_bridge.py` writing `runs/trace_current.jsonl`; archive and post-process that trace at scenario/session boundaries. Generate PNG dashboards asynchronously and discard sessions with no valid own-ship trace data.

**Tech Stack:** Python 3, FastAPI, pytest, matplotlib Agg backend, existing `tools/sil/colregs_trace_evaluator.py`, existing `scripts/run_6_scenarios.py`, existing RTK Query frontend API in `web/src/api/silApi.ts`, Vitest for frontend tests.

## Global Constraints

- Work only in `/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-behavior-fix` on branch `codex/colregs-behavior-fix`.
- Do not change COLREG behavior, scenario geometry, gate thresholds, CPA floors, or M4/M5/M6 decision logic.
- Keep `docker/sil_topic_bridge.py` low-coupled; it continues writing `runs/trace_current.jsonl`.
- Evidence session folders use timestamp-first names: `<YYYYMMDD_HHMMSS>_single_<scenario_id>`, `<YYYYMMDD_HHMMSS>_clean8`, `<YYYYMMDD_HHMMSS>_clean12`, `<YYYYMMDD_HHMMSS>_frontend_<scenario_id>`.
- Retain a session only when at least one scenario trace has `/sil/own_ship_state` samples `>= 20` and own-ship `sim_t` duration `>= 5.0` seconds.
- JSONL is the initial source of truth. Arrow metadata may be detected and recorded, but Arrow replay is not required.
- PNG dashboard generation must not block simulation completion and must not delete JSONL/report artifacts on failure.
- Every task uses TDD: failing test, implementation, passing test, commit.

---

## File Structure

Create:

- `tools/sil/evidence_session.py` - session naming, manifest writing, trace validation, trace archival, empty-session discard, Arrow metadata detection, postprocess logging.
- `tools/sil/trajectory_dashboard.py` - JSONL/report-to-PNG dashboard generator with the approved layout.
- `src/sil_orchestrator/evidence_routes.py` - FastAPI routes for session start/finalize/get/list.
- `src/sil_orchestrator/tests/test_evidence_routes.py` - API tests.
- `tests/tools/test_evidence_session.py` - pure Python session manager tests.
- `tests/tools/test_trajectory_dashboard.py` - dashboard generation tests.

Modify:

- `scripts/run_6_scenarios.py` - auto-create evidence sessions when `--trace-report-dir` is omitted; archive each scenario into the session; write batch summary there.
- `src/sil_orchestrator/main.py` - include `evidence_routes.router`; optionally call evidence session finalization from lifecycle deactivate if a frontend session is active.
- `web/src/api/silApi.ts` - add evidence session RTK Query endpoints and types.
- `web/src/screens/SimulationCheck.tsx` - create frontend evidence session before lifecycle launch and finalize on launch failure/abort path.
- `web/src/screens/__tests__/SimulationCheck.runtime.test.tsx` - assert evidence session start/finalize calls.

Do not modify:

- `docker/sil_topic_bridge.py` unless tests reveal a flush bug. Existing close/reset behavior is sufficient for this plan.
- M4/M5/M6 COLREG source files.

---

### Task 1: Evidence Session Core

**Files:**
- Create: `tools/sil/evidence_session.py`
- Test: `tests/tools/test_evidence_session.py`

**Interfaces:**
- Produces:
  - `EvidenceSessionManager(root: Path = Path("runs/trace_eval"), run_root: Path = Path("runs"))`
  - `EvidenceSessionManager.start(source: str, suite: str, scenario_id: str | None = None, created_at: datetime | None = None) -> EvidenceSession`
  - `EvidenceSessionManager.archive_scenario(session: EvidenceSession, scenario_id: str, trace_path: Path = Path("runs/trace_current.jsonl"), report_path: Path | None = None, status: str = "unknown", run_id: str | None = None) -> dict`
  - `EvidenceSessionManager.finalize(session: EvidenceSession, status: str) -> dict | None`
  - `validate_trace_jsonl(path: Path) -> dict`
  - `EvidenceSession.session_dir: Path`
  - `EvidenceSession.session_name: str`
- Consumes: no repo-local implementation from later tasks.

- [ ] **Step 1: Write the failing tests**

Create `tests/tools/test_evidence_session.py`:

```python
from __future__ import annotations

import json
from datetime import datetime, timezone
from pathlib import Path

from tools.sil.evidence_session import EvidenceSessionManager, validate_trace_jsonl


def _write_trace(path: Path, *, samples: int, duration_s: float) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w") as f:
        for i in range(samples):
            sim_t = 10.0 + (duration_s * i / max(1, samples - 1))
            f.write(json.dumps({
                "sim_t": round(sim_t, 3),
                "wall_t": 1000.0 + i,
                "topic": "/sil/own_ship_state",
                "lat": 63.44,
                "lon": 10.38,
                "heading_deg": 360.0,
                "sog_kn": 8.0,
            }) + "\n")


def test_session_name_timestamp_prefix_for_single(tmp_path: Path):
    mgr = EvidenceSessionManager(root=tmp_path / "trace_eval", run_root=tmp_path / "runs")
    session = mgr.start(
        source="cli",
        suite="single",
        scenario_id="colreg-rule14-ho",
        created_at=datetime(2026, 6, 22, 15, 30, 12, tzinfo=timezone.utc),
    )

    assert session.session_name == "20260622_153012_single_colreg-rule14-ho"
    assert session.session_dir.exists()
    manifest = json.loads((session.session_dir / "manifest.json").read_text())
    assert manifest["status"] == "pending"
    assert manifest["source"] == "cli"
    assert manifest["suite"] == "single"


def test_validate_trace_accepts_minimum_valid_data(tmp_path: Path):
    trace = tmp_path / "trace.jsonl"
    _write_trace(trace, samples=20, duration_s=5.0)

    result = validate_trace_jsonl(trace)

    assert result["valid_data"] is True
    assert result["own_ship_samples"] == 20
    assert result["sim_t_duration_s"] == 5.0


def test_validate_trace_rejects_short_duration(tmp_path: Path):
    trace = tmp_path / "trace.jsonl"
    _write_trace(trace, samples=25, duration_s=4.9)

    result = validate_trace_jsonl(trace)

    assert result["valid_data"] is False
    assert result["own_ship_samples"] == 25
    assert result["sim_t_duration_s"] == 4.9


def test_archive_valid_scenario_keeps_session_on_finalize(tmp_path: Path):
    run_root = tmp_path / "runs"
    trace = run_root / "trace_current.jsonl"
    _write_trace(trace, samples=25, duration_s=10.0)
    report = tmp_path / "report.json"
    report.write_text(json.dumps({"verdict": {"overall_pass": True}}))
    mgr = EvidenceSessionManager(root=tmp_path / "trace_eval", run_root=run_root)
    session = mgr.start(
        source="frontend",
        suite="frontend",
        scenario_id="colreg-rule14-ho",
        created_at=datetime(2026, 6, 22, 15, 30, 12, tzinfo=timezone.utc),
    )

    archived = mgr.archive_scenario(
        session,
        "colreg-rule14-ho",
        trace_path=trace,
        report_path=report,
        status="pass",
        run_id="run-test",
    )
    final_manifest = mgr.finalize(session, status="completed")

    assert archived["valid_data"] is True
    assert final_manifest is not None
    assert session.session_dir.exists()
    assert (session.session_dir / "colreg-rule14-ho.trace_current.jsonl").exists()
    assert (session.session_dir / "colreg-rule14-ho.json").exists()
    assert final_manifest["valid_data"] is True
    assert final_manifest["scenarios"][0]["run_id"] == "run-test"


def test_finalize_discards_session_with_no_valid_scenario(tmp_path: Path):
    run_root = tmp_path / "runs"
    trace = run_root / "trace_current.jsonl"
    _write_trace(trace, samples=3, duration_s=1.0)
    mgr = EvidenceSessionManager(root=tmp_path / "trace_eval", run_root=run_root)
    session = mgr.start(
        source="frontend",
        suite="frontend",
        scenario_id="colreg-rule14-ho",
        created_at=datetime(2026, 6, 22, 15, 30, 12, tzinfo=timezone.utc),
    )

    mgr.archive_scenario(session, "colreg-rule14-ho", trace_path=trace, status="stopped")
    final_manifest = mgr.finalize(session, status="stopped")

    assert final_manifest is None
    assert not session.session_dir.exists()
```

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
pytest tests/tools/test_evidence_session.py -v
```

Expected: FAIL with `ModuleNotFoundError: No module named 'tools.sil.evidence_session'`.

- [ ] **Step 3: Implement the core manager**

Create `tools/sil/evidence_session.py`:

```python
from __future__ import annotations

import json
import re
import shutil
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

VALID_TRACE_MIN_OWN_SHIP_SAMPLES = 20
VALID_TRACE_MIN_DURATION_S = 5.0

_SAFE_SEGMENT = re.compile(r"^[A-Za-z0-9_.-]+$")


@dataclass(frozen=True)
class EvidenceSession:
    session_name: str
    session_dir: Path


def _now() -> datetime:
    return datetime.now(timezone.utc).astimezone()


def _iso(dt: datetime) -> str:
    return dt.astimezone().isoformat(timespec="seconds")


def _safe(value: str) -> str:
    cleaned = value.strip().replace("/", "-").replace("\\", "-").replace(" ", "-")
    cleaned = re.sub(r"[^A-Za-z0-9_.-]+", "-", cleaned)
    cleaned = cleaned.strip("-")
    if not cleaned or not _SAFE_SEGMENT.match(cleaned):
        raise ValueError(f"Unsafe evidence path segment: {value!r}")
    return cleaned


def build_session_name(
    *,
    source: str,
    suite: str,
    scenario_id: str | None,
    created_at: datetime | None = None,
) -> str:
    dt = created_at or _now()
    prefix = dt.astimezone().strftime("%Y%m%d_%H%M%S")
    safe_suite = _safe(suite)
    if safe_suite in {"single", "frontend"}:
        if not scenario_id:
            raise ValueError(f"{safe_suite} evidence session requires scenario_id")
        return f"{prefix}_{safe_suite}_{_safe(scenario_id)}"
    if safe_suite in {"clean8", "clean12"}:
        return f"{prefix}_{safe_suite}"
    raise ValueError(f"Unsupported evidence suite: {suite!r}")


def _read_json(path: Path) -> dict[str, Any]:
    if not path.exists():
        return {}
    return json.loads(path.read_text())


def _write_json(path: Path, data: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n")


def validate_trace_jsonl(path: Path) -> dict[str, Any]:
    own_ship_times: list[float] = []
    if path.exists():
        with path.open() as f:
            for line in f:
                try:
                    record = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if record.get("topic") == "/sil/own_ship_state":
                    try:
                        own_ship_times.append(float(record.get("sim_t", 0.0)))
                    except (TypeError, ValueError):
                        continue
    duration = 0.0
    if own_ship_times:
        duration = round(max(own_ship_times) - min(own_ship_times), 3)
    valid = (
        len(own_ship_times) >= VALID_TRACE_MIN_OWN_SHIP_SAMPLES
        and duration >= VALID_TRACE_MIN_DURATION_S
    )
    return {
        "valid_data": bool(valid),
        "own_ship_samples": len(own_ship_times),
        "sim_t_duration_s": duration,
        "threshold_samples": VALID_TRACE_MIN_OWN_SHIP_SAMPLES,
        "threshold_duration_s": VALID_TRACE_MIN_DURATION_S,
    }


class EvidenceSessionManager:
    def __init__(
        self,
        root: Path = Path("runs/trace_eval"),
        run_root: Path = Path("runs"),
    ) -> None:
        self.root = Path(root)
        self.run_root = Path(run_root)

    def _unique_dir(self, name: str) -> Path:
        candidate = self.root / name
        if not candidate.exists():
            return candidate
        for idx in range(1, 100):
            suffixed = self.root / f"{name}_{idx:02d}"
            if not suffixed.exists():
                return suffixed
        raise RuntimeError(f"Cannot allocate unique evidence session name for {name}")

    def start(
        self,
        *,
        source: str,
        suite: str,
        scenario_id: str | None = None,
        created_at: datetime | None = None,
    ) -> EvidenceSession:
        source = _safe(source)
        suite = _safe(suite)
        base_name = build_session_name(
            source=source,
            suite=suite,
            scenario_id=scenario_id,
            created_at=created_at,
        )
        session_dir = self._unique_dir(base_name)
        session_dir.mkdir(parents=True, exist_ok=False)
        session = EvidenceSession(session_name=session_dir.name, session_dir=session_dir)
        manifest = {
            "schema_version": 1,
            "session_name": session.session_name,
            "source": source,
            "suite": suite,
            "created_at": _iso(created_at or _now()),
            "ended_at": None,
            "status": "pending",
            "valid_data": False,
            "validity": {
                "own_ship_samples": 0,
                "sim_t_duration_s": 0.0,
                "threshold_samples": VALID_TRACE_MIN_OWN_SHIP_SAMPLES,
                "threshold_duration_s": VALID_TRACE_MIN_DURATION_S,
            },
            "scenarios": [],
            "arrow": {
                "scoring_arrow_detected": False,
                "replay_arrow_detected": False,
                "scoring_arrow_path": None,
                "replay_arrow_path": None,
                "note": "Initial evidence is JSONL-first; Arrow replay is optional.",
            },
        }
        _write_json(session.session_dir / "manifest.json", manifest)
        return session

    def from_dir(self, session_dir: Path) -> EvidenceSession:
        session_dir = Path(session_dir)
        resolved_root = self.root.resolve()
        resolved_session = session_dir.resolve()
        resolved_session.relative_to(resolved_root)
        return EvidenceSession(session_name=session_dir.name, session_dir=session_dir)

    def _record_postprocess(self, session: EvidenceSession, entry: dict[str, Any]) -> None:
        path = session.session_dir / "logs" / "postprocess.jsonl"
        path.parent.mkdir(parents=True, exist_ok=True)
        enriched = {"wall_t": _iso(_now()), **entry}
        with path.open("a") as f:
            f.write(json.dumps(enriched, ensure_ascii=False, default=str) + "\n")

    def _detect_arrow(self, run_id: str | None) -> dict[str, Any]:
        if not run_id:
            return {
                "scoring_arrow_detected": False,
                "replay_arrow_detected": False,
                "scoring_arrow_path": None,
                "replay_arrow_path": None,
                "note": "Initial evidence is JSONL-first; Arrow replay is optional.",
            }
        run_dir = self.run_root / _safe(run_id)
        scoring_arrow = run_dir / "scoring.arrow"
        replay_arrow = run_dir / "replay.arrow"
        return {
            "scoring_arrow_detected": scoring_arrow.exists(),
            "replay_arrow_detected": replay_arrow.exists(),
            "scoring_arrow_path": str(scoring_arrow) if scoring_arrow.exists() else None,
            "replay_arrow_path": str(replay_arrow) if replay_arrow.exists() else None,
            "note": "Initial evidence is JSONL-first; Arrow replay is optional.",
        }

    def archive_scenario(
        self,
        session: EvidenceSession,
        scenario_id: str,
        *,
        trace_path: Path = Path("runs/trace_current.jsonl"),
        report_path: Path | None = None,
        status: str = "unknown",
        run_id: str | None = None,
    ) -> dict[str, Any]:
        safe_scenario = _safe(scenario_id)
        manifest_path = session.session_dir / "manifest.json"
        manifest = _read_json(manifest_path)
        scenario_entry: dict[str, Any] = {
            "scenario_id": safe_scenario,
            "status": status,
            "valid_data": False,
            "trace_path": None,
            "report_path": None,
            "png_path": None,
            "run_id": run_id,
        }
        trace_path = Path(trace_path)
        if trace_path.exists():
            dest = session.session_dir / f"{safe_scenario}.trace_current.jsonl"
            shutil.copyfile(trace_path, dest)
            validity = validate_trace_jsonl(dest)
            scenario_entry["valid_data"] = validity["valid_data"]
            scenario_entry["trace_path"] = dest.name
            scenario_entry["validity"] = validity
        else:
            scenario_entry["error"] = f"trace file not found: {trace_path}"
            self._record_postprocess(session, {
                "level": "error",
                "scenario_id": safe_scenario,
                "event": "trace_missing",
                "path": str(trace_path),
            })
        if report_path and Path(report_path).exists():
            report_dest = session.session_dir / f"{safe_scenario}.json"
            shutil.copyfile(report_path, report_dest)
            scenario_entry["report_path"] = report_dest.name
        scenario_entry["png_path"] = f"{safe_scenario}_trajectory_dashboard.png"
        scenarios = [s for s in manifest.get("scenarios", []) if s.get("scenario_id") != safe_scenario]
        scenarios.append(scenario_entry)
        manifest["scenarios"] = scenarios
        manifest["arrow"] = self._detect_arrow(run_id)
        validities = [
            s.get("validity", {})
            for s in scenarios
            if s.get("valid_data") and isinstance(s.get("validity"), dict)
        ]
        if validities:
            manifest["validity"] = {
                "own_ship_samples": sum(int(v.get("own_ship_samples", 0)) for v in validities),
                "sim_t_duration_s": round(sum(float(v.get("sim_t_duration_s", 0.0)) for v in validities), 3),
                "threshold_samples": VALID_TRACE_MIN_OWN_SHIP_SAMPLES,
                "threshold_duration_s": VALID_TRACE_MIN_DURATION_S,
            }
        _write_json(manifest_path, manifest)
        return scenario_entry

    def finalize(self, session: EvidenceSession, *, status: str) -> dict[str, Any] | None:
        manifest_path = session.session_dir / "manifest.json"
        manifest = _read_json(manifest_path)
        valid_count = sum(1 for s in manifest.get("scenarios", []) if s.get("valid_data"))
        if valid_count == 0:
            if session.session_dir.exists():
                shutil.rmtree(session.session_dir)
            return None
        manifest["status"] = status
        manifest["ended_at"] = _iso(_now())
        manifest["valid_data"] = True
        _write_json(manifest_path, manifest)
        return manifest
```

- [ ] **Step 4: Run tests to verify they pass**

Run:

```bash
pytest tests/tools/test_evidence_session.py -v
```

Expected: all tests PASS.

- [ ] **Step 5: Commit**

```bash
git add tools/sil/evidence_session.py tests/tools/test_evidence_session.py
git commit -m "feat(evidence): add session manager"
```

---

### Task 2: Trajectory Dashboard Generator

**Files:**
- Create: `tools/sil/trajectory_dashboard.py`
- Test: `tests/tools/test_trajectory_dashboard.py`

**Interfaces:**
- Consumes:
  - JSONL files archived by `EvidenceSessionManager.archive_scenario(...)`
  - optional report JSON produced by `scripts/run_6_scenarios.py`
- Produces:
  - `generate_trajectory_dashboard(trace_jsonl: Path, output_png: Path, scenario_id: str, session_name: str, report_json: Path | None = None) -> Path`

- [ ] **Step 1: Write the failing tests**

Create `tests/tools/test_trajectory_dashboard.py`:

```python
from __future__ import annotations

import json
from pathlib import Path

from tools.sil.trajectory_dashboard import generate_trajectory_dashboard


def _write_dashboard_trace(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w") as f:
        for i in range(40):
            sim_t = float(i)
            f.write(json.dumps({
                "sim_t": sim_t,
                "wall_t": 1000.0 + i,
                "topic": "/sil/own_ship_state",
                "lat": 63.44 + i * 0.00001,
                "lon": 10.38 + min(i, 20) * 0.00001,
                "heading_deg": 360.0 - min(i, 10) * 0.2,
                "sog_kn": 8.0,
            }) + "\n")
            f.write(json.dumps({
                "sim_t": sim_t,
                "wall_t": 1000.0 + i,
                "topic": "/l3/m4/behavior_plan",
                "behavior": 1 if 10 <= i <= 25 else 0,
                "avoidance_active": 10 <= i <= 25,
            }) + "\n")
            f.write(json.dumps({
                "sim_t": sim_t,
                "wall_t": 1000.0 + i,
                "topic": "/l3/m6/colregs_constraint",
                "conflict_detected": 10 <= i <= 25,
                "phase": "SOUND_WARNING" if 10 <= i <= 25 else "PRESERVE_COURSE",
                "primary_preferred_direction": "STARBOARD" if 10 <= i <= 25 else "HOLD",
            }) + "\n")
            f.write(json.dumps({
                "sim_t": sim_t,
                "wall_t": 1000.0 + i,
                "topic": "/l3/m5/avoidance_plan",
                "solver_status": "VALID" if 10 <= i <= 25 else "EMPTY",
            }) + "\n")
            f.write(json.dumps({
                "sim_t": sim_t,
                "wall_t": 1000.0 + i,
                "topic": "/sil/scoring",
                "total": 0.95,
            }) + "\n")
            f.write(json.dumps({
                "sim_t": sim_t,
                "wall_t": 1000.0 + i,
                "topic": "/sil/actuator_cmd",
                "rudder_deg": 5.0,
            }) + "\n")


def test_generate_dashboard_png_from_jsonl_and_report(tmp_path: Path):
    trace = tmp_path / "colreg-rule14-ho.trace_current.jsonl"
    report = tmp_path / "colreg-rule14-ho.json"
    output = tmp_path / "colreg-rule14-ho_trajectory_dashboard.png"
    _write_dashboard_trace(trace)
    report.write_text(json.dumps({
        "verdict": {
            "overall_pass": True,
            "safety_pass": True,
            "mission_pass": True,
            "colregs_pass": True,
            "stability_pass": True
        },
        "threshold_provenance": {
            "threshold_m": 180.0,
            "threshold_formula": "4.0L"
        },
        "layers": {
            "L1_scenario_validity": {"status": "UNKNOWN"},
            "L2_safety_floor": {"status": "PASS"}
        },
        "first_failure": None
    }))

    result = generate_trajectory_dashboard(
        trace_jsonl=trace,
        output_png=output,
        scenario_id="colreg-rule14-ho",
        session_name="20260622_153012_single_colreg-rule14-ho",
        report_json=report,
    )

    assert result == output
    assert output.exists()
    assert output.stat().st_size > 20_000


def test_generate_dashboard_png_without_report(tmp_path: Path):
    trace = tmp_path / "colreg-rule14-ho.trace_current.jsonl"
    output = tmp_path / "colreg-rule14-ho_trajectory_dashboard.png"
    _write_dashboard_trace(trace)

    result = generate_trajectory_dashboard(
        trace_jsonl=trace,
        output_png=output,
        scenario_id="colreg-rule14-ho",
        session_name="20260622_153012_single_colreg-rule14-ho",
        report_json=None,
    )

    assert result.exists()
    assert output.stat().st_size > 20_000
```

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
pytest tests/tools/test_trajectory_dashboard.py -v
```

Expected: FAIL with `ModuleNotFoundError: No module named 'tools.sil.trajectory_dashboard'`.

- [ ] **Step 3: Implement dashboard generator**

Create `tools/sil/trajectory_dashboard.py` using the known-good sample layout. The implementation must:

```python
from __future__ import annotations

import bisect
import json
import math
import statistics
from collections import Counter
from pathlib import Path
from typing import Any

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.collections import LineCollection
from matplotlib.lines import Line2D
from matplotlib.patches import FancyBboxPatch, Rectangle


R_EARTH_M = 6371008.8


def _read_jsonl(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    with path.open() as f:
        for line in f:
            try:
                records.append(json.loads(line))
            except json.JSONDecodeError:
                continue
    return records


def _fval(record: dict[str, Any], key: str, default: float = 0.0) -> float:
    try:
        value = record.get(key, default)
        return default if value is None else float(value)
    except (TypeError, ValueError):
        return default


def _geo_delta(ref: dict[str, Any], record: dict[str, Any]) -> tuple[float, float]:
    c = math.cos(math.radians(_fval(ref, "lat")))
    east = math.radians(_fval(record, "lon") - _fval(ref, "lon")) * R_EARTH_M * c
    north = math.radians(_fval(record, "lat") - _fval(ref, "lat")) * R_EARTH_M
    return east, north


def _geo_dist(a: dict[str, Any], b: dict[str, Any]) -> float:
    east, north = _geo_delta(a, b)
    return math.hypot(east, north)


def _dominant_ownship_segment(ownship: list[dict[str, Any]]) -> list[dict[str, Any]]:
    if not ownship:
        return []
    segments: list[list[dict[str, Any]]] = []
    current = [ownship[0]]
    for prev, cur in zip(ownship, ownship[1:]):
        if _geo_dist(prev, cur) > 1000.0:
            segments.append(current)
            current = [cur]
        else:
            current.append(cur)
    segments.append(current)
    return max(segments, key=len)


def _behavior_class(record: dict[str, Any]) -> str:
    behavior = int(record.get("behavior") or 0)
    active = bool(record.get("avoidance_active"))
    if behavior == 7:
        return "RECOVERY"
    if active or behavior in (1, 2):
        return "AVOIDANCE"
    return "TRANSIT"


def generate_trajectory_dashboard(
    *,
    trace_jsonl: Path,
    output_png: Path,
    scenario_id: str,
    session_name: str,
    report_json: Path | None = None,
) -> Path:
    records = _read_jsonl(Path(trace_jsonl))
    by_topic: dict[str, list[dict[str, Any]]] = {}
    for record in records:
        by_topic.setdefault(str(record.get("topic")), []).append(record)
    own = _dominant_ownship_segment(by_topic.get("/sil/own_ship_state", []))
    if len(own) < 2:
        raise ValueError(f"Not enough own-ship samples in {trace_jsonl}")

    report: dict[str, Any] = {}
    if report_json and Path(report_json).exists():
        report = json.loads(Path(report_json).read_text())

    ref = own[0]
    xs: list[float] = []
    ys: list[float] = []
    ts: list[float] = []
    hdgs: list[float] = []
    sogs: list[float] = []
    for row in own:
        east, north = _geo_delta(ref, row)
        xs.append(east)
        ys.append(north)
        ts.append(_fval(row, "sim_t"))
        hdgs.append(_fval(row, "heading_deg"))
        sogs.append(_fval(row, "sog_kn"))

    path_m = sum(_geo_dist(a, b) for a, b in zip(own, own[1:]))
    t0, t1 = ts[0], ts[-1]
    m4_all = by_topic.get("/l3/m4/behavior_plan", [])
    m4_times = [_fval(row, "sim_t") for row in m4_all]

    def class_at(t: float) -> str:
        idx = bisect.bisect_right(m4_times, t) - 1
        if idx < 0:
            return "TRANSIT"
        return _behavior_class(m4_all[idx])

    classes = [class_at(t) for t in ts]
    colors = {"TRANSIT": "#2563eb", "AVOIDANCE": "#dc2626", "RECOVERY": "#7c3aed"}
    points = list(zip(xs, ys))
    line_segments = [[points[i], points[i + 1]] for i in range(len(points) - 1)]
    line_colors = [colors[classes[i]] for i in range(len(line_segments))]

    transitions: list[tuple[float, str]] = []
    last_class: str | None = None
    for row in m4_all:
        rt = _fval(row, "sim_t")
        if t0 <= rt <= t1:
            klass = _behavior_class(row)
            if klass != last_class:
                transitions.append((rt, klass))
                last_class = klass

    m6 = [r for r in by_topic.get("/l3/m6/colregs_constraint", []) if t0 <= _fval(r, "sim_t") <= t1]
    m5 = [r for r in by_topic.get("/l3/m5/avoidance_plan", []) if t0 <= _fval(r, "sim_t") <= t1]
    scoring = [r for r in by_topic.get("/sil/scoring", []) if t0 <= _fval(r, "sim_t") <= t1]
    actuator = [r for r in by_topic.get("/sil/actuator_cmd", []) if t0 <= _fval(r, "sim_t") <= t1]

    xte = [abs(x) for x in xs]
    max_idx = max(range(len(xs)), key=lambda idx: xte[idx])
    phase_counter = Counter(str(r.get("phase")) for r in m6)
    direction_counter = Counter(str(r.get("primary_preferred_direction")) for r in m6)
    valid_m5 = sum(1 for r in m5 if str(r.get("solver_status")) == "VALID")
    empty_m5 = sum(1 for r in m5 if str(r.get("solver_status")) == "EMPTY")
    score_totals = [_fval(r, "total") for r in scoring]
    rudders = [abs(_fval(r, "rudder_deg")) for r in actuator]

    plt.rcParams.update({
        "font.family": "DejaVu Sans",
        "axes.unicode_minus": False,
        "axes.titlesize": 13,
        "axes.labelsize": 11,
        "xtick.labelsize": 9,
        "ytick.labelsize": 9,
    })
    fig = plt.figure(figsize=(15.4, 11.2), dpi=100, facecolor="#f8fafc")
    fig.suptitle(f"本船航线：{scenario_id}_{session_name}", fontsize=18, y=0.965, weight="bold", color="#0f172a")
    legend_ax = fig.add_axes([0.035, 0.115, 0.125, 0.775])
    legend_ax.axis("off")
    ax = fig.add_axes([0.185, 0.095, 0.295, 0.81])
    panel = fig.add_axes([0.525, 0.085, 0.43, 0.82])
    panel.axis("off")

    legend_ax.text(0.0, 0.98, "阶段注记", fontsize=12.5, weight="bold", color="#0f172a", va="top", transform=legend_ax.transAxes)
    y = 0.90
    for key, label in (("TRANSIT", "航行阶段"), ("AVOIDANCE", "避碰阶段"), ("RECOVERY", "恢复阶段")):
        legend_ax.add_line(Line2D([0.02, 0.32], [y, y], transform=legend_ax.transAxes, color=colors[key], lw=4, solid_capstyle="round"))
        legend_ax.text(0.38, y + 0.018, key, fontsize=9.5, weight="bold", color="#111827", va="center", transform=legend_ax.transAxes)
        legend_ax.text(0.38, y - 0.020, label, fontsize=8.5, color="#64748b", va="center", transform=legend_ax.transAxes)
        y -= 0.105
    for marker_y, color, label, marker in ((y, "#22c55e", "OS Start", "o"), (y - 0.065, "#2563eb", "OS End", "o"), (y - 0.13, "#f97316", "Max XTE", "D")):
        legend_ax.scatter([0.08], [marker_y], s=80, color=color, edgecolor="black", lw=.7, marker=marker, transform=legend_ax.transAxes, zorder=3)
        legend_ax.text(0.20, marker_y, label, fontsize=9.5, weight="bold", color="#111827", va="center", transform=legend_ax.transAxes)
    metric_y = 0.35
    legend_ax.text(0.0, metric_y, "轨迹摘要", fontsize=12.0, weight="bold", color="#0f172a", va="top", transform=legend_ax.transAxes)
    for idx, (key, value) in enumerate((
        ("points", f"{len(own):,}"),
        ("duration", f"{t1 - t0:.1f}s"),
        ("path", f"{path_m:.1f}m"),
        ("max XTE", f"{max(xte):.1f}m"),
    )):
        yy = metric_y - 0.055 - idx * 0.045
        legend_ax.text(0.0, yy, key, fontsize=8.5, color="#64748b", va="top", transform=legend_ax.transAxes)
        legend_ax.text(0.62, yy, value, fontsize=8.7, weight="bold", color="#111827", va="top", ha="right", transform=legend_ax.transAxes)

    ax.set_facecolor("white")
    for spine in ax.spines.values():
        spine.set_color("#334155")
        spine.set_linewidth(1)
    ax.grid(True, color="#cbd5e1", lw=.8)
    ax.add_collection(LineCollection(line_segments, colors=line_colors, linewidths=2.8, capstyle="round", joinstyle="round", zorder=4))
    ax.scatter([xs[0]], [ys[0]], s=88, color="#22c55e", edgecolor="black", lw=.8, zorder=8)
    ax.scatter([xs[-1]], [ys[-1]], s=88, color="#2563eb", edgecolor="black", lw=.8, zorder=8)
    ax.scatter([xs[max_idx]], [ys[max_idx]], s=70, marker="D", color="#f97316", edgecolor="black", lw=.6, zorder=8)
    ax.annotate(f"Max XTE {xte[max_idx]:.1f}m", xy=(xs[max_idx], ys[max_idx]), xytext=(15, -24), textcoords="offset points", fontsize=8.8, color="#9a3412", arrowprops=dict(arrowstyle="->", color="#f97316", lw=1))
    ax.set_xlim(min(min(xs), -55) - 35, max(max(xs), 55) + 110)
    ax.set_ylim(min(-75, min(ys) - 35), max(max(ys) + 130, 2550))
    ax.set_aspect("equal", adjustable="box")
    ax.set_xlabel("East from start (m)")
    ax.set_ylabel("North from start (m)")
    ax.set_title("Full Trace", pad=10)

    def status_color(ok: Any) -> str:
        return "#16a34a" if ok is True else ("#dc2626" if ok is False else "#64748b")

    def status_text(ok: Any) -> str:
        return "PASS" if ok is True else ("FAIL" if ok is False else "UNKNOWN")

    def panel_text(x: float, y0: float, text: str, size: float = 9, color: str = "#334155", weight: str | None = None, ha: str = "left") -> None:
        panel.text(x, y0, text, transform=panel.transAxes, fontsize=size, color=color, weight=weight, va="top", ha=ha)

    def card(top: float, height: float, title: str) -> float:
        panel.add_patch(FancyBboxPatch((0.02, top - height), 0.96, height, boxstyle="round,pad=0.010,rounding_size=0.012", lw=.9, edgecolor="#cbd5e1", facecolor="white", transform=panel.transAxes))
        panel.text(0.05, top - 0.032, title, transform=panel.transAxes, fontsize=12.5, weight="bold", color="#0f172a", va="top")
        return top - 0.068

    verdict = report.get("verdict", {})
    overall = verdict.get("overall_pass")
    panel.add_patch(Rectangle((0.02, 0.925), 0.96, 0.055, transform=panel.transAxes, facecolor=status_color(overall), edgecolor="none"))
    panel.text(0.05, 0.952, f"OVERALL {status_text(overall)}", transform=panel.transAxes, fontsize=15.5, weight="bold", color="white", va="center")
    panel.text(0.95, 0.952, scenario_id, transform=panel.transAxes, fontsize=9.5, color="white", va="center", ha="right")

    y0 = card(0.895, 0.205, "Core Run Data")
    core = [
        ("Trace points", f"{len(own):,}"),
        ("Sim time", f"{t0:.1f}s -> {t1:.1f}s ({t1 - t0:.1f}s)"),
        ("Path length", f"{path_m:.1f} m"),
        ("Max XTE", f"{max(xte):.1f} m"),
        ("Start", f"SOG {sogs[0]:.1f} kn | HDG {hdgs[0]:.1f} deg"),
        ("End", f"SOG {sogs[-1]:.1f} kn | HDG {hdgs[-1]:.1f} deg"),
    ]
    for idx, (key, value) in enumerate(core):
        yy = y0 - idx * .022
        panel_text(.05, yy, key, 8.5, "#64748b")
        panel_text(.36, yy, value, 8.8, "#111827", "bold")

    y0 = card(0.66, 0.300, "GATE + Layer Checks")
    for idx, (name, ok) in enumerate((
        ("Safety", verdict.get("safety_pass")),
        ("Mission", verdict.get("mission_pass")),
        ("COLREGs", verdict.get("colregs_pass")),
        ("Stability", verdict.get("stability_pass")),
    )):
        x0 = .05 + (idx % 2) * .47
        yy = y0 - (idx // 2) * .047
        panel.add_patch(Rectangle((x0, yy - .026), .14, .032, transform=panel.transAxes, facecolor=status_color(ok), edgecolor="none"))
        panel.text(x0 + .07, yy - .010, status_text(ok), transform=panel.transAxes, fontsize=8.2, color="white", weight="bold", ha="center", va="center")
        panel_text(x0 + .16, yy, name, 8.9, "#111827", "bold")
    provenance = report.get("threshold_provenance", {})
    panel_text(.05, y0 - .103, "CPA floor", 8.4, "#64748b")
    panel_text(.27, y0 - .103, f"{provenance.get('threshold_m', 'UNKNOWN')} m ({provenance.get('threshold_formula', 'UNKNOWN')})", 8.7, "#111827", "bold")
    panel_text(.55, y0 - .103, "First failure", 8.4, "#64748b")
    panel_text(.78, y0 - .103, str(report.get("first_failure") or "None"), 8.7, "#111827", "bold")
    layers = list(report.get("layers", {}).items())
    for idx, (layer_name, data) in enumerate(layers[:7]):
        col = 0 if idx < 4 else 1
        row = idx if idx < 4 else idx - 4
        x0 = .05 if col == 0 else .55
        stat_x = .44 if col == 0 else .94
        yy = y0 - .148 - row * .024
        status = data.get("status") if isinstance(data, dict) else data
        color = "#16a34a" if status == "PASS" else ("#dc2626" if status == "FAIL" else "#64748b")
        panel_text(x0, yy, layer_name.replace("_", " ")[:21], 7.8, "#334155")
        panel_text(stat_x, yy, str(status), 7.9, color, "bold", "right")

    y0 = card(0.335, 0.245, "Trace Signals")
    trans = " -> ".join(f"{klass.replace('AVOIDANCE', 'AVOID')}@{t:.0f}s" for t, klass in transitions)
    if len(trans) > 52:
        trans = trans[:49] + "..."
    signals = [
        ("M4 transitions", trans or "None"),
        ("M6 conflict", f"{sum(1 for r in m6 if r.get('conflict_detected'))}/{len(m6)} samples"),
        ("M6 phase", ", ".join(f"{k}:{v}" for k, v in phase_counter.most_common(2))),
        ("M6 direction", ", ".join(f"{k}:{v}" for k, v in direction_counter.most_common(2))),
        ("M5 solver", f"VALID {valid_m5} | EMPTY {empty_m5}"),
        ("Scoring total", f"avg {statistics.mean(score_totals):.3f} | min {min(score_totals):.3f}" if score_totals else "UNKNOWN"),
        ("Max rudder", f"{max(rudders):.1f} deg" if rudders else "UNKNOWN"),
    ]
    for idx, (key, value) in enumerate(signals):
        yy = y0 - idx * .024
        panel_text(.05, yy, key, 8.3, "#64748b")
        panel_text(.33, yy, value, 8.3, "#111827", "bold")

    output_png.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output_png, facecolor=fig.get_facecolor())
    plt.close(fig)
    return output_png
```

- [ ] **Step 4: Run tests to verify they pass**

Run:

```bash
pytest tests/tools/test_trajectory_dashboard.py -v
```

Expected: all tests PASS and generated PNG files exceed `20_000` bytes.

- [ ] **Step 5: Commit**

```bash
git add tools/sil/trajectory_dashboard.py tests/tools/test_trajectory_dashboard.py
git commit -m "feat(evidence): generate trajectory dashboards"
```

---

### Task 3: CLI Runner Evidence Sessions

**Files:**
- Modify: `scripts/run_6_scenarios.py`
- Test: `tests/scripts/test_run_6_scenarios_gate.py`

**Interfaces:**
- Consumes:
  - `EvidenceSessionManager` from Task 1.
  - `generate_trajectory_dashboard(...)` from Task 2.
- Produces:
  - Automatic `runs/trace_eval/<timestamp>_<suite>/` when `--trace-report-dir` is not supplied.
  - `batch_summary.json` inside the evidence session.

- [ ] **Step 1: Write failing tests**

Append tests to `tests/scripts/test_run_6_scenarios_gate.py`:

```python
from __future__ import annotations

import json
from pathlib import Path


def test_clean8_auto_trace_report_dir(monkeypatch, tmp_path):
    import scripts.run_6_scenarios as runner

    monkeypatch.chdir(tmp_path)
    (tmp_path / "runs").mkdir()
    trace = tmp_path / "runs" / "trace_current.jsonl"
    trace.write_text("\n".join(
        json.dumps({
            "sim_t": float(i),
            "wall_t": 1000.0 + i,
            "topic": "/sil/own_ship_state",
            "lat": 63.44 + i * 0.00001,
            "lon": 10.38,
            "heading_deg": 360.0,
            "sog_kn": 8.0,
        })
        for i in range(25)
    ) + "\n")
    monkeypatch.setattr(runner, "SCENARIOS", ["colreg-rule14-ho"])
    monkeypatch.setattr(runner, "ALL_SCENARIOS", ["colreg-rule14-ho"])
    monkeypatch.setattr(runner, "_load_expected_outcome", lambda scenario_id: {})
    monkeypatch.setattr(runner, "run_scenario", lambda scenario_id, total_time_override=None: {
        "run_id": "run-test",
        "overall_pass": True,
        "safety_pass": True,
        "mission_pass": True,
        "colregs_pass": True,
        "stability_pass": True,
        "min_cpa_m": 200.0,
        "cpa_ok": True,
        "cpa_floor_m": 180.0,
        "steer_dir": "starboard",
        "steer_mag": 30.0,
        "stability_kpis": {},
        "stability_checks": {},
        "role": "give_way",
    })
    monkeypatch.setattr(runner, "generate_trajectory_dashboard", lambda **kwargs: kwargs["output_png"].write_text("png") or kwargs["output_png"])

    rc = runner.main([])

    assert rc == 0
    sessions = list((tmp_path / "runs" / "trace_eval").glob("*_clean8"))
    assert len(sessions) == 1
    assert (sessions[0] / "batch_summary.json").exists()
    assert (sessions[0] / "colreg-rule14-ho.trace_current.jsonl").exists()
    assert (sessions[0] / "colreg-rule14-ho_trajectory_dashboard.png").exists()


def test_explicit_trace_report_dir_is_preserved(monkeypatch, tmp_path):
    import scripts.run_6_scenarios as runner

    monkeypatch.chdir(tmp_path)
    (tmp_path / "runs").mkdir()
    trace = tmp_path / "runs" / "trace_current.jsonl"
    trace.write_text("\n".join(
        json.dumps({
            "sim_t": float(i),
            "wall_t": 1000.0 + i,
            "topic": "/sil/own_ship_state",
            "lat": 63.44 + i * 0.00001,
            "lon": 10.38,
        })
        for i in range(25)
    ) + "\n")
    monkeypatch.setattr(runner, "SCENARIOS", ["colreg-rule14-ho"])
    monkeypatch.setattr(runner, "ALL_SCENARIOS", ["colreg-rule14-ho"])
    monkeypatch.setattr(runner, "_load_expected_outcome", lambda scenario_id: {})
    monkeypatch.setattr(runner, "run_scenario", lambda scenario_id, total_time_override=None: {
        "run_id": "run-test",
        "overall_pass": True,
        "safety_pass": True,
        "mission_pass": True,
        "colregs_pass": True,
        "stability_pass": True,
        "min_cpa_m": 200.0,
        "cpa_ok": True,
        "cpa_floor_m": 180.0,
        "steer_dir": "starboard",
        "steer_mag": 30.0,
        "stability_kpis": {},
        "stability_checks": {},
        "role": "give_way",
    })
    monkeypatch.setattr(runner, "generate_trajectory_dashboard", lambda **kwargs: kwargs["output_png"].write_text("png") or kwargs["output_png"])

    rc = runner.main(["--trace-report-dir", "runs/trace_eval/manual_dir"])

    assert rc == 0
    assert (tmp_path / "runs" / "trace_eval" / "manual_dir" / "batch_summary.json").exists()
```

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
pytest tests/scripts/test_run_6_scenarios_gate.py::test_clean8_auto_trace_report_dir tests/scripts/test_run_6_scenarios_gate.py::test_explicit_trace_report_dir_is_preserved -v
```

Expected: FAIL because `scripts/run_6_scenarios.py` does not auto-create evidence sessions or import `generate_trajectory_dashboard`.

- [ ] **Step 3: Modify runner imports**

In `scripts/run_6_scenarios.py`, add:

```python
from tools.sil.evidence_session import EvidenceSession, EvidenceSessionManager
from tools.sil.trajectory_dashboard import generate_trajectory_dashboard
```

- [ ] **Step 4: Add suite selection helper**

In `scripts/run_6_scenarios.py`, near `_parse_args`, add:

```python
def _evidence_suite(args, scenarios):
    if args.include_intelligent:
        return "clean12", None
    if len(scenarios) == 1:
        return "single", scenarios[0]
    return "clean8", None


def _create_evidence_session(args, scenarios):
    manager = EvidenceSessionManager()
    if args.trace_report_dir:
        session_dir = Path(args.trace_report_dir)
        session_dir.mkdir(parents=True, exist_ok=True)
        manifest = session_dir / "manifest.json"
        if not manifest.exists():
            suite, scenario_id = _evidence_suite(args, scenarios)
            session = manager.start(source="cli", suite=suite, scenario_id=scenario_id)
            if session.session_dir != session_dir:
                if session.session_dir.exists():
                    shutil.rmtree(session.session_dir)
                session_dir.mkdir(parents=True, exist_ok=True)
                session = EvidenceSession(session_name=session_dir.name, session_dir=session_dir)
        else:
            session = EvidenceSession(session_name=session_dir.name, session_dir=session_dir)
        return manager, session
    suite, scenario_id = _evidence_suite(args, scenarios)
    return manager, manager.start(source="cli", suite=suite, scenario_id=scenario_id)
```

- [ ] **Step 5: Archive and dashboard after each scenario**

In `main(argv=None)`, after scenarios are finalized and before the scenario loop, create session:

```python
    evidence_manager, evidence_session = _create_evidence_session(args, scenarios)
    args.trace_report_dir = str(evidence_session.session_dir)
```

Replace the per-scenario report archival block with:

```python
                report_path = _write_trace_evaluation_report(
                    scen, res, args.trace_report_dir)
                if report_path:
                    res["trace_evaluation_report_path"] = report_path
                    report_data = json.loads(Path(report_path).read_text())
                    verdict = report_data["verdict"]
                    res["safety_pass"] = verdict["safety_pass"]
                    res["mission_pass"] = verdict["mission_pass"]
                    res["colregs_pass"] = verdict["colregs_pass"]
                    res["traceeval_stability_pass"] = verdict["stability_pass"]
                    res["traceeval_overall_pass"] = verdict["overall_pass"]
                scenario_entry = evidence_manager.archive_scenario(
                    evidence_session,
                    scen,
                    trace_path=Path("runs/trace_current.jsonl"),
                    report_path=Path(report_path) if report_path else None,
                    status="pass" if res.get("overall_pass") else "fail",
                    run_id=res.get("run_id"),
                )
                if scenario_entry.get("valid_data"):
                    try:
                        generate_trajectory_dashboard(
                            trace_jsonl=evidence_session.session_dir / scenario_entry["trace_path"],
                            report_json=(evidence_session.session_dir / scenario_entry["report_path"]) if scenario_entry.get("report_path") else None,
                            output_png=evidence_session.session_dir / scenario_entry["png_path"],
                            scenario_id=scen,
                            session_name=evidence_session.session_name,
                        )
                    except Exception as exc:
                        evidence_manager._record_postprocess(evidence_session, {
                            "level": "error",
                            "scenario_id": scen,
                            "event": "dashboard_failed",
                            "error": str(exc),
                        })
```

After writing summary, also copy it into session and finalize:

```python
    session_summary_path = evidence_session.session_dir / "batch_summary.json"
    with open(session_summary_path, "w") as f:
        json.dump(results, f, indent=2)
    evidence_manager.finalize(
        evidence_session,
        status="completed" if results else "error",
    )
```

- [ ] **Step 6: Run tests**

Run:

```bash
pytest tests/scripts/test_run_6_scenarios_gate.py::test_clean8_auto_trace_report_dir tests/scripts/test_run_6_scenarios_gate.py::test_explicit_trace_report_dir_is_preserved -v
```

Expected: both tests PASS.

- [ ] **Step 7: Commit**

```bash
git add scripts/run_6_scenarios.py tests/scripts/test_run_6_scenarios_gate.py
git commit -m "feat(evidence): archive clean probe sessions"
```

---

### Task 4: FastAPI Evidence Routes

**Files:**
- Create: `src/sil_orchestrator/evidence_routes.py`
- Modify: `src/sil_orchestrator/main.py`
- Test: `src/sil_orchestrator/tests/test_evidence_routes.py`

**Interfaces:**
- Consumes:
  - `EvidenceSessionManager`
  - `generate_trajectory_dashboard`
- Produces:
  - `POST /api/v1/evidence/session/start`
  - `POST /api/v1/evidence/session/{session_id}/finalize`
  - `GET /api/v1/evidence/session/{session_id}`
  - `GET /api/v1/evidence/sessions?limit=50`

- [ ] **Step 1: Write failing API tests**

Create `src/sil_orchestrator/tests/test_evidence_routes.py`:

```python
from __future__ import annotations

import json
from pathlib import Path

import pytest
from httpx import ASGITransport, AsyncClient
from fastapi import FastAPI

from sil_orchestrator import evidence_routes


def _write_trace(path: Path, samples: int = 25, duration_s: float = 10.0) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w") as f:
        for i in range(samples):
            f.write(json.dumps({
                "sim_t": duration_s * i / max(1, samples - 1),
                "wall_t": 1000.0 + i,
                "topic": "/sil/own_ship_state",
                "lat": 63.44 + i * 0.00001,
                "lon": 10.38,
            }) + "\n")


@pytest.mark.asyncio
async def test_start_finalize_get_and_list_session(tmp_path, monkeypatch):
    monkeypatch.setattr(evidence_routes, "RUN_DIR", tmp_path / "runs")
    monkeypatch.setattr(evidence_routes, "TRACE_EVAL_DIR", tmp_path / "runs" / "trace_eval")
    monkeypatch.setattr(evidence_routes, "generate_trajectory_dashboard", lambda **kwargs: kwargs["output_png"].write_text("png") or kwargs["output_png"])
    _write_trace(tmp_path / "runs" / "trace_current.jsonl")
    app = FastAPI()
    app.include_router(evidence_routes.router)
    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        start = await client.post("/api/v1/evidence/session/start", json={
            "source": "frontend",
            "suite": "frontend",
            "scenario_id": "colreg-rule14-ho",
        })
        assert start.status_code == 200
        session_id = start.json()["session_id"]
        fin = await client.post(f"/api/v1/evidence/session/{session_id}/finalize", json={
            "scenario_id": "colreg-rule14-ho",
            "status": "stopped",
            "run_id": "run-test",
        })
        assert fin.status_code == 200
        assert fin.json()["valid_data"] is True
        got = await client.get(f"/api/v1/evidence/session/{session_id}")
        assert got.status_code == 200
        assert got.json()["session_name"] == session_id
        listed = await client.get("/api/v1/evidence/sessions?limit=5")
        assert listed.status_code == 200
        assert listed.json()["sessions"][0]["session_name"] == session_id


@pytest.mark.asyncio
async def test_finalize_discards_empty_frontend_session(tmp_path, monkeypatch):
    monkeypatch.setattr(evidence_routes, "RUN_DIR", tmp_path / "runs")
    monkeypatch.setattr(evidence_routes, "TRACE_EVAL_DIR", tmp_path / "runs" / "trace_eval")
    _write_trace(tmp_path / "runs" / "trace_current.jsonl", samples=2, duration_s=1.0)
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
            "status": "stopped",
        })
        assert fin.status_code == 200
        assert fin.json()["discarded"] is True
        assert not (tmp_path / "runs" / "trace_eval" / session_id).exists()


@pytest.mark.asyncio
async def test_rejects_path_traversal(tmp_path, monkeypatch):
    monkeypatch.setattr(evidence_routes, "RUN_DIR", tmp_path / "runs")
    monkeypatch.setattr(evidence_routes, "TRACE_EVAL_DIR", tmp_path / "runs" / "trace_eval")
    app = FastAPI()
    app.include_router(evidence_routes.router)
    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        resp = await client.get("/api/v1/evidence/session/..%2Fescape")
        assert resp.status_code == 400
```

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
pytest src/sil_orchestrator/tests/test_evidence_routes.py -v
```

Expected: FAIL because `sil_orchestrator.evidence_routes` does not exist.

- [ ] **Step 3: Implement routes**

Create `src/sil_orchestrator/evidence_routes.py`:

```python
from __future__ import annotations

import json
import threading
from pathlib import Path
from typing import Any

from fastapi import APIRouter, BackgroundTasks, HTTPException

from sil_orchestrator.config import RUN_DIR
from tools.sil.evidence_session import EvidenceSessionManager
from tools.sil.trajectory_dashboard import generate_trajectory_dashboard

TRACE_EVAL_DIR = RUN_DIR / "trace_eval"

router = APIRouter(prefix="/api/v1/evidence", tags=["evidence"])
_session_lock = threading.Lock()
_active_frontend_session: str | None = None


def _manager() -> EvidenceSessionManager:
    return EvidenceSessionManager(root=TRACE_EVAL_DIR, run_root=RUN_DIR)


def _resolve_session(session_id: str) -> Path:
    try:
        target = (TRACE_EVAL_DIR / session_id).resolve()
        target.relative_to(TRACE_EVAL_DIR.resolve())
    except ValueError as exc:
        raise HTTPException(status_code=400, detail="Invalid session_id") from exc
    if not target.exists():
        raise HTTPException(status_code=404, detail="Evidence session not found")
    return target


def _build_png(session_dir: Path, scenario_entry: dict[str, Any], scenario_id: str, session_name: str) -> None:
    trace_name = scenario_entry.get("trace_path")
    if not trace_name:
        return
    trace_path = session_dir / trace_name
    report_path = session_dir / scenario_entry["report_path"] if scenario_entry.get("report_path") else None
    output_png = session_dir / scenario_entry.get("png_path", f"{scenario_id}_trajectory_dashboard.png")
    generate_trajectory_dashboard(
        trace_jsonl=trace_path,
        report_json=report_path,
        output_png=output_png,
        scenario_id=scenario_id,
        session_name=session_name,
    )


@router.post("/session/start")
async def start_session(request: dict):
    global _active_frontend_session
    source = str(request.get("source", "frontend"))
    suite = str(request.get("suite", "frontend"))
    scenario_id = request.get("scenario_id")
    session = _manager().start(source=source, suite=suite, scenario_id=scenario_id)
    if source == "frontend":
        with _session_lock:
            _active_frontend_session = session.session_name
    manifest = json.loads((session.session_dir / "manifest.json").read_text())
    return {
        "session_id": session.session_name,
        "session_name": session.session_name,
        "path": str(session.session_dir),
        "manifest": manifest,
    }


@router.post("/session/{session_id}/finalize")
async def finalize_session(session_id: str, request: dict, background_tasks: BackgroundTasks):
    global _active_frontend_session
    session_dir = _resolve_session(session_id)
    mgr = _manager()
    session = mgr.from_dir(session_dir)
    scenario_id = str(request.get("scenario_id", "unknown"))
    status = str(request.get("status", "stopped"))
    run_id = request.get("run_id")
    report_path = request.get("report_path")
    scenario_entry = mgr.archive_scenario(
        session,
        scenario_id,
        trace_path=RUN_DIR / "trace_current.jsonl",
        report_path=Path(report_path) if report_path else None,
        status=status,
        run_id=str(run_id) if run_id else None,
    )
    manifest = mgr.finalize(session, status=status)
    if manifest is None:
        if _active_frontend_session == session_id:
            with _session_lock:
                _active_frontend_session = None
        return {"discarded": True, "session_id": session_id, "valid_data": False}
    if scenario_entry.get("valid_data"):
        background_tasks.add_task(_build_png, session.session_dir, scenario_entry, scenario_id, session.session_name)
    if _active_frontend_session == session_id:
        with _session_lock:
            _active_frontend_session = None
    return {"discarded": False, **manifest}


@router.get("/session/{session_id}")
async def get_session(session_id: str):
    session_dir = _resolve_session(session_id)
    manifest_path = session_dir / "manifest.json"
    if not manifest_path.exists():
        raise HTTPException(status_code=404, detail="manifest.json not found")
    return json.loads(manifest_path.read_text())


@router.get("/sessions")
async def list_sessions(limit: int = 50):
    limit = max(1, min(int(limit), 200))
    if not TRACE_EVAL_DIR.exists():
        return {"sessions": []}
    manifests = []
    for manifest_path in sorted(TRACE_EVAL_DIR.glob("*/manifest.json"), key=lambda p: p.stat().st_mtime, reverse=True):
        try:
            manifests.append(json.loads(manifest_path.read_text()))
        except Exception:
            continue
        if len(manifests) >= limit:
            break
    return {"sessions": manifests}
```

- [ ] **Step 4: Include router**

Modify `src/sil_orchestrator/main.py`:

```python
from sil_orchestrator.evidence_routes import router as evidence_router
```

and add near other includes:

```python
app.include_router(evidence_router)
```

- [ ] **Step 5: Run tests**

Run:

```bash
pytest src/sil_orchestrator/tests/test_evidence_routes.py -v
```

Expected: all tests PASS.

- [ ] **Step 6: Commit**

```bash
git add src/sil_orchestrator/evidence_routes.py src/sil_orchestrator/main.py src/sil_orchestrator/tests/test_evidence_routes.py
git commit -m "feat(evidence): add orchestrator session routes"
```

---

### Task 5: Frontend Launch Integration

**Files:**
- Modify: `web/src/api/silApi.ts`
- Modify: `web/src/screens/SimulationCheck.tsx`
- Test: `web/src/screens/__tests__/SimulationCheck.runtime.test.tsx`

**Interfaces:**
- Consumes:
  - `POST /api/v1/evidence/session/start`
  - `POST /api/v1/evidence/session/{session_id}/finalize`
- Produces:
  - A new evidence session per frontend start click.
  - Session finalization on launch failure/abort path.

- [ ] **Step 1: Add failing frontend test**

In `web/src/screens/__tests__/SimulationCheck.runtime.test.tsx`, add mocks:

```ts
const evidenceMocks = {
  startEvidenceSession: vi.fn(),
  finalizeEvidenceSession: vi.fn(),
};
```

Extend the `web/src/api/silApi.ts` mock:

```ts
useStartEvidenceSessionMutation: () => [evidenceMocks.startEvidenceSession],
useFinalizeEvidenceSessionMutation: () => [evidenceMocks.finalizeEvidenceSession],
```

Add test:

```ts
it('creates a frontend evidence session before lifecycle configure and activate', async () => {
  evidenceMocks.startEvidenceSession.mockReturnValue({
    unwrap: () => Promise.resolve({ session_id: '20260622_153012_frontend_safe_route' }),
  });
  evidenceMocks.finalizeEvidenceSession.mockReturnValue({
    unwrap: () => Promise.resolve({ discarded: false }),
  });
  mocks.cleanupLifecycle.mockReturnValue({ unwrap: () => Promise.resolve({ success: true }) });
  mocks.configureLifecycle.mockReturnValue({ unwrap: () => Promise.resolve({ success: true }) });
  mocks.activateLifecycle.mockReturnValue({ unwrap: () => Promise.resolve({ success: true, run_id: 'run-test' }) });

  render(<SimulationCheck />);
  await userEvent.click(screen.getByRole('button', { name: /start/i }));

  await waitFor(() => expect(evidenceMocks.startEvidenceSession).toHaveBeenCalledWith({
    source: 'frontend',
    suite: 'frontend',
    scenario_id: 'safe_route',
  }));
  expect(mocks.configureLifecycle).toHaveBeenCalledWith('safe_route');
});
```

Add test for failed configure:

```ts
it('finalizes frontend evidence session when configure fails', async () => {
  evidenceMocks.startEvidenceSession.mockReturnValue({
    unwrap: () => Promise.resolve({ session_id: '20260622_153012_frontend_safe_route' }),
  });
  evidenceMocks.finalizeEvidenceSession.mockReturnValue({
    unwrap: () => Promise.resolve({ discarded: true }),
  });
  mocks.cleanupLifecycle.mockReturnValue({ unwrap: () => Promise.resolve({ success: true }) });
  mocks.configureLifecycle.mockReturnValue({ unwrap: () => Promise.resolve({ success: false, error: 'bad config' }) });

  render(<SimulationCheck />);
  await userEvent.click(screen.getByRole('button', { name: /start/i }));

  await waitFor(() => expect(evidenceMocks.finalizeEvidenceSession).toHaveBeenCalledWith({
    sessionId: '20260622_153012_frontend_safe_route',
    scenario_id: 'safe_route',
    status: 'error',
  }));
});
```

- [ ] **Step 2: Run frontend test to verify it fails**

Run:

```bash
cd web
npm test -- SimulationCheck.runtime.test.tsx --runInBand
```

Expected: FAIL because evidence API hooks do not exist.

- [ ] **Step 3: Add RTK Query endpoints**

Modify `web/src/api/silApi.ts`:

```ts
export interface EvidenceSessionStartRequest {
  source: 'frontend' | 'cli';
  suite: 'frontend' | 'single' | 'clean8' | 'clean12';
  scenario_id?: string;
}

export interface EvidenceSessionStartResponse {
  session_id: string;
  session_name: string;
  path: string;
  manifest: Record<string, unknown>;
}

export interface EvidenceSessionFinalizeRequest {
  sessionId: string;
  scenario_id: string;
  status: 'completed' | 'stopped' | 'error';
  run_id?: string;
}
```

Inside endpoint builder:

```ts
startEvidenceSession: builder.mutation<EvidenceSessionStartResponse, EvidenceSessionStartRequest>({
  query: (body) => ({
    url: '/evidence/session/start',
    method: 'POST',
    body,
  }),
}),

finalizeEvidenceSession: builder.mutation<Record<string, unknown>, EvidenceSessionFinalizeRequest>({
  query: ({ sessionId, ...body }) => ({
    url: `/evidence/session/${encodeURIComponent(sessionId)}/finalize`,
    method: 'POST',
    body,
  }),
}),
```

Export generated hooks:

```ts
useStartEvidenceSessionMutation,
useFinalizeEvidenceSessionMutation,
```

- [ ] **Step 4: Wire SimulationCheck start/failure paths**

Modify `web/src/screens/SimulationCheck.tsx` imports/hooks:

```ts
const [startEvidenceSession] = useStartEvidenceSessionMutation();
const [finalizeEvidenceSession] = useFinalizeEvidenceSessionMutation();
```

Before `cleanupLifecycle()` in the start handler:

```ts
let evidenceSessionId: string | undefined;
try {
  const evidence = await startEvidenceSession({
    source: 'frontend',
    suite: 'frontend',
    scenario_id: scenarioId,
  }).unwrap();
  evidenceSessionId = evidence.session_id;
} catch (e) {
  setLifecycleError(`Evidence session failed: ${e instanceof Error ? e.message : String(e)}`);
  setTransitioning(false);
  proceedingRef.current = false;
  return;
}
```

On configure failure:

```ts
if (evidenceSessionId) {
  try {
    await finalizeEvidenceSession({
      sessionId: evidenceSessionId,
      scenario_id: scenarioId,
      status: 'error',
    }).unwrap();
  } catch {}
}
```

On activate failure use the same finalization block with `status: 'error'`.

On activate success store `evidenceSessionId` in a ref:

```ts
activeEvidenceSessionRef.current = evidenceSessionId;
```

In `handleAbort`, before navigation:

```ts
if (activeEvidenceSessionRef.current && scenarioId) {
  try {
    await finalizeEvidenceSession({
      sessionId: activeEvidenceSessionRef.current,
      scenario_id: scenarioId,
      status: 'stopped',
    }).unwrap();
  } catch {}
  activeEvidenceSessionRef.current = undefined;
}
```

- [ ] **Step 5: Run frontend test**

Run:

```bash
cd web
npm test -- SimulationCheck.runtime.test.tsx --runInBand
```

Expected: affected tests PASS.

- [ ] **Step 6: Commit**

```bash
git add web/src/api/silApi.ts web/src/screens/SimulationCheck.tsx web/src/screens/__tests__/SimulationCheck.runtime.test.tsx
git commit -m "feat(evidence): create sessions from frontend launches"
```

---

### Task 6: End-to-End Verification

**Files:**
- No new source files.
- Evidence outputs under `runs/trace_eval/`.

**Interfaces:**
- Consumes all tasks.
- Produces verified evidence folders for single, clean8, clean12, and frontend flow.

- [ ] **Step 1: Run Python unit tests**

Run:

```bash
pytest tests/tools/test_evidence_session.py tests/tools/test_trajectory_dashboard.py src/sil_orchestrator/tests/test_evidence_routes.py -v
```

Expected: all tests PASS.

- [ ] **Step 2: Run targeted runner tests**

Run:

```bash
pytest tests/scripts/test_run_6_scenarios_gate.py -v
```

Expected: all existing gate tests plus new evidence tests PASS.

- [ ] **Step 3: Run frontend test**

Run:

```bash
cd web
npm test -- SimulationCheck.runtime.test.tsx --runInBand
```

Expected: PASS.

- [ ] **Step 4: Run single probe evidence smoke**

Run from worktree root:

```bash
python3 scripts/run_colregs_clean_8probe.py --scenario colreg-rule14-ho
```

Expected:

```text
runs/trace_eval/<timestamp>_single_colreg-rule14-ho/
  manifest.json
  batch_summary.json
  colreg-rule14-ho.json
  colreg-rule14-ho.trace_current.jsonl
  colreg-rule14-ho_trajectory_dashboard.png
```

- [ ] **Step 5: Run clean 8-probe evidence smoke**

Run:

```bash
python3 scripts/run_colregs_clean_8probe.py
```

Expected:

```text
runs/trace_eval/<timestamp>_clean8/
  manifest.json
  batch_summary.json
  one JSONL per completed valid scenario
  one PNG per completed valid scenario
```

- [ ] **Step 6: Run clean 12-probe evidence smoke**

Run:

```bash
python3 scripts/run_colregs_clean_8probe.py --include-intelligent --restart-between-runs
```

Expected:

```text
runs/trace_eval/<timestamp>_clean12/
  manifest.json
  batch_summary.json
  one JSONL per completed valid scenario
  one PNG per completed valid scenario
```

- [ ] **Step 7: Verify no empty session folders remain**

Run:

```bash
python3 - <<'PY'
import json
from pathlib import Path
bad = []
for manifest in Path("runs/trace_eval").glob("*/manifest.json"):
    data = json.loads(manifest.read_text())
    if not data.get("valid_data"):
        bad.append(str(manifest.parent))
print("\n".join(bad))
raise SystemExit(1 if bad else 0)
PY
```

Expected: no output and exit code `0`.

- [ ] **Step 8: Commit verification notes if docs are updated**

If implementation adds a handoff or evidence note, commit it:

```bash
git add handoff/workspace_log.md
git commit -m "docs(evidence): record session verification"
```

Skip this commit when no docs were changed.

---

## Self-Review

Spec coverage:

- Unified folder per launch: Task 1, Task 3, Task 4, Task 5.
- Timestamp-first naming: Task 1 tests and implementation.
- Valid data discard rule: Task 1, Task 4, Task 6.
- JSONL archive: Task 1 and Task 3.
- PNG generation: Task 2 and Task 3/4 integration.
- CLI single/8/12: Task 3 and Task 6.
- Frontend click: Task 4 and Task 5.
- Arrow optional metadata: Task 1.
- No COLREG behavior changes: Global Constraints and file exclusion list.

Placeholder scan:

- No placeholder markers.
- No unfinished work markers.
- No undefined future interfaces; function names are defined in Task 1 and Task 2 before consumers.

Type consistency:

- `EvidenceSessionManager.start(...) -> EvidenceSession` is used consistently.
- `EvidenceSessionManager.archive_scenario(...) -> dict` is used consistently.
- `generate_trajectory_dashboard(...) -> Path` is used consistently.
- Frontend request fields use `sessionId` in TypeScript and convert to URL path, while API uses `{session_id}` path parameter.
