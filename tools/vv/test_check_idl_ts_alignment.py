"""Unit tests for check_idl_ts_alignment - run with: pytest tools/vv/test_check_idl_ts_alignment.py"""
import subprocess, sys, textwrap
from pathlib import Path

SAT_TS = Path("web/src/types/sat.ts")

def test_sat_ts_exists():
    assert SAT_TS.exists(), f"Expected {SAT_TS} to exist"

def test_script_exits_zero_on_real_file():
    result = subprocess.run(
        [sys.executable, "tools/vv/check_idl_ts_alignment.py", "--sat-ts", str(SAT_TS)],
        capture_output=True, text=True
    )
    assert result.returncode == 0, f"STDOUT: {result.stdout}\nSTDERR: {result.stderr}"

def test_script_detects_mismatch(tmp_path):
    broken = tmp_path / "sat.ts"
    report = tmp_path / "idl_alignment_report.json"
    broken.write_text(textwrap.dedent("""
        export interface IvpContribution {
          direction_deg: number;
          cost: number;
          // label MISSING - mismatch
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
