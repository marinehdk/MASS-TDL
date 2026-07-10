from __future__ import annotations

import gzip
import json
import math
import sqlite3
from collections import defaultdict
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


def _safe_session_child(session_path: Path, value: Any, default_name: str) -> Path:
    relative = Path(str(value or default_name))
    candidate = session_path / relative
    resolved_session = session_path.resolve()
    resolved_candidate = candidate.resolve(strict=False)
    try:
        resolved_candidate.relative_to(resolved_session)
    except ValueError as exc:
        raise ValueError(f"Evidence artifact path escapes session: {relative}") from exc
    return resolved_candidate


def _first_present(row: dict[str, Any], *keys: str) -> Any:
    for key in keys:
        if key in row:
            return row[key]
    return "UNKNOWN"


def _first_rule(row: dict[str, Any]) -> Any:
    if "rule" in row:
        return row["rule"]
    rules = row.get("active_rules")
    if isinstance(rules, list) and rules:
        first = rules[0]
        if isinstance(first, dict) and first.get("rule_id") is not None:
            return f"Rule{first['rule_id']}"
    return "UNKNOWN"


def _role_label(value: Any) -> Any:
    if value == "UNKNOWN":
        return value
    if isinstance(value, str):
        stripped = value.strip()
        if stripped:
            return stripped
    try:
        role = int(value)
    except (TypeError, ValueError):
        return value
    return {
        0: "stand_on",
        1: "give_way",
        2: "both_give_way",
        3: "free",
    }.get(role, role)


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


def _compliance_status(value: Any) -> str:
    if isinstance(value, str):
        normalized = value.strip().upper()
        if normalized in {"PASS", "FAIL"}:
            return normalized
    return "UNKNOWN"


def _as_float(value: Any) -> float | None:
    try:
        result = float(value)
    except (TypeError, ValueError):
        return None
    if not math.isfinite(result):
        return None
    return result


def _project_relative_position(lat: Any, lon: Any, bearing_deg: Any, range_m: Any) -> tuple[float | None, float | None]:
    origin_lat = _as_float(lat)
    origin_lon = _as_float(lon)
    bearing = _as_float(bearing_deg)
    distance_m = _as_float(range_m)
    if origin_lat is None or origin_lon is None or bearing is None or distance_m is None:
        return None, None
    if distance_m < 0:
        return None, None
    radius_m = 6_371_000.0
    angular_distance = distance_m / radius_m
    bearing_rad = math.radians(bearing)
    lat1 = math.radians(origin_lat)
    lon1 = math.radians(origin_lon)
    lat2 = math.asin(
        math.sin(lat1) * math.cos(angular_distance)
        + math.cos(lat1) * math.sin(angular_distance) * math.cos(bearing_rad)
    )
    lon2 = lon1 + math.atan2(
        math.sin(bearing_rad) * math.sin(angular_distance) * math.cos(lat1),
        math.cos(angular_distance) - math.sin(lat1) * math.sin(lat2),
    )
    normalized_lon = (math.degrees(lon2) + 540.0) % 360.0 - 180.0
    return math.degrees(lat2), normalized_lon


def _scenario_from_batch(batch: dict[str, Any], scenario_id: str) -> dict[str, Any]:
    direct = batch.get(scenario_id)
    if isinstance(direct, dict):
        return direct
    rows = batch.get("results") or batch.get("scenarios") or []
    for row in rows:
        if row.get("scenario") == scenario_id or row.get("scenario_id") == scenario_id:
            return row
    return {}


def _run_meta_path(session_path: Path) -> Path | None:
    candidates = [session_path / "run_meta.json"]
    if session_path.name == "trace":
        candidates.append(session_path.parent / "run_meta.json")
    for path in candidates:
        if path.exists():
            return path
    return None


def _read_run_meta(session_path: Path) -> dict[str, Any]:
    path = _run_meta_path(session_path)
    return _read_json(path) if path is not None else {}


