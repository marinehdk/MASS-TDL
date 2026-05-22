"""Unit tests for check_idl_ts_alignment - run with: pytest tools/vv/test_check_idl_ts_alignment.py"""
import json
import subprocess, sys, textwrap
from pathlib import Path

SAT_TS = Path("web/src/types/sat.ts")

def test_sat_ts_exists():
    assert SAT_TS.exists(), f"Expected {SAT_TS} to exist"

def test_script_exits_zero_on_real_file():
    report = Path("test-results/idl_alignment_report.json")
    result = subprocess.run(
        [sys.executable, "tools/vv/check_idl_ts_alignment.py", "--sat-ts", str(SAT_TS)],
        capture_output=True, text=True
    )
    assert result.returncode == 0, f"STDOUT: {result.stdout}\nSTDERR: {result.stderr}"
    assert "[PASS] All 17 fields aligned." in result.stdout
    data = json.loads(report.read_text())
    assert data["total_fields_checked"] == 17
    assert data["mismatches_count"] == 0

def test_script_detects_mismatch(tmp_path):
    broken = tmp_path / "sat.ts"
    report = tmp_path / "idl_alignment_report.json"
    broken.write_text(textwrap.dedent("""
        export interface IvpContribution {
          direction_deg: number;
          cost: number;
          // label MISSING - mismatch
        }

        export interface ColregsChainLayer {
          layer: 1 | 2 | 3 | 4 | 5;
          label: string;
          conclusion: string;
        }

        export interface TrajectoryCandidate {
          id: number;
          points: Array<{ lon: number; lat: number }>;
          cost: number;
          is_optimal: boolean;
          type: 'mid_mpc' | 'bc_mpc';
        }

        export interface SotifMetrics {
          ais_radar_consistency_sigma: number;
          target_predictability_rms_m: number;
          perception_coverage_pct: number;
          colregs_parse_failures: number;
          comm_link_rtt_ms: number;
          checker_veto_rate_pct: number;
        }
    """))
    result = subprocess.run(
        [
            sys.executable,
            "tools/vv/check_idl_ts_alignment.py",
            "--sat-ts",
            str(broken),
            "--output",
            str(report),
        ],
        capture_output=True, text=True
    )
    assert result.returncode != 0
    assert "mismatch" in result.stdout.lower() or "mismatch" in result.stderr.lower()
    data = json.loads(report.read_text())
    assert data["total_fields_checked"] == 17
    assert data["mismatches"] == [
        {
            "interface": "IvpContribution",
            "field": "label",
            "reason": "MISSING",
            "expected_type": "string",
            "actual_type": None,
        }
    ]
