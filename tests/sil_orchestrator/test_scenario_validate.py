"""Tests for upgraded scenario_store.validate() with jsonschema."""
import pytest
from sil_orchestrator.scenario_store import ScenarioStore

VALID_YAML = """\
title: Test Scenario
startTime: "2026-05-15T00:00:00Z"
ownShip:
  static:
    id: 1
    shipType: Cargo
  initial:
    position: {latitude: 63.44, longitude: 10.38}
    heading: 0.0
    sog: 10.0
    cog: 0.0
"""

MISSING_OWNSHIP_YAML = """\
title: Bad Scenario
startTime: "2026-05-15T00:00:00Z"
"""

INVALID_COORD_YAML = """\
title: Bad Coordinates
startTime: "2026-05-15T00:00:00Z"
ownShip:
  static:
    id: 1
    shipType: Cargo
  initial:
    position: {latitude: 95.0, longitude: 10.38}
    heading: 0.0
    sog: 10.0
    cog: 0.0
"""

EMPTY_YAML = ""


class TestScenarioValidate:
    def test_valid_yaml_passes(self):
        store = ScenarioStore()
        result = store.validate(VALID_YAML)
        assert result["valid"] is True
        assert len(result["errors"]) == 0

    def test_missing_ownship_fails(self):
        store = ScenarioStore()
        result = store.validate(MISSING_OWNSHIP_YAML)
        assert result["valid"] is False
        assert len(result["errors"]) > 0

    def test_invalid_coordinate_fails(self):
        store = ScenarioStore()
        result = store.validate(INVALID_COORD_YAML)
        assert result["valid"] is False
        assert any("latitude" in e.lower() or "95" in e for e in result["errors"])

    def test_empty_yaml_fails(self):
        store = ScenarioStore()
        result = store.validate(EMPTY_YAML)
        assert result["valid"] is False
        assert len(result["errors"]) > 0

    def test_validate_returns_errors_list(self):
        store = ScenarioStore()
        result = store.validate(MISSING_OWNSHIP_YAML)
        assert isinstance(result["errors"], list)
        assert result["errors"][0] != ""

    def test_valid_yaml_no_schema_errors(self):
        store = ScenarioStore()
        result = store.validate(VALID_YAML)
        assert result["valid"] is True
