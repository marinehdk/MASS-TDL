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
        rows.append(
            (
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
            )
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
            target_id = str(row.get("target_id") or row.get("primary_target_id") or "UNKNOWN")
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
    modules = ["M2", "M6", "M4", "M5", "L4", "M7"]
    chain = {module: {"status": "UNKNOWN", "status_source": "diagnostic_availability", "facts": {}} for module in modules}
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
