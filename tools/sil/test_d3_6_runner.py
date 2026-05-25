"""D3.6 runner tests -- unit + integration.

Run from repo root: python3 -m pytest tools/sil/test_d3_6_runner.py -v
"""
from __future__ import annotations
import csv as _csv
import importlib.util
import json
import math as _math
import subprocess
import sys
from pathlib import Path

import pytest

RUNNER = Path("tools/sil/d3_6_runner.py")
REPO_ROOT = Path(__file__).parents[2]


def run(args: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, str(RUNNER)] + args,
        capture_output=True, text=True, cwd=str(REPO_ROOT),
    )


def _load_runner():
    spec = importlib.util.spec_from_file_location("d3_6_runner", RUNNER)
    mod = importlib.util.module_from_spec(spec)
    sys.modules["d3_6_runner"] = mod
    spec.loader.exec_module(mod)
    return mod


class TestCLISkeleton:
    def test_help_exits_zero(self):
        r = run(["--help"])
        assert r.returncode == 0
        assert "cube" in r.stdout

    def test_all_subcommands_have_help(self):
        for sub in ["cube", "sotif", "iv", "mc", "gif", "report", "run-all"]:
            r = run([sub, "--help"])
            assert r.returncode == 0, f"{sub} --help failed:\n{r.stderr}"

    def test_unknown_subcommand_exits_nonzero(self):
        r = run(["bogus"])
        assert r.returncode != 0


class TestManifest:
    def test_manifest_written_correctly(self, tmp_path):
        mod = _load_runner()
        csv_path = tmp_path / "cube_results.csv"
        csv_path.write_text("col1\nval1\nval2\n")
        mod._write_manifest("cube", {"workers": 4}, csv_path, tmp_path)
        manifest = json.loads((tmp_path / "run_manifest_cube.json").read_text())
        assert manifest["stage"] == "cube"
        assert manifest["row_count"] == 2
        assert len(manifest["sha256"]) == 64
        assert "git_commit" in manifest
        assert "timestamp" in manifest


class TestCubeSubcommand:
    def test_cube_dry_run_produces_csv_with_schema(self, tmp_path):
        evidence = tmp_path / "evidence"
        r = run([
            "--evidence-dir", str(evidence),
            "cube", "--workers", "1", "--n-cube-sample", "5",
            "--na-decl", "tools/sil/cube_na_declarations.yaml",
        ])
        assert r.returncode in (0, 1), f"cube crashed:\n{r.stderr}"
        csv_path = evidence / "cube_results.csv"
        assert csv_path.exists()
        rows = list(_csv.DictReader(csv_path.open()))
        assert len(rows) == 5
        required_cols = {
            "scenario_id", "rule", "odd", "disturbance", "seed", "na_cell",
            "verdict", "cpa_min_nm", "safety_score", "rule_score",
            "delay_pen", "mag_pen", "phase_score", "total_score",
        }
        assert required_cols.issubset(set(rows[0].keys()))

    def test_cube_na_cells_recorded_in_manifest(self, tmp_path):
        evidence = tmp_path / "evidence"
        run([
            "--evidence-dir", str(evidence),
            "cube", "--workers", "1", "--n-cube-sample", "30",
            "--na-decl", "tools/sil/cube_na_declarations.yaml",
        ])
        manifest = json.loads((evidence / "run_manifest_cube.json").read_text())
        assert "na_cells" in manifest["params"]
        assert "coverage" in manifest["params"]

    def test_build_cube_scenario_returns_valid_dict(self):
        mod = _load_runner()
        scen = mod._build_cube_scenario("Rule14", "open_sea", "bf_2_3", 1)
        assert scen["metadata"]["schema_version"] == "3.0"
        assert scen["metadata"]["encounter"]["rule"] == "Rule14"
        assert scen["metadata"]["odd_cell"]["domain"] == "open_sea"
        assert len(scen["targetShips"]) == 1


class TestSotifSubcommand:
    def test_sotif_triggers_yaml_has_50_in_3_categories(self):
        import yaml
        data = yaml.safe_load(Path("scenarios/sotif/sotif_triggers.yaml").read_text())
        assert len(data["triggers"]) == 50
        cats = [t["category"] for t in data["triggers"]]
        assert cats.count("A") == 15
        assert cats.count("B") == 20
        assert cats.count("C") == 15

    def test_sotif_dry_run_produces_csv_with_trigger_id(self, tmp_path):
        evidence = tmp_path / "evidence"
        r = run([
            "--evidence-dir", str(evidence),
            "sotif", "--seeds", "1",
            "--triggers-yaml", "scenarios/sotif/sotif_triggers.yaml",
            "--n-sotif-sample", "3",
        ])
        assert r.returncode in (0, 1), f"sotif crashed:\n{r.stderr}"
        csv_path = evidence / "sotif_results.csv"
        assert csv_path.exists()
        rows = list(_csv.DictReader(csv_path.open()))
        assert len(rows) == 3
        assert "trigger_id" in rows[0]
        assert "assumption_class" in rows[0]


