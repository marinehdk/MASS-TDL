import asyncio
import json
import sys
import tempfile
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO_ROOT / "src"))


def _import_scoring():
    import importlib
    import sil_orchestrator.scoring_routes as sr
    importlib.reload(sr)
    return sr


def test_scoring_arrow_read_via_open_file(tmp_path, monkeypatch):
    """When scoring.arrow exists in file/footer format, endpoint reads it correctly.

    This tests the fix for InvalidFooter when reading footer-format Arrow files.
    Footer format is produced by ArrowWriter (new_file), stream format by IPC streaming.
    """
    import pyarrow as pa
    import pyarrow.ipc as ipc

    run_dir = tmp_path / "run-test-arrow"
    run_dir.mkdir()

    n = 100
    schema = pa.schema([
        ("stamp", pa.float64()), ("safety", pa.float64()),
        ("rule_compliance", pa.float64()), ("delay_penalty", pa.float64()),
        ("action_magnitude_penalty", pa.float64()), ("phase_score", pa.float64()),
        ("plausibility", pa.float64()), ("total", pa.float64()),
        ("cpa_nm", pa.float64()), ("cpa_target_nm", pa.float64()),
        ("pass_fail", pa.bool_()), ("applicable_rule", pa.string()),
    ])
    batch = pa.RecordBatch.from_pylist(
        [{
            "stamp": float(i), "safety": 0.85, "rule_compliance": 0.92,
            "delay_penalty": 0.05, "action_magnitude_penalty": 0.10,
            "phase_score": 0.88, "plausibility": 0.95, "total": 0.78,
            "cpa_nm": 3.5, "cpa_target_nm": 0.27,
            "pass_fail": True, "applicable_rule": "Rule 14",
        } for i in range(n)],
        schema=schema,
    )
    arrow_path = run_dir / "scoring.arrow"
    with pa.OSFile(str(arrow_path), "wb") as sink:
        with ipc.new_file(sink, schema) as writer:  # FILE/footer format (ArrowWriter style)
            writer.write_batch(batch)

    sr = _import_scoring()
    monkeypatch.setattr(sr, "RUN_DIR", tmp_path)

    # Mock the config to use tmp_path
    from sil_orchestrator import config
    monkeypatch.setattr(config, "RUN_DIR", tmp_path)

    result = asyncio.run(sr.scoring_last_run())

    assert result.get("kpis") is not None, f"kpis missing: {result}"
    assert result.get("scoring_dimensions") is not None, f"scoring_dimensions missing: {result}"
    assert result.get("verdict") in {"pass", "fail"}, f"unexpected verdict: {result.get('verdict')}"
    # Verify actual KPI data
    assert result["kpis"]["decision_count"] == n, f"expected {n} rows, got {result['kpis']['decision_count']}"


def test_scoring_json_fallback(tmp_path, monkeypatch):
    """When scoring.arrow missing/corrupted but scoring.json exists, fallback works."""
    run_dir = tmp_path / "run-fallback"
    run_dir.mkdir()
    payload = {
        "kpis": {"min_cpa_nm": 3.48, "tcpa_min_s": 180.0},
        "scoring_dimensions": None,
        "rule_chain": [],
        "verdict": None,
    }
    (run_dir / "scoring.json").write_text(json.dumps(payload))

    sr = _import_scoring()
    monkeypatch.setattr(sr, "RUN_DIR", tmp_path)

    from sil_orchestrator import config
    monkeypatch.setattr(config, "RUN_DIR", tmp_path)

    result = asyncio.run(sr.scoring_last_run())
    assert result.get("kpis") is not None
    assert result["kpis"]["min_cpa_nm"] == 3.48


def test_scoring_arrow_polars_read_ipc_handles_footer_format(tmp_path):
    """Verify that polars.read_ipc() correctly reads footer-format Arrow files."""
    import pyarrow as pa
    import pyarrow.ipc as ipc
    import polars as pl

    arrow_path = tmp_path / "test.arrow"
    schema = pa.schema([("col", pa.int64())])
    batch = pa.RecordBatch.from_pydict({"col": [1, 2, 3]})

    # Write in footer format (like ArrowWriter does)
    with pa.OSFile(str(arrow_path), "wb") as sink:
        with ipc.new_file(sink, schema) as writer:
            writer.write_batch(batch)

    # Read with polars (should work)
    df = pl.read_ipc(str(arrow_path))
    assert len(df) == 3
    assert df["col"].to_list() == [1, 2, 3]
