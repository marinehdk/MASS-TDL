"""Tests for scenario YAML → ROS2 parameter injection chain.

Tests verify both imazu-08 parsing correctness and error behavior
(imazu-99 nonexistent, bad data) for the LifecycleBridge helper functions
_load_scenario_yaml, _extract_injection_params, and _print_injection_summary.
"""
import json
import logging
import sys
from unittest.mock import MagicMock, patch

# Mock rclpy and related modules before any sil_orchestrator imports —
# lifecycle_bridge imports them at module level and they aren't available
# on the dev host without ROS2.  This mirrors test_selfcheck.py pattern.
sys.modules["rclpy"] = MagicMock()
sys.modules["rclpy.node"] = MagicMock()
sys.modules["rclpy.callback_groups"] = MagicMock()
sys.modules["rclpy.executors"] = MagicMock()
sys.modules["lifecycle_msgs.srv"] = MagicMock()
sys.modules["lifecycle_msgs.msg"] = MagicMock()
sys.modules["rcl_interfaces.srv"] = MagicMock()
sys.modules["rcl_interfaces.msg"] = MagicMock()

import pytest
import yaml

from sil_orchestrator.lifecycle_bridge import (
    ScenarioInjectionError,
    _load_scenario_yaml,
    _extract_injection_params,
    _print_injection_summary,
)

# ---------------------------------------------------------------------------
# Full YAML content of imazu-08-ms.yaml (from
# scenarios/IMAZU标准测试/imazu-08-ms.yaml) — used as mock ScenarioStore payload
# ---------------------------------------------------------------------------
IMAZU_08_YAML = """\
title: Imazu Case 08 — MS
description: 'Imazu (1987) benchmark case 08. OS: heading 0deg, SOG 10 kn.'
startTime: '2026-01-01T00:00:00Z'
ownShip:
  static:
    id: 1
    shipType: Cargo
    name: FCB Own Ship
    mmsi: 123456789
  initial:
    position:
      latitude: 63.44
      longitude: 10.38
    cog: 0.0
    sog: 10.0
    heading: 0.0
    navStatus: Under way using engine
  model: fcb_mmg_vessel
  controller: psbmpc_wrapper
targetShips:
  - id: ts1
    static:
      id: 2
      mmsi: 100000001
    initial:
      position:
        latitude: 63.503492
        longitude: 10.241335
      cog: 90.0
      sog: 10.0
      heading: 90.0
    model: ais_replay_vessel
  - id: ts2
    static:
      id: 3
      mmsi: 100000002
    initial:
      position:
        latitude: 63.566984
        longitude: 10.38
      cog: 180.0
      sog: 10.0
      heading: 180.0
    model: ais_replay_vessel
environment:
  wind:
    dir_deg: 0.0
    speed_mps: 0.0
  current:
    dir_deg: 0.0
    speed_mps: 0.0
  visibility_nm: 5.4
metadata:
  schema_version: '3.0'
  scenario_id: imazu-08-ms-v1.0
  vessel_class: FCB
  odd_cell:
    domain: open_sea_offshore_wind_farm
  encounter:
    rule: Rule14
    give_way_vessel: own
    expected_own_action: turn_starboard
    avoidance_time_s: 300.0
    avoidance_delta_rad: 0.6109
    avoidance_duration_s: 90.0
  scenario_source: imazu1987
  expected_outcome:
    cpa_min_m_ge: 300.0
  simulation_settings:
    total_time: 1000.0
    dt: 0.02
    n_rps_initial: 3.0
    coordinate_origin: [63.44, 10.38]
    dynamics_mode: internal
    backend: ros2
  disturbance:
    wind: {dir_deg: 0.0, speed_mps: 0.0}
    current: {dir_deg: 0.0, speed_mps: 0.0}
"""


# ---------------------------------------------------------------------------
# _load_scenario_yaml — YAML loading & error handling
# ---------------------------------------------------------------------------

class TestLoadScenarioYaml:
    """Verify _load_scenario_yaml returns parsed dict for valid scenarios
    and raises ScenarioInjectionError for missing / invalid scenarios."""

    @patch("sil_orchestrator.scenario_store.ScenarioStore")
    def test_imazu08_returns_dict_with_required_keys(self, mock_store):
        """Loading imazu-08-ms returns dict with ownShip, targetShips,
        environment, metadata."""
        mock_instance = mock_store.return_value
        mock_instance.get.return_value = {
            "yaml_content": IMAZU_08_YAML,
            "hash": "abc123",
            "backend": "demo",
            "is_baseline": True,
        }
        result = _load_scenario_yaml("imazu-08-ms")
        assert isinstance(result, dict)
        assert "ownShip" in result
        assert "targetShips" in result
        assert "environment" in result
        assert "metadata" in result

    @patch("sil_orchestrator.scenario_store.ScenarioStore")
    def test_imazu99_raises_scenario_injection_error(self, mock_store):
        """Loading imazu-99-ms (nonexistent) raises ScenarioInjectionError,
        not a silent default."""
        mock_instance = mock_store.return_value
        mock_instance.get.return_value = None
        with pytest.raises(ScenarioInjectionError, match="not found"):
            _load_scenario_yaml("imazu-99-ms")

    @patch("sil_orchestrator.scenario_store.ScenarioStore")
    def test_nonexistent_xyz_raises_scenario_injection_error(self, mock_store):
        """Loading completely unknown scenario raises ScenarioInjectionError."""
        mock_instance = mock_store.return_value
        mock_instance.get.return_value = None
        with pytest.raises(ScenarioInjectionError, match="not found"):
            _load_scenario_yaml("nonexistent_xyz")