class TestNomotoVessel:
    def test_heading_increases_with_starboard_rudder(self):
        mod = _load_runner()
        state = mod.ShipState(lat=63.44, lon=10.38, psi=0.0, u=5.144, r=0.0)
        vessel = mod.NomotoVessel(K=0.15, tau=30.0, t_surge=60.0, init_state=state)
        psi0 = vessel.state.psi
        for _ in range(100):
            vessel.step(dt=1.0, delta_cmd=0.3, u_cmd=5.144)
        assert vessel.state.psi > psi0, "Starboard rudder must increase psi"

    def test_surge_converges_to_commanded_speed(self):
        mod = _load_runner()
        state = mod.ShipState(lat=63.44, lon=10.38, psi=0.0, u=0.0, r=0.0)
        vessel = mod.NomotoVessel(init_state=state)
        u_cmd = 5.144
        for _ in range(600):
            vessel.step(dt=1.0, delta_cmd=0.0, u_cmd=u_cmd)
        assert abs(vessel.state.u - u_cmd) < 0.1, "Surge must converge to u_cmd"

    def test_position_updates_northward_with_zero_rudder(self):
        mod = _load_runner()
        state = mod.ShipState(lat=63.44, lon=10.38, psi=0.0, u=5.144, r=0.0)
        vessel = mod.NomotoVessel(init_state=state)
        vessel.step(dt=10.0, delta_cmd=0.0, u_cmd=5.144)
        assert vessel.state.lat > 63.44, "North heading must increase latitude"


class TestVelocityObstacle:
    def test_returns_valid_heading_float(self):
        mod = _load_runner()
        own = mod.ShipState(lat=63.44, lon=10.38, psi=0.0, u=5.144, r=0.0)
        tgt = mod.ShipState(lat=63.44 + 0.5 / 60, lon=10.38 + 1.0 / 60,
                             psi=_math.pi + _math.pi / 4, u=5.144, r=0.0)
        vo = mod.VelocityObstacle()
        heading_cmd, speed_cmd = vo.get_avoidance_velocity(own, tgt)
        assert 0.0 <= heading_cmd <= 2 * _math.pi
        assert speed_cmd > 0.0

    def test_no_action_when_target_not_threatening(self):
        mod = _load_runner()
        own = mod.ShipState(lat=63.44, lon=10.38, psi=0.0, u=5.144, r=0.0)
        tgt = mod.ShipState(lat=63.44 - 5.0 / 60, lon=10.38, psi=0.0, u=5.144, r=0.0)
        vo = mod.VelocityObstacle()
        heading_cmd, _ = vo.get_avoidance_velocity(own, tgt)
        assert abs(heading_cmd - own.psi) < 0.01, "No threat: heading must stay unchanged"


class TestIvSubcommand:
    def test_iv_generates_at_least_50_yaml_files(self, tmp_path):
        iv_dir = tmp_path / "scenarios" / "iv"
        evidence = tmp_path / "evidence"
        r = run(["--evidence-dir", str(evidence), "iv",
                 "--scenarios-dir", str(iv_dir), "--n-min", "50"])
        assert r.returncode in (0, 1), f"iv crashed:\n{r.stderr}"
        yamls = list(iv_dir.glob("iv_*.yaml"))
        assert len(yamls) >= 50

    def test_iv_results_csv_pass_rate_above_85pct(self, tmp_path):
        iv_dir = tmp_path / "scenarios" / "iv"
        evidence = tmp_path / "evidence"
        run(["--evidence-dir", str(evidence), "iv",
             "--scenarios-dir", str(iv_dir), "--n-min", "50"])
        rows = list(_csv.DictReader((evidence / "iv_results.csv").open()))
        pass_count = sum(1 for r in rows if r["verdict"] == "PASS")
        assert pass_count / len(rows) >= 0.85, \
            f"IV PASS rate {pass_count}/{len(rows)} below 85%"


