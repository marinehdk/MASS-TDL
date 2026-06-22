from __future__ import annotations

import json
from datetime import datetime
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
        created_at=datetime(2026, 6, 22, 15, 30, 12),
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
        created_at=datetime(2026, 6, 22, 15, 30, 12),
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
        created_at=datetime(2026, 6, 22, 15, 30, 12),
    )

    mgr.archive_scenario(session, "colreg-rule14-ho", trace_path=trace, status="stopped")
    final_manifest = mgr.finalize(session, status="stopped")

    assert final_manifest is None
    assert not session.session_dir.exists()

