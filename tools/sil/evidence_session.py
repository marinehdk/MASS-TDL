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
    if dt.tzinfo is None:
        return dt.isoformat(timespec="seconds")
    return dt.astimezone().isoformat(timespec="seconds")


def _timestamp(dt: datetime) -> str:
    if dt.tzinfo is None:
        return dt.strftime("%Y%m%d_%H%M%S")
    return dt.astimezone().strftime("%Y%m%d_%H%M%S")


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
    del source
    dt = created_at or _now()
    prefix = _timestamp(dt)
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
                if record.get("topic") != "/sil/own_ship_state":
                    continue
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

    def _initial_manifest(
        self,
        *,
        session_name: str,
        source: str,
        suite: str,
        created_at: datetime,
    ) -> dict[str, Any]:
        return {
            "schema_version": 1,
            "session_name": session_name,
            "source": source,
            "suite": suite,
            "created_at": _iso(created_at),
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
        created = created_at or _now()
        base_name = build_session_name(
            source=source,
            suite=suite,
            scenario_id=scenario_id,
            created_at=created,
        )
        session_dir = self._unique_dir(base_name)
        session_dir.mkdir(parents=True, exist_ok=False)
        session = EvidenceSession(session_name=session_dir.name, session_dir=session_dir)
        manifest = self._initial_manifest(
            session_name=session.session_name,
            source=source,
            suite=suite,
            created_at=created,
        )
        _write_json(session.session_dir / "manifest.json", manifest)
        return session

    def initialize_existing(
        self,
        session_dir: Path,
        *,
        source: str,
        suite: str,
        created_at: datetime | None = None,
    ) -> EvidenceSession:
        session_dir = Path(session_dir)
        session_dir.mkdir(parents=True, exist_ok=True)
        session = EvidenceSession(session_name=session_dir.name, session_dir=session_dir)
        manifest_path = session.session_dir / "manifest.json"
        if not manifest_path.exists():
            _write_json(
                manifest_path,
                self._initial_manifest(
                    session_name=session.session_name,
                    source=_safe(source),
                    suite=_safe(suite),
                    created_at=created_at or _now(),
                ),
            )
        return session

    def from_dir(self, session_dir: Path) -> EvidenceSession:
        session_dir = Path(session_dir)
        resolved_root = self.root.resolve()
        resolved_session = session_dir.resolve()
        resolved_session.relative_to(resolved_root)
        return EvidenceSession(session_name=session_dir.name, session_dir=session_dir)

    def record_postprocess(self, session: EvidenceSession, entry: dict[str, Any]) -> None:
        path = session.session_dir / "logs" / "postprocess.jsonl"
        path.parent.mkdir(parents=True, exist_ok=True)
        enriched = {"wall_t": _iso(_now()), **entry}
        with path.open("a") as f:
            f.write(json.dumps(enriched, ensure_ascii=False, default=str) + "\n")

    def _detect_arrow(self, run_id: str | None) -> dict[str, Any]:
        note = "Initial evidence is JSONL-first; Arrow replay is optional."
        if not run_id:
            return {
                "scoring_arrow_detected": False,
                "replay_arrow_detected": False,
                "scoring_arrow_path": None,
                "replay_arrow_path": None,
                "note": note,
            }
        run_dir = self.run_root / _safe(run_id)
        scoring_arrow = run_dir / "scoring.arrow"
        replay_arrow = run_dir / "replay.arrow"
        return {
            "scoring_arrow_detected": scoring_arrow.exists(),
            "replay_arrow_detected": replay_arrow.exists(),
            "scoring_arrow_path": str(scoring_arrow) if scoring_arrow.exists() else None,
            "replay_arrow_path": str(replay_arrow) if replay_arrow.exists() else None,
            "note": note,
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
            trace_dest = session.session_dir / f"{safe_scenario}.trace_current.jsonl"
            if trace_path.resolve() != trace_dest.resolve():
                shutil.copyfile(trace_path, trace_dest)
            validity = validate_trace_jsonl(trace_dest)
            scenario_entry["valid_data"] = validity["valid_data"]
            scenario_entry["trace_path"] = trace_dest.name
            scenario_entry["validity"] = validity
        else:
            scenario_entry["error"] = f"trace file not found: {trace_path}"
            self.record_postprocess(session, {
                "level": "error",
                "scenario_id": safe_scenario,
                "event": "trace_missing",
                "path": str(trace_path),
            })

        if report_path and Path(report_path).exists():
            report_dest = session.session_dir / f"{safe_scenario}.json"
            if Path(report_path).resolve() != report_dest.resolve():
                shutil.copyfile(report_path, report_dest)
            scenario_entry["report_path"] = report_dest.name

        scenario_entry["png_path"] = f"{safe_scenario}_trajectory_dashboard.png"
        scenarios = [
            s for s in manifest.get("scenarios", [])
            if s.get("scenario_id") != safe_scenario
        ]
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