def _latest_mtime(session_path: Path, run_meta_path: Path | None) -> float:
    file_mtimes = [p.stat().st_mtime for p in session_path.rglob("*") if p.is_file()]
    if run_meta_path is not None:
        file_mtimes.append(run_meta_path.stat().st_mtime)
    return max(file_mtimes, default=(session_path / "manifest.json").stat().st_mtime)


def _gate_rows(
    evidence_id: str,
    session_id: str,
    scenario_id: str,
    report: dict[str, Any],
    batch_row: dict[str, Any],
    artifact_consistency: dict[str, Any],
) -> tuple[list[tuple[Any, ...]], list[tuple[Any, ...]]]:
    rows: list[dict[str, Any]] = []

    def add(gate_id: str, status: str, temporal_scope: str, rank: int, payload: dict[str, Any], source: str) -> None:
        rows.append(
            {
                "evidence_id": evidence_id,
                "session_id": session_id,
                "scenario_id": scenario_id,
                "gate_id": gate_id,
                "status": status,
                "temporal_scope": temporal_scope,
                "precedence_rank": rank,
                "conflict_group": None,
                "payload_json": json.dumps(payload, sort_keys=True),
                "source": source,
            }
        )

    layer_map = {
        "L1_scenario_validity": "G-SCN",
        "L2_safety_floor": "G-SEP",
        "L3_dynamic_risk": "G-SEP",
        "L4_colregs_compliance": "G-SEM",
        "L5_route_recovery": "G-REL",
        "L6_seamanship": "G-REL",
        "L7_stability": "G-ACT",
    }
    report_layers = report.get("layers") or {}
    for layer_id, gate_id in layer_map.items():
        payload = report_layers.get(layer_id)
        if payload is None:
            add(
                gate_id,
                "UNKNOWN",
                "final_run_verdict",
                20,
                {"layer_id": layer_id, "missing": True},
                f"TraceEvaluationReport.layers.{layer_id}",
            )
            continue
        add(
            gate_id,
            _source_status(payload),
            "final_run_verdict",
            20,
            payload,
            f"TraceEvaluationReport.layers.{layer_id}",
        )

    batch_row = batch_row or {}
    batch_gate_specs = (
        (
            "G-SEP",
            "batch_summary.cpa_ok",
            batch_row.get("cpa_ok"),
            _bool_status,
        ),
        (
            "G-SEP",
            "batch_summary.domain_gates.risk_gate_ok",
            (batch_row.get("domain_gates") or {}).get("risk_gate_ok"),
            _bool_status,
        ),
        (
            "G-SEM",
            "batch_summary.phase_semantics.phase_semantics_ok",
            (batch_row.get("phase_semantics") or {}).get("phase_semantics_ok"),
            _bool_status,
        ),
        (
            "G-SEM",
            "batch_summary.compliance_verdict",
            batch_row.get("compliance_verdict"),
            _compliance_status,
        ),
        (
            "G-ACT",
            "batch_summary.stability_pass",
            batch_row.get("stability_pass"),
            _bool_status,
        ),
        (
            "G-REL",
            "batch_summary.returned_to_route",
            batch_row.get("returned_to_route"),
            _bool_status,
        ),
        (
            "G-REL",
            "batch_summary.route_corridor_ok",
            batch_row.get("route_corridor_ok"),
            _bool_status,
        ),
        (
            "G-REL",
            "batch_summary.domain_gates.seamanship_gate_ok",
            (batch_row.get("domain_gates") or {}).get("seamanship_gate_ok"),
            _bool_status,
        ),
    )
    for gate_id, source_field, source_value, status_fn in batch_gate_specs:
        add(
            gate_id,
            status_fn(source_value),
            "final_run_verdict",
            10,
            {"field": source_field, "value": source_value},
            source_field,
        )

    if artifact_consistency:
        add("G-ART", _bool_status(artifact_consistency.get("g_art_ok")), "artifact_consistency", 5, artifact_consistency, "artifact_consistency")
    else:
        add("G-ART", "UNKNOWN", "artifact_consistency", 5, {}, "artifact_consistency")

    conflict_events: list[tuple[Any, ...]] = []
    by_gate: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for row in rows:
        by_gate[row["gate_id"]].append(row)

    for gate_id, gate_rows in by_gate.items():
        known_statuses = {row["status"] for row in gate_rows if row["status"] != "UNKNOWN"}
        if len(known_statuses) > 1:
            conflict_group = f"{scenario_id}:{gate_id}:conflict"
            for row in gate_rows:
                row["conflict_group"] = conflict_group
            conflict_events.append(
                (
                    evidence_id,
                    session_id,
                    scenario_id,
                    0.0,
                    None,
                    "GATE",
                    "gate_conflict",
                    "warn",
                    json.dumps(
                        {
                            "gate_id": gate_id,
                            "conflict_group": conflict_group,
                            "sources": [
                                {
                                    "source": row["source"],
                                    "status": row["status"],
                                }
                                for row in gate_rows
                            ],
                        },
                        sort_keys=True,
                    ),
                    "gate_conflict",
                )
            )

    return [tuple(row[key] for key in ("evidence_id", "session_id", "scenario_id", "gate_id", "status", "temporal_scope", "precedence_rank", "conflict_group", "payload_json", "source")) for row in rows], conflict_events


