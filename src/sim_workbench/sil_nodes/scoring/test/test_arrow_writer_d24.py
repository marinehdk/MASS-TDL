"""D2.4 tests for ArrowWriter schema extension."""
from __future__ import annotations
import sys, tempfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parents[2]))
import pyarrow as pa
import pyarrow.ipc as ipc
import pytest
from scoring.arrow_writer import ArrowWriter
from scoring.hagen_scorer import ScoringRow

def _make_row(**kw) -> ScoringRow:
    defaults = dict(stamp=1.0, safety=1.0, rule_compliance=1.0,
                    delay_penalty=0.0, action_magnitude_penalty=0.0,
                    phase_score=1.0, plausibility=1.0, total=0.85,
                    cpa_nm=0.5, cpa_target_nm=0.27,
                    pass_fail=True, applicable_rule="Rule14")
    defaults.update(kw)
    return ScoringRow(**defaults)

def test_schema_has_pass_fail_column():
    assert "pass_fail" in ArrowWriter.SCHEMA.names

def test_schema_pass_fail_is_bool():
    field = ArrowWriter.SCHEMA.field("pass_fail")
    assert field.type == pa.bool_()

def test_schema_has_applicable_rule_column():
    assert "applicable_rule" in ArrowWriter.SCHEMA.names

def test_schema_applicable_rule_is_string():
    field = ArrowWriter.SCHEMA.field("applicable_rule")
    assert field.type == pa.string()

def test_roundtrip_pass_fail_written_and_read():
    with tempfile.NamedTemporaryFile(suffix=".arrow", delete=False) as f:
        path = f.name
    with ArrowWriter(path) as w:
        w.append(_make_row(pass_fail=True, applicable_rule="Rule14"))
        w.append(_make_row(pass_fail=False, applicable_rule="Rule15"))
    with pa.memory_map(path, "r") as source:
        table = ipc.open_file(source).read_all()
    assert table.column("pass_fail").to_pylist() == [True, False]
    assert table.column("applicable_rule").to_pylist() == ["Rule14", "Rule15"]
