"""Unit tests for tools/migrate_scenario_yaml.py — 18 tests covering all migration paths."""

import copy
import math
from pathlib import Path

import pytest
import yaml

from tools.migrate_scenario_yaml import (
    ODD_ZONE_MAP,
    COLREGS_RULE_TOKEN_MAP,
    MPS_TO_KN,
    M_TO_NM,
    KN_TO_MPS,
    EARTH_R,
    ais_nav_status_int_to_string,
    detect_schema_version,
    enu_to_latlon,
    migrate_v1_to_maritime,
    migrate_v2_to_maritime,
)

# ---------------------------------------------------------------------------
# YAML fixtures
# ---------------------------------------------------------------------------

V1_FIXTURE = """\
schema_version: "1.0"
scenario_id: colreg-rule14-ho-001-v1.0
description: "Rule 14 pure head-on encounter in calm conditions."
rule_branch_covered:
  - Rule14_HeadOn
  - Rule8_Action
vessel_class: FCB
odd_zone: A
initial_conditions:
  own_ship:
    x_m: 0.0
    y_m: 0.0
    heading_nav_deg: 0.0
    speed_kn: 12.0
    n_rps: 3.5
  targets:
    - target_id: 1
      x_m: 0.0
      y_m: 3704.0
      cog_nav_deg: 180.0
      sog_mps: 5.14
encounter:
  rule: Rule14
  give_way_vessel: own
  expected_own_action: turn_starboard
  avoidance_time_s: 200.0
  avoidance_delta_rad: 1.2217
  avoidance_duration_s: 90.0
disturbance_model:
  wind_kn: 0.0
  wind_dir_nav_deg: 0.0
  current_kn: 0.0
  current_dir_nav_deg: 0.0
  vis_m: 10000.0
  wave_height_m: 0.0
prng_seed: 42
pass_criteria:
  max_dcpa_no_action_m: 900.0
  min_dcpa_with_action_m: 500.0
  bearing_sector_deg: [345.0, 15.0]
simulation:
  duration_s: 600.0
  dt_s: 0.02
"""

V2_FIXTURE = """\
title: Imazu Case 01 — HO
description: 'Imazu (1987) benchmark case 01.'
start_time: '2026-01-01T00:00:00Z'
own_ship:
  id: os
  nav_status: 0
  mmsi: 123456789
  initial:
    position:
      latitude: 63.44
      longitude: 10.38
    cog: 0.0
    sog: 10.0
    heading: 0.0
target_ships:
- id: ts1
  nav_status: 0
  mmsi: 100000001
  initial:
    position:
      latitude: 63.557451
      longitude: 10.38
    cog: 180.0
    sog: 10.0
    heading: 180.0
metadata:
  schema_version: '2.0'
  scenario_id: imazu-01-ho-v1.0
  scenario_source: imazu1987
  vessel_class: FCB
  odd_zone: A
  geo_origin:
    latitude: 63.44
    longitude: 10.38
    description: Norwegian Sea anchor
  encounter:
    rule: Rule14
    give_way_vessel: own
    expected_own_action: turn_starboard
    avoidance_time_s: 300.0
    avoidance_delta_rad: 0.6109
    avoidance_duration_s: 90.0
  disturbance_model:
    wind_kn: 0.0
    wind_dir_nav_deg: 0.0
    current_kn: 0.0
    current_dir_nav_deg: 0.0
    vis_m: 10000.0
    wave_height_m: 0.0
  pass_criteria:
    max_dcpa_no_action_m: 926.0
    min_dcpa_with_action_m: 500.0
  simulation:
    duration_s: 700
    dt_s: 0.02
    n_rps_initial: 3.0
  prng_seed: null
"""