def _insert_artifact(conn: sqlite3.Connection, evidence_id: str, session_id: str, scenario_id: str | None, kind: str, session_path: Path, path: Path) -> None:
    if not path.exists():
        return
    path = path.resolve()
    session_path = session_path.resolve()
    path.relative_to(session_path)
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
            str(path),
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
    expected_state_fields = {
        "/l3/m2/world_state": (
            "M2",
            {
                "primary_target_id": lambda row: _first_present(row, "primary_target_id", "target_id", "colregs_chain_target_id"),
                "cpa_m": lambda row: _first_present(row, "cpa_m", "primary_cpa_m"),
                "tcpa_s": lambda row: _first_present(row, "tcpa_s", "primary_tcpa_s"),
                "confidence": lambda row: _first_present(row, "confidence"),
            },
        ),
        "/l3/m6/colregs": (
            "M6",
            {
                "rule": _first_rule,
                "role": lambda row: _role_label(_first_present(row, "role", "primary_role")),
                "preferred_direction": lambda row: _first_present(row, "preferred_direction", "primary_preferred_direction"),
                "phase": lambda row: _first_present(row, "phase"),
                "release_predicted": lambda row: _first_present(row, "release_predicted"),
            },
        ),
        "/l3/m6/colregs_constraint": (
            "M6",
            {
                "rule": _first_rule,
                "role": lambda row: _role_label(_first_present(row, "role", "primary_role")),
                "preferred_direction": lambda row: _first_present(row, "preferred_direction", "primary_preferred_direction"),
                "phase": lambda row: _first_present(row, "phase"),
                "release_predicted": lambda row: _first_present(row, "release_predicted"),
            },
        ),
        "/l3/m5/trajectory": (
            "M5",
            {
                "solver_status": lambda row: _first_present(row, "solver_status"),
                "plan_status": lambda row: _first_present(row, "plan_status"),
                "route_hash": lambda row: _first_present(row, "route_hash", "plan_id"),
                "waypoint_count": lambda row: _first_present(row, "waypoint_count", "n_waypoints", "segment_source_count"),
            },
        ),
        "/l3/m5/avoidance_plan": (
            "M5",
            {
                "solver_status": lambda row: _first_present(row, "solver_status"),
                "plan_status": lambda row: _first_present(row, "plan_status"),
                "route_hash": lambda row: _first_present(row, "route_hash", "plan_id"),
                "waypoint_count": lambda row: _first_present(row, "waypoint_count", "n_waypoints", "segment_source_count"),
            },
        ),
        "/l4/guidance": (
            "L4",
            {
                "execution_state": lambda row: _first_present(row, "execution_state"),
                "accepted": lambda row: _first_present(row, "accepted"),
                "rejected": lambda row: _first_present(row, "rejected"),
                "degraded": lambda row: _first_present(row, "degraded"),
                "reason": lambda row: _first_present(row, "reason"),
            },
        ),
    }

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
        if topic == "/l3/m2/world_state" and ("own_lat" in row or "own_lon" in row):
            trajectory.append((evidence_id, session_id, scenario_id, "OWN", "ownship", sim_t, wall_t, row.get("own_lat"), row.get("own_lon"), row.get("own_heading_deg"), row.get("own_sog_kn"), None, topic, seq))
        mapped_snapshot = expected_state_fields.get(topic)
        if mapped_snapshot is not None:
            module, fields = mapped_snapshot
            for field, getter in fields.items():
                set_state(module, field, sim_t, getter(row), topic)
        if topic == "/l3/m2/world_state":
            target_id = str(row.get("target_id") or row.get("primary_target_id") or "UNKNOWN")
            target_lat = row.get("target_lat")
            target_lon = row.get("target_lon")
            if target_lat is None or target_lon is None:
                target_lat, target_lon = _project_relative_position(
                    row.get("own_lat"),
                    row.get("own_lon"),
                    row.get("primary_brg_deg"),
                    row.get("primary_rng_m"),
                )
            trajectory.append((
                evidence_id,
                session_id,
                scenario_id,
                target_id,
                "target",
                sim_t,
                wall_t,
                target_lat,
                target_lon,
                row.get("target_heading_deg", row.get("primary_target_heading_deg")),
                row.get("target_sog_kn", row.get("primary_target_sog_kn")),
                None,
                topic,
                seq,
            ))
        if topic == "/l3/asdr/record":
            events.append(
                (
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
                )
            )
    for key in list(last_state):
        close_state(key, final_t)
    return trajectory, events, states


