"""Tests for Marzip builder: MCAP->Arrow post-processing + 7-piece assembly.

Ref: docs/superpowers/plans/2026-05-15-gap-015-sil-fixes.md Task 4.2
"""
import json
import tempfile
from pathlib import Path
import pytest


class TestMarzipBuilder:
    def test_mcap_to_polars_empty_bag(self, tmp_path: Path):
        """Empty MCAP bag produces empty DataFrame with correct columns."""
        from sil_orchestrator.marzip_builder import mcap_to_polars
        from sil_orchestrator.marzip_schemas import SCORING_COLUMNS

        mcap_path = tmp_path / "empty.mcap"
        mcap_path.write_bytes(b"")

        df = mcap_to_polars(str(mcap_path))
        assert set(SCORING_COLUMNS).issubset(set(df.columns))
        assert len(df) == 0

    def test_derive_kpis_from_polars(self):
        """Derived KPIs computed correctly from scoring DataFrame."""
        pytest.importorskip("polars")
        import polars as pl
        from sil_orchestrator.marzip_builder import derive_kpis

        df = pl.DataFrame({
            "stamp_ns": [0, 1_000_000_000],
            "safety": [0.95, 0.90],
            "rule_compliance": [0.88, 0.85],
            "delay": [0.70, 0.65],
            "magnitude": [0.95, 0.92],
            "phase": [0.82, 0.80],
            "plausibility": [0.99, 0.98],
            "total_score": [0.88, 0.85],
            "cpa_m": [800.0, 750.0],
            "tcpa_s": [120.0, 115.0],
            "own_lat": [35.0, 35.001],
            "own_lon": [139.0, 139.001],
            "own_sog": [12.0, 11.5],
            "own_cog": [1.5, 1.52],
            "own_rot": [0.01, 0.02],
            "own_rudder_angle": [0.05, 0.08],
            "own_throttle": [0.8, 0.75],
            "target_mmsi": [123456789, 123456789],
            "target_lat": [35.01, 35.012],
            "target_lon": [139.01, 139.012],
            "target_sog": [10.0, 9.8],
            "target_cog": [4.5, 4.52],
            "target_range_m": [1500.0, 1480.0],
            "target_bearing_rad": [0.5, 0.52],
            "encounter_type": ["HO", "HO"],
            "active_rule": ["Rule 14", "Rule 14"],
            "grounding_risk": [0.01, 0.02],
            "route_deviation_m": [2.0, 3.5],
        })

        kpis = derive_kpis(df)
        assert "min_cpa_nm" in kpis
        assert kpis["min_cpa_nm"] == pytest.approx(750.0 * 0.000539957, rel=1e-6)
        assert kpis["duration_s"] == pytest.approx(1.0)
        assert kpis["max_rudder_deg"] == pytest.approx(0.08 * 57.2958, rel=1e-3)

    def test_assemble_marzip_7_pieces(self, tmp_path: Path):
        """Full marzip assembly produces all 7 artifacts."""
        from sil_orchestrator.marzip_builder import assemble_marzip

        run_dir = tmp_path / "runs" / "test-run"
        run_dir.mkdir(parents=True)
        (run_dir / "scenario.yaml").write_text("simulation:\n  duration_s: 300\n")
        (run_dir / "scenario.sha256").write_text(
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
        )

        export_dir = tmp_path / "exports"
        export_dir.mkdir()

        result_path = assemble_marzip("test-run", run_dir_parent=tmp_path / "runs",
                                       export_dir=export_dir)
        assert result_path.endswith(".marzip")

        import zipfile
        with zipfile.ZipFile(result_path, "r") as zf:
            names = set(zf.namelist())
            expected = {
                "scenario.yaml", "scenario.sha256", "manifest.yaml",
                "scoring.arrow", "results.bag.mcap", "results.bag.mcap.sha256",
                "asdr_events.jsonl", "verdict.json",
            }
            assert expected.issubset(names), f"Missing: {expected - names}"

    def test_export_endpoint_returns_processing(self):
        """POST /api/v1/export/marzip returns {status: processing}."""
        from sil_orchestrator.main import app
        from sil_orchestrator import config
        from fastapi.testclient import TestClient

        client = TestClient(app)
        with tempfile.TemporaryDirectory() as td:
            run_dir = Path(td) / "runs" / "run-test"
            run_dir.mkdir(parents=True)
            (run_dir / "scenario.yaml").write_text("dummy: true")
            (run_dir / "scenario.sha256").write_text("abc123")

            import sil_orchestrator.config as cfg
            original_run, original_export = cfg.RUN_DIR, cfg.EXPORT_DIR
            try:
                cfg.RUN_DIR = Path(td) / "runs"
                cfg.EXPORT_DIR = Path(td) / "exports"
                cfg.EXPORT_DIR.mkdir(parents=True, exist_ok=True)

                resp = client.post("/api/v1/export/marzip", json={"run_id": "run-test"})
                assert resp.status_code == 200
                assert resp.json()["status"] == "processing"
            finally:
                cfg.RUN_DIR = original_run
                cfg.EXPORT_DIR = original_export

    def test_export_nonexistent_run_404(self):
        """POST /api/v1/export/marzip with bad run_id returns 404."""
        from sil_orchestrator.main import app
        from fastapi.testclient import TestClient

        client = TestClient(app)
        resp = client.post("/api/v1/export/marzip", json={"run_id": "no-such-run"})
        assert resp.status_code == 404
