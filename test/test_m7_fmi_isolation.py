"""Test M7-FMI isolation CI lint script.

Task 7 of D1.3c: verify the CI lint script correctly detects
prohibited cosim/fmi_bridge references in M7 Safety Supervisor code.
"""

import subprocess
from pathlib import Path

LINT_SCRIPT = Path("tools/ci/check-m7-fmi-isolation.sh")
M7_DIR = Path("src/l3_tdl_kernel/m7_safety_supervisor")
M7_HEADER = M7_DIR / "include" / "m7_safety_supervisor" / "safety_supervisor_node.hpp"


def test_clean_tree_passes():
    """Clean M7 directory passes lint."""
    if not LINT_SCRIPT.exists():
        import pytest
        pytest.skip("lint script not found")

    result = subprocess.run(
        ["bash", str(LINT_SCRIPT)],
        capture_output=True, text=True,
    )
    assert result.returncode == 0, (
        f"Lint failed on clean tree:\n{result.stdout}\n{result.stderr}"
    )
    assert "PASS" in result.stdout


def test_violation_in_header_detected():
    """Violation: M7 #include <cosim/...> triggers failure."""
    if not M7_HEADER.exists():
        import pytest
        pytest.skip("M7 header not found at expected path")

    original = M7_HEADER.read_text()
    try:
        M7_HEADER.write_text('#include <cosim/cosim.hpp>\n' + original)
        result = subprocess.run(
            ["bash", str(LINT_SCRIPT)],
            capture_output=True, text=True,
        )
        assert result.returncode != 0, (
            "Lint should fail with violation\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
        assert "FAIL" in result.stdout
        assert "include" in result.stdout.lower()
    finally:
        M7_HEADER.write_text(original)