def ingest_session(conn: sqlite3.Connection, root: EvidenceRootConfig, session_path: Path, raw_trace_policy: str = "keep", force: bool = False) -> IngestResult:
    session_path = session_path.resolve()
    manifest = _read_json(session_path / "manifest.json")
    run_meta_path = _run_meta_path(session_path)
    run_meta = _read_run_meta(session_path)
    session_id = str(run_meta.get("run_id") or manifest.get("session_name") or session_path.name)
    evidence_id = compute_evidence_id(root.root_id, session_path)
    scenarios = manifest.get("scenarios") or []
    latest_mtime = _latest_mtime(session_path, run_meta_path)
    summary_path = session_path / "summary.json"
    batch_path = session_path / "batch_summary.json"
    batch = _read_json(summary_path) if summary_path.exists() else _read_json(batch_path) if batch_path.exists() else {}

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
                str(run_meta.get("source") or manifest.get("source") or root.source),
                str(run_meta.get("mode") or manifest.get("suite") or "unknown"),
                root.root_id,
                None,
                run_meta.get("git_head"),
                str(session_path),
                run_meta.get("created_at") or manifest.get("created_at"),
                manifest.get("ended_at"),
                run_meta.get("status") or manifest.get("status"),
                1 if manifest.get("valid_data", True) else 0,
                int(run_meta.get("scenario_count") or len(scenarios)),
                raw_trace_policy,
                latest_mtime,
            ),
        )
        _insert_artifact(conn, evidence_id, session_id, None, "manifest", session_path, session_path / "manifest.json")
        _insert_artifact(conn, evidence_id, session_id, None, "summary", session_path, summary_path)
        _insert_artifact(conn, evidence_id, session_id, None, "batch_summary", session_path, batch_path)

        trajectory_count = 0
        event_count = 0
        for scenario in scenarios:
            scenario_id = str(scenario["scenario_id"])
            report_path = _safe_session_child(session_path, scenario.get("report_path"), f"{scenario_id}.json")
            trace_path = _safe_session_child(session_path, scenario.get("trace_path"), f"{scenario_id}.trace_current.jsonl")
            if not trace_path.exists() and (trace_path.with_suffix(trace_path.suffix + ".gz")).exists():
                trace_path = trace_path.with_suffix(trace_path.suffix + ".gz")
            art_path = _safe_session_child(session_path, f"{scenario_id}.artifact_consistency.json", f"{scenario_id}.artifact_consistency.json")
            report = _read_json(report_path) if report_path.exists() else {}
            batch_row = _scenario_from_batch(batch, scenario_id)
            art = _read_json(art_path) if art_path.exists() else batch_row.get("artifact_consistency") or {}
            overall = batch_row.get("overall_pass")
            if overall is None:
                overall = (report.get("verdict") or {}).get("overall_pass")
            if overall is None:
                verdict = (run_meta.get("verdicts") or {}).get(scenario_id)
                if isinstance(verdict, str):
                    normalized_verdict = verdict.strip().lower()
                    if normalized_verdict in {"pass", "passed", "ok"}:
                        overall = True
                    elif normalized_verdict in {"fail", "failed", "no"}:
                        overall = False
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
            m5_timeline_path = _safe_session_child(session_path, scenario.get("m5_timeline_path"), f"{scenario_id}/m5_timeline.json")
            _insert_artifact(conn, evidence_id, session_id, scenario_id, "m5_timeline", session_path, m5_timeline_path)
            trajectory_png_path = _safe_session_child(session_path, scenario.get("trajectory_path"), f"{scenario_id}/trajectory.png")
            _insert_artifact(conn, evidence_id, session_id, scenario_id, "trajectory_png", session_path, trajectory_png_path)
            png_path = _safe_session_child(session_path, scenario.get("png_path"), f"{scenario_id}_trajectory_dashboard.png")
            _insert_artifact(conn, evidence_id, session_id, scenario_id, "trajectory_dashboard_png", session_path, png_path)
            if trace_path.exists():
                trajectory, events, states = _trajectory_rows(evidence_id, session_id, scenario_id, trace_path)
                conn.executemany("insert into trajectory_samples values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", trajectory)
                downsample = [row + (0,) for index, row in enumerate(trajectory) if index % 1 == 0]
                conn.executemany("insert into trajectory_downsample values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", downsample)
                conn.executemany("insert into events(evidence_id, session_id, scenario_id, sim_t, wall_t, module, event_type, severity, payload_json, source_topic) values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", events)
                conn.executemany("insert into state_segments values (?, ?, ?, ?, ?, ?, ?, ?, ?)", states)
                trajectory_count += len(trajectory)
                event_count += len(events)
            gates, gate_conflicts = _gate_rows(evidence_id, session_id, scenario_id, report, batch_row, art)
            conn.executemany("insert into gate_results values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", gates)
            for gate in gates:
                conn.execute(
                    "insert into events(evidence_id, session_id, scenario_id, sim_t, wall_t, module, event_type, severity, payload_json, source_topic) values (?, ?, ?, 0, null, 'GATE', 'GATE_RESULT', ?, ?, ?)",
                    (evidence_id, session_id, scenario_id, "crit" if gate[4] == "FAIL" else "info", gate[8], gate[9]),
                )
                event_count += 1
            for conflict_event in gate_conflicts:
                conn.execute(
                    "insert into events(evidence_id, session_id, scenario_id, sim_t, wall_t, module, event_type, severity, payload_json, source_topic) values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                    conflict_event,
                )
                event_count += 1
    return IngestResult(evidence_id=evidence_id, session_id=session_id, scenario_count=len(scenarios), trajectory_count=trajectory_count, event_count=event_count)