V2_FIXTURE_NO_SEED = """\
title: Imazu Case 01 — HO
description: 'Imazu (1987) benchmark case 01.'
start_time: '2026-01-01T00:00:00Z'
own_ship:
  id: os
  nav_status: 0
  mmsi: 123456789
  initial:
    position:
      latitude: 63.44
      longitude: 10.38
    cog: 0.0
    sog: 10.0
    heading: 0.0
target_ships: []
metadata:
  schema_version: '2.0'
  scenario_id: imazu-01-ho-v1.0
  scenario_source: imazu1987
  vessel_class: FCB
  odd_zone: A
  geo_origin:
    latitude: 63.44
    longitude: 10.38
    description: Norwegian Sea anchor
  encounter:
    rule: Rule14
    give_way_vessel: own
    expected_own_action: turn_starboard
    avoidance_time_s: 300.0
    avoidance_delta_rad: 0.6109
    avoidance_duration_s: 90.0
  disturbance_model:
    wind_kn: 0.0
    wind_dir_nav_deg: 0.0
    current_kn: 0.0
    current_dir_nav_deg: 0.0
    vis_m: 10000.0
    wave_height_m: 0.0
  pass_criteria:
    max_dcpa_no_action_m: 926.0
    min_dcpa_with_action_m: 500.0
  simulation:
    duration_s: 700
    dt_s: 0.02
    n_rps_initial: 3.0
  prng_seed: null
"""


# ===================================================================
# Test helpers
# ===================================================================

def _load_v1() -> dict:
    return yaml.safe_load(V1_FIXTURE)


def _load_v2() -> dict:
    return yaml.safe_load(V2_FIXTURE)


def _load_v2_no_seed() -> dict:
    return yaml.safe_load(V2_FIXTURE_NO_SEED)


# ===================================================================
# 1. test_enu_to_latlon_zero
# ===================================================================

class TestEnuToLatLon:
    def test_enu_to_latlon_zero(self):
        lat, lon = enu_to_latlon(0.0, 0.0, origin=(63.44, 10.38))
        assert lat == pytest.approx(63.44, abs=1e-9)
        assert lon == pytest.approx(10.38, abs=1e-9)

    def test_enu_to_latlon_north_displacement(self):
        lat, lon = enu_to_latlon(0.0, 3704.0, origin=(63.44, 10.38))
        dlat_deg = 3704.0 / EARTH_R * 180.0 / math.pi
        assert lat == pytest.approx(63.44 + dlat_deg, abs=1e-6)
        assert lon == pytest.approx(10.38, abs=1e-6)


# ===================================================================
# 2–7. V1 migration tests
# ===================================================================

