"""Tests for D1.3.1 Orchestration Trace Audit Tool."""
import json
import sys
import tempfile
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
from d1_3_1_orch_trace import (
    audit_fmu_order,
    audit_step_completeness,
    audit_step_consistency,
    audit_timing,
    generate_audit_report,
    parse_trace,
    _validate_schema,
)


def _make_trace(timestamps=None, durations=None, fmu_calls=None, labels=None, expected=None):
    n = len(timestamps) if timestamps is not None else (len(labels) if labels else 5)
    ts = timestamps or np.linspace(0, 0.04, n).tolist()
    durs = durations or [500] * n
    calls = fmu_calls or [["FMU_A", "FMU_B", "FMU_C"]] * n
    labs = labels or [f"step_{i}" for i in range(n)]
    exp = expected or labs
    steps = [
        {"index": i, "timestamp": ts[i], "duration_us": durs[i],
         "fmu_calls": calls[i], "label": labs[i]}
        for i in range(n)
    ]
    return {"steps": steps, "expected_steps": exp}


def test_consistent_trace_passes_audit():
    trace = _make_trace()
    result = audit_step_consistency(trace)
    assert result["passed"]


def test_inconsistent_dt_fails():
    timestamps = [0.0, 0.01, 0.018, 0.031, 0.04]
    trace = _make_trace(timestamps=timestamps)
    result = audit_step_consistency(trace)
    assert not result["passed"]
    assert result["dt_std_s"] > 0.001


def test_missing_steps_detected():
    trace = _make_trace(labels=["step_0", "step_2", "step_1"], expected=["step_0", "step_1", "step_2"])
    result = audit_step_completeness(trace)
    assert result["passed"]
    result2 = audit_step_completeness(
        _make_trace(labels=["step_0", "step_1"], expected=["step_0", "step_1", "step_2"])
    )
    assert not result2["passed"]
    assert result2["missing"] == ["step_2"]


def test_overrun_detected():
    trace = _make_trace(durations=[2000, 3000, 6000, 2500, 4000])
    result = audit_timing(trace)
    assert not result["passed"]
    assert result["overrun_count"] == 1


def test_schema_validation():
    valid = _make_trace()
    assert _validate_schema(valid)["valid"]
    bad = {"steps": "not_a_list", "expected_steps": []}
    assert not _validate_schema(bad)["valid"]
    bad2 = {"steps": [{"index": 0, "timestamp": 0, "duration_us": 100}], "expected_steps": []}
    v = _validate_schema(bad2)
    assert not v["valid"]
    assert any("fmu_calls" in e for e in v["errors"])
