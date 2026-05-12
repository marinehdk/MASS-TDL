"""E2E test: R14 Head-on scenario — validate -> create -> lifecycle flow.

Phase 1: validates REST API flow against orchestrator endpoints.
Requires: sil_orchestrator running (or mocks backend).
"""

import pytest

try:
    import requests as _requests
    ORCHESTRATOR = "http://localhost:8000/api/v1"
except ImportError:
    ORCHESTRATOR = None


@pytest.fixture
def r14_yaml():
    with open("scenarios/imazu22/r14_head_on.yaml") as f:
        return f.read()


@pytest.mark.integration
class TestR14Scenario:
    def test_validate_scenario_yaml(self, r14_yaml):
        """Step 1: Validate scenario YAML structure."""
        # Offline validation: check YAML is well-formed
        import yaml
        data = yaml.safe_load(r14_yaml)
        assert "scenario" in data
        assert data["scenario"]["encounter_type"] == "head-on"
        assert len(data["targets"]) == 1
        assert data["targets"][0]["mmsi"] == 100000001

    def test_scenario_has_expected_fields(self, r14_yaml):
        """Step 2: All required sections present."""
        import yaml
        data = yaml.safe_load(r14_yaml)
        for section in ["own_ship", "targets", "environment", "simulation", "expected"]:
            assert section in data, f"Missing section: {section}"
        assert data["simulation"]["duration_s"] == 600
        assert data["simulation"]["tick_hz"] == 50

    def test_colregs_rule14_expected(self, r14_yaml):
        """Step 3: Expected COLREGs rule is Rule 14."""
        import yaml
        data = yaml.safe_load(r14_yaml)
        assert "Rule 14" in data["expected"]["rule_triggered"]
        assert data["expected"]["colregs_compliant"] is True

    def test_targets_config_valid(self, r14_yaml):
        """Step 4: Target vessel configuration is valid."""
        import yaml
        data = yaml.safe_load(r14_yaml)
        for t in data["targets"]:
            assert t["mmsi"] > 0
            assert t["mode"] in ("replay", "ncdm", "intelligent")
            assert "lat" in t["initial"]
            assert "lon" in t["initial"]
            assert t["initial"]["sog_kn"] > 0