def _rows(conn: sqlite3.Connection, sql: str, args: tuple[Any, ...]) -> list[dict[str, Any]]:
    return [dict(row) for row in conn.execute(sql, args).fetchall()]


def _as_optional_bool(value: Any) -> Any:
    if value is None:
        return None
    return bool(value)


def query_replay(conn: sqlite3.Connection, evidence_id: str, scenario_id: str) -> dict[str, Any]:
    session = dict(conn.execute("select * from sessions where evidence_id = ?", (evidence_id,)).fetchone())
    scenario = dict(conn.execute("select * from scenarios where evidence_id = ? and scenario_id = ?", (evidence_id, scenario_id)).fetchone())
    session["valid_data"] = _as_optional_bool(session.get("valid_data"))
    scenario["overall_pass"] = _as_optional_bool(scenario.get("overall_pass"))
    for key in ("returned_to_route", "route_return_required", "route_corridor_ok", "stability_pass"):
        if key in scenario:
            scenario[key] = _as_optional_bool(scenario.get(key))
    trajectory = _rows(conn, "select * from trajectory_downsample where evidence_id = ? and scenario_id = ? order by sim_t, sample_seq", (evidence_id, scenario_id))
    events = _rows(conn, "select * from events where evidence_id = ? and scenario_id = ? order by sim_t, event_id", (evidence_id, scenario_id))
    gates = _rows(conn, "select * from gate_results where evidence_id = ? and scenario_id = ? order by precedence_rank, gate_id", (evidence_id, scenario_id))
    artifacts = _rows(conn, "select artifact_id, kind, relative_path, available from artifacts where evidence_id = ? order by kind", (evidence_id,))
    for artifact in artifacts:
        artifact["available"] = _as_optional_bool(artifact.get("available"))
    duration_s = max([float(row["sim_t"]) for row in trajectory + events] or [0.0])
    return {"session": session, "scenario": scenario, "duration_s": duration_s, "trajectory": trajectory, "events": events, "gates": gates, "artifacts": artifacts}