class TestMcSubcommand:
    def test_mc_produces_csv_and_sensitivity_json(self, tmp_path):
        evidence = tmp_path / "evidence"
        r = run(["--evidence-dir", str(evidence),
                 "mc", "--n", "200", "--sobol-n", "64", "--seed", "42"])
        assert r.returncode in (0, 1), f"mc crashed:\n{r.stderr}"
        assert (evidence / "mc_results.csv").exists()
        sens = json.loads((evidence / "mc_sensitivity.json").read_text())
        assert "pass_rate_ci" in sens
        assert "sobol_S1" in sens
        assert "weight_sensitivity" in sens

    def test_mc_ci_structure_valid(self, tmp_path):
        evidence = tmp_path / "evidence"
        run(["--evidence-dir", str(evidence),
             "mc", "--n", "500", "--sobol-n", "64", "--seed", "0"])
        sens = json.loads((evidence / "mc_sensitivity.json").read_text())
        ci = sens["pass_rate_ci"]
        assert "lower" in ci and "upper" in ci
        assert 0.0 <= ci["lower"] <= ci["upper"] <= 1.0

    def test_mc_csv_has_correct_schema(self, tmp_path):
        evidence = tmp_path / "evidence"
        run(["--evidence-dir", str(evidence),
             "mc", "--n", "100", "--sobol-n", "64", "--seed", "1"])
        rows = list(_csv.DictReader((evidence / "mc_results.csv").open()))
        assert len(rows) == 100
        assert "cpa_min_nm" in rows[0]
        assert "verdict" in rows[0]


class TestGifSubcommand:
    def _seed_failures_csv(self, evidence: Path) -> Path:
        evidence.mkdir(parents=True, exist_ok=True)
        failures_csv = evidence / "report.failures.csv"
        with failures_csv.open("w", newline="") as f:
            w = _csv.DictWriter(f, fieldnames=[
                "scenario_id","trigger_type","verdict","cpa_min_nm","rule_violated",
                "fail_frame_idx","asdr_hash","gif_path","root_cause_category",
                "root_cause_detail","mitigation",
            ])
            w.writeheader()
            w.writerow({
                "scenario_id": "Rule14_open_sea_bf_2_3_s1",
                "trigger_type": "cube", "verdict": "FAIL", "cpa_min_nm": "0.15",
                "rule_violated": "Rule14", "fail_frame_idx": "300",
                "asdr_hash": "abc123deadbeef01", "gif_path": "",
                "root_cause_category": "GEOMETRIC", "root_cause_detail": "CPA<0.27", "mitigation": "",
            })
        return failures_csv

    def test_gif_generates_one_gif_per_fail(self, tmp_path):
        evidence = tmp_path / "evidence"
        failures_csv = self._seed_failures_csv(evidence)
        r = run(["--evidence-dir", str(evidence), "gif",
                 "--failures-csv", str(failures_csv)])
        assert r.returncode == 0, r.stderr
        gifs = list((evidence / "gifs").glob("*.gif"))
        assert len(gifs) >= 1, "Must generate at least one GIF for the FAIL scenario"

    def test_gif_updates_gif_path_in_csv(self, tmp_path):
        evidence = tmp_path / "evidence"
        failures_csv = self._seed_failures_csv(evidence)
        run(["--evidence-dir", str(evidence), "gif", "--failures-csv", str(failures_csv)])
        rows = list(_csv.DictReader(failures_csv.open()))
        fail_rows = [r for r in rows if r["verdict"] == "FAIL"]
        assert all(r["gif_path"] for r in fail_rows), "gif_path must be filled for all FAIL rows"


class TestRlFuzzerStub:
    def _load_stub(self):
        spec = importlib.util.spec_from_file_location(
            "rl_fuzzer_stub", Path("tools/sil/rl_fuzzer_stub.py"))
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        return mod

    def test_generate_returns_maritime_schema_dict(self):
        mod = self._load_stub()
        stub = mod.RLFuzzerStub()
        result = stub.generate(rule="Rule14", odd="open_sea", seed=42)
        assert isinstance(result, dict)
        assert result["metadata"]["schema_version"] == "3.0"
        assert "ownShip" in result
        assert "targetShips" in result
        assert len(result["targetShips"]) >= 1

    def test_generate_is_deterministic_same_seed(self):
        mod = self._load_stub()
        r1 = mod.RLFuzzerStub().generate("Rule15", "open_sea", seed=7)
        r2 = mod.RLFuzzerStub().generate("Rule15", "open_sea", seed=7)
        assert r1["targetShips"][0]["initial"]["cog"] == r2["targetShips"][0]["initial"]["cog"]

    def test_load_failure_cases_does_not_raise(self, tmp_path):
        mod = self._load_stub()
        stub = mod.RLFuzzerStub()
        csv_path = tmp_path / "failures.csv"
        csv_path.write_text("scenario_id,verdict\nscenario1,FAIL\n")
        stub.load_failure_cases(str(csv_path))