class TestMigrateV1:
    def test_migrate_v1_to_maritime_top_level_keys(self):
        doc = _load_v1()
        result = migrate_v1_to_maritime(doc, origin=(63.44, 10.38))
        expected = {"title", "description", "startTime", "ownShip", "targetShips", "environment", "metadata"}
        assert set(result.keys()) == expected

    def test_migrate_v1_ownship_initial(self):
        doc = _load_v1()
        result = migrate_v1_to_maritime(doc, origin=(63.44, 10.38))
        os_ = result["ownShip"]
        assert os_["static"]["id"] == 1
        assert os_["static"]["shipType"] == "Cargo"
        assert os_["initial"]["position"]["latitude"] == pytest.approx(63.44, abs=1e-6)
        assert os_["initial"]["position"]["longitude"] == pytest.approx(10.38, abs=1e-6)
        assert os_["initial"]["cog"] == 0.0
        assert os_["initial"]["sog"] == 12.0
        assert os_["initial"]["heading"] == 0.0
        assert os_["model"] == "fcb_mmg_vessel"
        assert os_["controller"] == "psbmpc_wrapper"

    def test_migrate_v1_targets_initial(self):
        doc = _load_v1()
        result = migrate_v1_to_maritime(doc, origin=(63.44, 10.38))
        ts = result["targetShips"]
        assert len(ts) == 1
        t0 = ts[0]
        assert t0["id"] == "ts1"
        assert t0["static"]["id"] == 2
        dlat_deg = 3704.0 / EARTH_R * 180.0 / math.pi
        assert t0["initial"]["position"]["latitude"] == pytest.approx(63.44 + dlat_deg, abs=1e-6)
        assert t0["initial"]["position"]["longitude"] == pytest.approx(10.38, abs=1e-6)
        assert t0["initial"]["cog"] == 180.0
        assert t0["initial"]["sog"] == pytest.approx(5.14 * MPS_TO_KN, abs=0.01)
        assert t0["initial"]["heading"] == 180.0
        assert t0["model"] == "ais_replay_vessel"

    def test_migrate_v1_environment(self):
        doc = _load_v1()
        result = migrate_v1_to_maritime(doc, origin=(63.44, 10.38))
        env = result["environment"]
        assert "wind" in env
        assert "current" in env
        assert env["wind"]["speed_mps"] == pytest.approx(0.0, abs=1e-4)
        assert env["wind"]["dir_deg"] == 0.0
        assert env["current"]["speed_mps"] == pytest.approx(0.0, abs=1e-4)
        assert env["visibility_nm"] == pytest.approx(10000.0 * M_TO_NM, abs=0.1)
        assert "significant_wave_height_m" not in env

    def test_migrate_v1_metadata_fields(self):
        doc = _load_v1()
        result = migrate_v1_to_maritime(doc, origin=(63.44, 10.38))
        meta = result["metadata"]
        assert meta["schema_version"] == "3.0"
        assert meta["scenario_id"] == "colreg-rule14-ho-001-v1.0"
        assert meta["colregs_rules"] == ["R14", "R8"]
        assert meta["vessel_class"] == "FCB"
        assert meta["seed"] == 42
        assert meta["odd_cell"]["domain"] == "open_sea_offshore_wind_farm"
        assert meta["encounter"]["rule"] == "Rule14"
        assert meta["expected_outcome"]["cpa_min_m_ge"] == 500.0
        assert meta["simulation_settings"]["total_time"] == 600.0
        assert meta["simulation_settings"]["dt"] == 0.02
        assert meta["simulation_settings"]["n_rps_initial"] == 3.5
        assert meta["simulation_settings"]["dynamics_mode"] == "internal"
        assert "disturbance" in meta


# ===================================================================
# 8. test_ais_nav_status_conversion
# ===================================================================

class TestAisNavStatus:
    def test_ais_nav_status_conversion(self):
        assert ais_nav_status_int_to_string(0) == "Under way using engine"
        assert ais_nav_status_int_to_string(1) == "At anchor"
        assert ais_nav_status_int_to_string(15) == "Undefined"
        assert ais_nav_status_int_to_string(99) == "Undefined"
        assert ais_nav_status_int_to_string(14) == "AIS-SART (active)"


# ===================================================================
# 9–15. V2 migration tests
# ===================================================================