def query_decision_frame(conn: sqlite3.Connection, evidence_id: str, scenario_id: str, sim_t: float) -> dict[str, Any]:
    expected_fields = {
        "M2": ("primary_target_id", "cpa_m", "tcpa_s", "confidence"),
        "M6": ("rule", "role", "preferred_direction", "phase", "release_predicted"),
        "M4": ("behavior", "avoidance_active"),
        "M5": ("solver_status", "plan_status", "route_hash", "waypoint_count"),
        "L4": ("execution_state", "accepted", "rejected", "degraded", "reason"),
        "M7": ("alert_type", "severity", "recommended_mrm"),
    }
    chain = {
        module: {
            "status": "UNKNOWN",
            "status_source": "diagnostic_availability",
            "facts": {field: "UNKNOWN" for field in fields},
        }
        for module, fields in expected_fields.items()
    }
    rows = _rows(
        conn,
        """
        select *
        from state_segments as s
        where s.evidence_id = ?
          and s.scenario_id = ?
          and s.start_t <= ?
          and (
            ? < s.end_t
            or (
              ? = s.end_t
              and s.end_t = (
                select max(s2.end_t)
                from state_segments as s2
                where s2.evidence_id = s.evidence_id
                  and s2.scenario_id = s.scenario_id
                  and s2.module = s.module
                  and s2.field = s.field
              )
            )
          )
        order by s.module, s.field, s.start_t
        """,
        (evidence_id, scenario_id, sim_t, sim_t, sim_t),
    )
    for row in rows:
        module = row["module"]
        if module in chain:
            facts = chain[module]["facts"]
            if row["field"] not in facts:
                facts[row["field"]] = "UNKNOWN"
            facts[row["field"]] = json.loads(row["value_json"])
            chain[module]["status"] = "OK"
    gates = _rows(conn, "select gate_id, status, temporal_scope, payload_json, source from gate_results where evidence_id = ? and scenario_id = ? order by precedence_rank, gate_id", (evidence_id, scenario_id))
    nearby = _rows(conn, "select * from events where evidence_id = ? and scenario_id = ? and sim_t between ? and ? order by sim_t", (evidence_id, scenario_id, sim_t - 5.0, sim_t + 5.0))
    own = conn.execute(
        "select * from trajectory_samples where evidence_id = ? and scenario_id = ? and vessel_id = 'OWN' order by abs(sim_t - ?) limit 1",
        (evidence_id, scenario_id, sim_t),
    ).fetchone()
    own_ship = dict(own) if own else {}
    return {"evidence_id": evidence_id, "scenario_id": scenario_id, "sim_t": sim_t, "own_ship": own_ship, "targets": [], "chain": chain, "gates": gates, "nearby_events": nearby}