# ---------------------------------------------------------------------------
# _extract_injection_params — parameter extraction from parsed YAML
# ---------------------------------------------------------------------------

@pytest.fixture
def imazu08_parsed():
    """Return parsed YAML dict for imazu-08-ms."""
    return yaml.safe_load(IMAZU_08_YAML)


class TestExtractInjectionParams:
    """Verify _extract_injection_params maps YAML fields to correct
    ROS2 node parameters."""

    # -- imazu-08 correctness tests -----------------------------------------

    def test_ship_dynamics_params_match_yaml(self, imazu08_parsed):
        """ship_dynamics_node receives initial_lat, initial_lon, heading,
        sog, cog with exact YAML values (tolerance 1e-6)."""
        result = _extract_injection_params(imazu08_parsed)
        ship = result.get("ship_dynamics_node", {})
        assert "initial_lat" in ship
        assert "initial_lon" in ship
        assert "initial_heading" in ship
        assert "initial_sog" in ship
        assert "initial_cog" in ship
        assert ship["initial_lat"][0] == pytest.approx(63.44, abs=1e-6)
        assert ship["initial_lon"][0] == pytest.approx(10.38, abs=1e-6)
        assert ship["initial_heading"][0] == pytest.approx(0.0, abs=1e-6)
        assert ship["initial_sog"][0] == pytest.approx(10.0, abs=1e-6)
        assert ship["initial_cog"][0] == pytest.approx(0.0, abs=1e-6)

    def test_target_vessel_params_has_two_targets(self, imazu08_parsed):
        """target_vessel_node receives default_targets_json containing
        exactly 2 target ships."""
        result = _extract_injection_params(imazu08_parsed)
        tv = result.get("target_vessel_node", {})
        assert "default_targets_json" in tv
        targets = json.loads(tv["default_targets_json"][0])
        assert isinstance(targets, list)
        assert len(targets) == 2

    def test_scenario_lifecycle_mgr_has_scenario_id(self, imazu08_parsed):
        """scenario_lifecycle_mgr receives scenario_id matching
        metadata.scenario_id from YAML."""
        result = _extract_injection_params(imazu08_parsed)
        mgr = result.get("scenario_lifecycle_mgr", {})
        assert "scenario_id" in mgr
        assert mgr["scenario_id"][0] == "imazu-08-ms-v1.0"

    def test_environment_params_match_yaml(self, imazu08_parsed):
        """env_disturbance_node receives wind_dir_deg, wind_speed_mps,
        current_dir_deg, current_speed_mps from YAML."""
        result = _extract_injection_params(imazu08_parsed)
        env = result.get("env_disturbance_node", {})
        assert env["wind_dir_deg"][0] == pytest.approx(0.0, abs=1e-6)
        assert env["wind_speed_mps"][0] == pytest.approx(0.0, abs=1e-6)
        assert env["current_dir_deg"][0] == pytest.approx(0.0, abs=1e-6)
        assert env["current_speed_mps"][0] == pytest.approx(0.0, abs=1e-6)

    # -- error / edge-case tests --------------------------------------------

    def test_bad_data_missing_ownship(self):
        """Missing ownShip key is handled gracefully — no
        ship_dynamics_node entry in injection map."""
        result = _extract_injection_params({
            "targetShips": [],
            "environment": {"wind": {}, "current": {}},
        })
        assert "ship_dynamics_node" not in result

    def test_empty_yaml_returns_empty_map(self):
        """Empty dict produces empty injection map."""
        result = _extract_injection_params({})
        assert result == {}


# ---------------------------------------------------------------------------
# _print_injection_summary — logging output
# ---------------------------------------------------------------------------

class TestPrintInjectionSummary:
    """Verify _print_injection_summary logs node names and param counts."""

    def test_summary_logs_node_names_and_count(self, caplog):
        """With non-empty injection map, summary logs total param count
        and each node name."""
        caplog.set_level(logging.DEBUG)
        injection_map = {
            "ship_dynamics_node": {"initial_lat": (63.44, MagicMock())},
            "target_vessel_node": {"default_targets_json": ("[]", MagicMock())},
        }
        _print_injection_summary(injection_map)
        assert "2 params across 2 nodes" in caplog.text
        assert "ship_dynamics_node" in caplog.text
        assert "target_vessel_node" in caplog.text

    def test_empty_map_logs_no_params_message(self, caplog):
        """Empty injection map logs 'no parameters to inject'."""
        caplog.set_level(logging.INFO)
        _print_injection_summary({})
        assert "no parameters to inject" in caplog.text