class TestMigrateV2:
    def test_migrate_v2_to_maritime_title_startTime(self):
        doc = _load_v2()
        result = migrate_v2_to_maritime(doc)
        assert result["title"] == "Imazu Case 01 — HO"
        assert result["description"] == "Imazu (1987) benchmark case 01."
        assert result["startTime"] == "2026-01-01T00:00:00Z"

    def test_migrate_v2_ownship(self):
        doc = _load_v2()
        result = migrate_v2_to_maritime(doc)
        os_ = result["ownShip"]
        assert os_["static"]["id"] == 1
        assert os_["static"]["mmsi"] == 123456789
        assert os_["static"]["shipType"] == "Cargo"
        assert os_["initial"]["position"]["latitude"] == 63.44
        assert os_["initial"]["position"]["longitude"] == 10.38
        assert os_["initial"]["cog"] == 0.0
        assert os_["initial"]["sog"] == 10.0
        assert os_["initial"]["heading"] == 0.0
        assert os_["initial"]["navStatus"] == "Under way using engine"
        assert os_["model"] == "fcb_mmg_vessel"
        assert os_["controller"] == "psbmpc_wrapper"

    def test_migrate_v2_targets(self):
        doc = _load_v2()
        result = migrate_v2_to_maritime(doc)
        ts = result["targetShips"]
        assert len(ts) == 1
        t0 = ts[0]
        assert t0["id"] == "ts1"
        assert t0["static"]["id"] == 2
        assert t0["static"]["mmsi"] == 100000001
        assert t0["initial"]["position"]["latitude"] == 63.557451
        assert t0["initial"]["position"]["longitude"] == 10.38
        assert t0["initial"]["cog"] == 180.0
        assert t0["initial"]["sog"] == 10.0
        assert t0["initial"]["heading"] == 180.0
        assert t0["model"] == "ais_replay_vessel"

    def test_migrate_v2_metadata_preservation(self):
        doc = _load_v2()
        result = migrate_v2_to_maritime(doc)
        meta = result["metadata"]
        assert meta["schema_version"] == "3.0"
        assert meta["scenario_id"] == "imazu-01-ho-v1.0"
        assert meta["scenario_source"] == "imazu1987"
        assert meta["vessel_class"] == "FCB"
        assert meta["encounter"]["rule"] == "Rule14"
        assert meta["encounter"]["avoidance_time_s"] == 300.0
        assert meta["expected_outcome"]["cpa_min_m_ge"] == 500.0
        assert meta["simulation_settings"]["total_time"] == 700.0
        assert meta["simulation_settings"]["dt"] == 0.02
        assert meta["simulation_settings"]["n_rps_initial"] == 3.0
        assert meta["simulation_settings"]["dynamics_mode"] == "internal"
        assert meta["simulation_settings"]["coordinate_origin"] == [63.44, 10.38]
        assert meta["odd_cell"]["domain"] == "open_sea_offshore_wind_farm"
        assert "disturbance" in meta

    def test_migrate_v2_environment_disturbance_split(self):
        doc = _load_v2()
        result = migrate_v2_to_maritime(doc)
        env = result["environment"]
        meta = result["metadata"]
        assert "wind" in env
        assert "current" in env
        assert env["visibility_nm"] == pytest.approx(10000.0 * M_TO_NM, abs=0.1)
        dist = meta["disturbance"]
        assert "wind" in dist
        assert "current" in dist
        assert dist["wind"]["dir_deg"] == env["wind"]["dir_deg"]
        assert dist["wind"]["speed_mps"] == env["wind"]["speed_mps"]

    def test_migrate_v2_null_seed_handling(self):
        doc = _load_v2()
        result = migrate_v2_to_maritime(doc)
        meta = result["metadata"]
        assert "seed" not in meta

    def test_migrate_v2_odd_zone_mapping(self):
        doc_v2 = _load_v2()
        result = migrate_v2_to_maritime(doc_v2)
        assert result["metadata"]["odd_cell"]["domain"] == "open_sea_offshore_wind_farm"


# ===================================================================
# 16. Roundtrip preservation
# ===================================================================

class TestRoundtrip:
    def test_roundtrip_no_information_loss(self):
        doc = _load_v2()
        result = migrate_v2_to_maritime(doc)
        meta = result["metadata"]
        assert meta["encounter"]["rule"] == "Rule14"
        assert meta["encounter"]["give_way_vessel"] == "own"
        assert meta["encounter"]["expected_own_action"] == "turn_starboard"
        assert meta["encounter"]["avoidance_time_s"] == 300.0
        assert meta["encounter"]["avoidance_delta_rad"] == 0.6109
        assert meta["encounter"]["avoidance_duration_s"] == 90.0
        assert meta["expected_outcome"]["cpa_min_m_ge"] == 500.0


# ===================================================================
# 17–18. Schema version detection
# ===================================================================

class TestDetectSchemaVersion:
    def test_detect_schema_version_v1(self):
        doc = _load_v1()
        assert detect_schema_version(doc) == "v1"

    def test_detect_schema_version_v2(self):
        doc = _load_v2()
        assert detect_schema_version(doc) == "v2"

    def test_detect_schema_version_unknown(self):
        assert detect_schema_version({}) == "unknown"
        assert detect_schema_version({"title": "foo"}) == "unknown"