class TestReportSubcommand:
    def _seed_evidence(self, evidence: Path) -> None:
        evidence.mkdir(parents=True, exist_ok=True)
        cube_header = ",".join([
            "scenario_id","rule","odd","disturbance","seed","na_cell",
            "verdict","cpa_min_nm","asdr_hash",
            "safety_score","rule_score","delay_pen","mag_pen","phase_score","total_score",
            "iv_mode","fail_gif_path"])
        for fname in ["cube_results.csv","iv_results.csv","mc_results.csv"]:
            (evidence / fname).write_text(cube_header + "\n")
        sotif_header = cube_header + ",trigger_id,assumption_class"
        (evidence / "sotif_results.csv").write_text(sotif_header + "\n")
        (evidence / "mc_sensitivity.json").write_text(
            '{"pass_rate":0.95,"n_filtered":800,"n_pass":760,'
            '"pass_rate_ci":{"lower":0.93,"upper":0.97},'
            '"sobol_S1":{"target_bearing_initial":0.42},"weight_sensitivity":{}}')
        (evidence / "report.failures.csv").write_text(
            "scenario_id,trigger_type,verdict,cpa_min_nm,rule_violated,"
            "fail_frame_idx,asdr_hash,gif_path,root_cause_category,root_cause_detail,mitigation\n")
        for stage in ["cube","sotif","iv","mc","gif","report"]:
            (evidence / f"run_manifest_{stage}.json").write_text(
                f'{{"stage":"{stage}","git_commit":"abc1234","timestamp":"2026-08-15T10:00:00Z",'
                f'"params":{{}},"output":"","row_count":0,"sha256":""}}')

    def test_report_html_contains_all_10_sections(self, tmp_path):
        evidence = tmp_path / "evidence"
        self._seed_evidence(evidence)
        out_html = tmp_path / "D3.6-coverage-report.html"
        r = run(["--evidence-dir", str(evidence), "report",
                 "--all-evidence", str(evidence), "--output-html", str(out_html)])
        assert r.returncode == 0, r.stderr
        assert out_html.exists()
        content = out_html.read_text()
        for section in [f"S{i}" for i in range(1, 11)]:
            assert section in content, f"Section {section} missing from report HTML"

    def test_report_html_size_geq_50kb(self, tmp_path):
        evidence = tmp_path / "evidence"
        self._seed_evidence(evidence)
        out_html = tmp_path / "D3.6-coverage-report.html"
        run(["--evidence-dir", str(evidence), "report",
             "--all-evidence", str(evidence), "--output-html", str(out_html)])
        assert out_html.stat().st_size >= 50_000, "Report HTML must be >= 50 KB (DoD 7)"


class TestRunAll:
    def test_run_all_dry_run_produces_all_artifacts(self, tmp_path):
        """run-all --n-cube-sample 10 --mc-n 100 --iv-n 10 must complete without crash."""
        evidence = tmp_path / "evidence"
        iv_dir   = tmp_path / "scenarios" / "iv"
        r = run([
            "--evidence-dir", str(evidence),
            "run-all",
            "--workers", "1",
            "--n-cube-sample", "10",
            "--mc-n", "100",
            "--iv-n", "10",
            "--iv-scenarios-dir", str(iv_dir),
        ])
        assert r.returncode in (0, 1), f"run-all crashed:\n{r.stderr[-500:]}"
        for fname in ["cube_results.csv", "sotif_results.csv",
                      "iv_results.csv", "mc_results.csv", "mc_sensitivity.json"]:
            assert (evidence / fname).exists(), f"Missing {fname}"
        for stage in ["cube", "sotif", "iv", "mc", "gif", "report"]:
            mf = evidence / f"run_manifest_{stage}.json"
            assert mf.exists(), f"Missing run_manifest_{stage}.json"

    def test_run_all_dry_run_csv_has_correct_cols(self, tmp_path):
        evidence = tmp_path / "evidence"
        iv_dir   = tmp_path / "scenarios" / "iv"
        run(["--evidence-dir", str(evidence), "run-all",
             "--workers", "1", "--n-cube-sample", "5",
             "--mc-n", "50", "--iv-n", "5",
             "--iv-scenarios-dir", str(iv_dir)])
        rows = list(_csv.DictReader((evidence / "cube_results.csv").open()))
        if rows:
            for col in ["safety_score", "rule_score", "delay_pen",
                        "mag_pen", "phase_score", "total_score"]:
                assert col in rows[0], f"Missing scoring column: {col}"
