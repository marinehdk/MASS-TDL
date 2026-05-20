"""Tests for batch_runner v2 features — multi-dir discovery + Arrow dual output.

(D1.3.2.1) These tests verify the infrastructure without running actual
simulations (no ROS2 available on macOS).
"""
from __future__ import annotations

import tempfile
from pathlib import Path

import pyarrow as pa
import pytest



def test_discover_scenarios_returns_at_least_22():
    """Default discovery reads both IMAZU and COLREGs directories,
    returning at least 22 files."""
    from batch_runner import discover_scenarios

    files = discover_scenarios()
    assert len(files) >= 22


def test_discover_scenarios_imazu_only_returns_22():
    """IMAZU-only discovery returns exactly the 22 Imazu scenarios."""
    from batch_runner import discover_scenarios

    imazu_dir = Path("scenarios/IMAZU标准测试")
    files = discover_scenarios([imazu_dir])
    assert len(files) == 22


def test_summary_arrow_roundtrip():
    """SUMMARY_SCHEMA round-trips through the Arrow IPC file format."""
    from batch_runner import SUMMARY_SCHEMA

    rows = [
        {
            "scenario_id": "imazu-01-ho",
            "pass": True,
            "dcpa_m": 120.5,
            "wall_clock_s": 12.3,
            "duration_s": 180.0,
        },
        {
            "scenario_id": "colreg-rule14-ho",
            "pass": False,
            "dcpa_m": 5.2,
            "wall_clock_s": 15.7,
            "duration_s": 200.0,
        },
    ]
    col_names = [f.name for f in SUMMARY_SCHEMA]
    data = {col: [r[col] for r in rows] for col in col_names}
    table = pa.table(data, schema=SUMMARY_SCHEMA)

    with tempfile.NamedTemporaryFile(suffix=".arrow") as f:
        with pa.ipc.new_file(f.name, SUMMARY_SCHEMA) as writer:
            writer.write_table(table)
        reader = pa.ipc.open_file(f.name)
        restored = reader.read_all()

    assert restored.schema == SUMMARY_SCHEMA
    assert len(restored) == 2
    assert restored.column("scenario_id").to_pylist() == [
        "imazu-01-ho",
        "colreg-rule14-ho",
    ]
    assert restored.column("pass").to_pylist() == [True, False]
    assert restored.column("dcpa_m").to_pylist() == [
        pytest.approx(120.5),
        pytest.approx(5.2),
    ]


def test_trajectory_arrow_roundtrip():
    """TRAJECTORY_POINT_SCHEMA round-trips through the Arrow IPC format."""
    from batch_runner import TRAJECTORY_POINT_SCHEMA

    rows = [
        {
            "scenario_id": "imazu-01-ho",
            "t_s": 0.0,
            "x_m": 0.0,
            "y_m": 0.0,
            "psi_rad": 0.5,
            "u_mps": 5.0,
            "v_mps": 0.0,
            "r_radps": 0.01,
        },
        {
            "scenario_id": "imazu-01-ho",
            "t_s": 10.0,
            "x_m": 50.0,
            "y_m": 3.0,
            "psi_rad": 0.52,
            "u_mps": 5.1,
            "v_mps": 0.1,
            "r_radps": 0.015,
        },
    ]
    col_names = [f.name for f in TRAJECTORY_POINT_SCHEMA]
    data = {col: [r[col] for r in rows] for col in col_names}
    table = pa.table(data, schema=TRAJECTORY_POINT_SCHEMA)

    with tempfile.NamedTemporaryFile(suffix=".arrow") as f:
        with pa.ipc.new_file(f.name, TRAJECTORY_POINT_SCHEMA) as writer:
            writer.write_table(table)
        reader = pa.ipc.open_file(f.name)
        restored = reader.read_all()

    assert restored.schema == TRAJECTORY_POINT_SCHEMA
    assert len(restored) == 2
    assert restored.column("x_m").to_pylist() == [
        pytest.approx(0.0),
        pytest.approx(50.0),
    ]
    assert restored.column("u_mps").to_pylist() == [
        pytest.approx(5.0),
        pytest.approx(5.1),
    ]
