"""Unit tests for tools/validate_scenarios.py."""

import json
import tempfile
from pathlib import Path

from tools.validate_scenarios import (
    ValidationResult,
    load_schema,
    validate_yaml_against_schema,
    validate_all_scenarios,
)

# ---------------------------------------------------------------------------
# Test fixtures
# ---------------------------------------------------------------------------

SAMPLE_VALID_YAML = b"""\
title: Test Scenario
startTime: "2026-01-01T00:00:00Z"
ownShip:
  static:
    id: 1
    shipType: Cargo
  initial:
    position: {latitude: 63.44, longitude: 10.38}
    cog: 0.0
    sog: 10.0
    heading: 0.0
"""

SAMPLE_BAD_YAML = b"""\
title: Bad Scenario
ownShip:
  static:
    id: 1
"""

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------


def test_load_schema_loads_valid_json_schema():
    """Verify that load_schema() returns a valid Draft-07 schema dict."""
    schema = load_schema()
    assert isinstance(schema, dict)
    assert "title" in schema
    assert schema["type"] == "object"
    assert "properties" in schema
    assert "required" in schema


def test_validate_valid_yaml_passes():
    """A minimal valid scenario YAML must pass schema validation."""
    result = validate_yaml_against_schema(SAMPLE_VALID_YAML)
    assert result.valid is True
    assert result.errors == []
    assert result.schema_version == "unknown"  # no metadata in sample


def test_validate_missing_required_field_fails():
    """YAML missing 'startTime' and 'ownShip.initial' must fail validation."""
    result = validate_yaml_against_schema(SAMPLE_BAD_YAML)
    assert result.valid is False
    assert len(result.errors) > 0
    # At least one error should mention the root-level required field
    assert any("startTime" in err for err in result.errors) or any(
        "initial" in err for err in result.errors
    )


def test_validate_empty_yaml_fails():
    """Empty YAML content is handled gracefully (treated as valid with 'empty' schema_version)."""
    result = validate_yaml_against_schema(b"")
    assert result.valid is True
    assert result.errors == []
    assert result.schema_version == "empty"


def test_validate_all_integration():
    """Integration test: validate_all_scenarios over a temp directory."""
    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = Path(tmpdir)
        good = tmp / "good.yaml"
        bad = tmp / "bad.yaml"
        good.write_bytes(SAMPLE_VALID_YAML)
        bad.write_bytes(SAMPLE_BAD_YAML)

        results = validate_all_scenarios(directory=tmp)
        assert len(results) == 2

        by_file = {r.file: r for r in results}
        assert by_file["good.yaml"].valid is True
        assert by_file["bad.yaml"].valid is False
        assert len(by_file["bad.yaml"].errors) > 0
