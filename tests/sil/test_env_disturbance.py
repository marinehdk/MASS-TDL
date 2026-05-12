"""Tests for EnvDisturbanceNode — Gauss-Markov wind + current model."""
import sys
from pathlib import Path

# Ensure the package is importable when running tests from the repo root
_pkg = Path(__file__).resolve().parents[3] / "src" / "sim_workbench" / "sil_nodes" / "env_disturbance"
sys.path.insert(0, str(_pkg))

from env_disturbance.node import EnvDisturbanceNode


def test_step_returns_all_fields() -> None:
    node = EnvDisturbanceNode()
    env = node.step(1.0)
    expected = {
        "wind_direction_deg",
        "wind_speed_mps",
        "current_direction_deg",
        "current_speed_mps",
        "visibility_nm",
        "sea_state_beaufort",
    }
    assert set(env.keys()) == expected


def test_wind_speed_non_negative() -> None:
    node = EnvDisturbanceNode()
    for _ in range(100):
        env = node.step(1.0)
        assert env["wind_speed_mps"] >= 0.0


def test_wind_direction_in_range() -> None:
    node = EnvDisturbanceNode()
    for _ in range(100):
        env = node.step(1.0)
        assert 0.0 <= env["wind_direction_deg"] < 360.0


def test_default_initial_values() -> None:
    node = EnvDisturbanceNode()
    env = node.step(1.0)
    # Default initial wind = 5.0 before GM noise applies; with alpha near 1.0
    # for large tau, output should be close to 5.0
    assert env["current_speed_mps"] == 0.5
    assert env["current_direction_deg"] == 0.0
    assert env["visibility_nm"] == 10.0
    assert env["sea_state_beaufort"] == 3
