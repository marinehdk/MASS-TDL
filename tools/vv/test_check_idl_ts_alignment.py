"""Unit tests for check_idl_ts_alignment — run with: pytest tools/vv/test_check_idl_ts_alignment.py"""
import subprocess
import sys
import textwrap
from pathlib import Path

SAT_TS = Path("web/src/types/sat.ts")


def test_sat_ts_exists():
    """Sanity: sat.ts must be loadable."""
    assert SAT_TS.exists(), f"Expected {SAT_TS} to exist"


def test_script_exits_zero_on_real_file():
    """Full integration: real sat.ts → exit 0."""
    result = subprocess.run(
        [sys.executable, "tools/vv/check_idl_ts_alignment.py", "--sat-ts", str(SAT_TS)],
        capture_output=True, text=True,
    )
    assert result.returncode == 0, (
        f"STDOUT: {result.stdout}\nSTDERR: {result.stderr}"
    )


def test_script_detects_mismatch(tmp_path):
    """Broken sat.ts (missing field) → non-zero exit + mismatch in output."""
    broken = tmp_path / "sat.ts"
    broken.write_text(textwrap.dedent("""\
        export interface IvpContribution {
          direction_deg: number;
          cost: number;
          // label MISSING — mismatch
        }
    """))
    result = subprocess.run(
        [sys.executable, "tools/vv/check_idl_ts_alignment.py",
         "--sat-ts", str(broken)],
        capture_output=True, text=True,
    )
    assert result.returncode != 0
    output = (result.stdout + result.stderr).lower()
    assert "mismatch" in output, f"Expected 'mismatch' in output:\n{output}"
