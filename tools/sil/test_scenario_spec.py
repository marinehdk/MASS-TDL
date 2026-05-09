"""Tests for ScenarioSpec Pydantic model."""
import math
import sys
import textwrap
from pathlib import Path

import pytest
import yaml

# Add sil directory to path so imports work from repo root
sys.path.insert(0, str(Path(__file__).parent))

from scenario_spec import (
    DisturbanceModel,
    Encounter,
    InitialConditions,
    OwnShip,
    PassCriteria,
    ScenarioSpec,
    SimulationConfig,
    Target,
)


MINIMAL_YAML = textwrap.dedent("""\
    schema_version: "1.0"
    scenario_id: colreg-rule14-ho-001-v1.0
    description: Test head-on scenario
    rule_branch_covered:
      - Rule14_HeadOn
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
      avoidance_delta_rad: 0.6109
      avoidance_duration_s: 60.0
    disturbance_model:
      wind_kn: 0.0
      wind_dir_nav_deg: 0.0
      current_kn: 0.0
      current_dir_nav_deg: 0.0
      vis_m: 10000.0
      wave_height_m: 0.0
    prng_seed: 42
    pass_criteria:
      max_dcpa_no_action_m: 926.0
      min_dcpa_with_action_m: 926.0
      bearing_sector_deg: [345.0, 15.0]
    simulation:
      duration_s: 600.0
      dt_s: 0.02
""")


def test_parse_minimal_yaml():
    data = yaml.safe_load(MINIMAL_YAML)
    spec = ScenarioSpec.model_validate(data)
    assert spec.scenario_id == "colreg-rule14-ho-001-v1.0"
    assert spec.prng_seed == 42
    assert spec.simulation.duration_s == 600.0
    assert spec.simulation.dt_s == 0.02


def test_own_ship_speed_conversion():
    data = yaml.safe_load(MINIMAL_YAML)
    spec = ScenarioSpec.model_validate(data)
    own = spec.initial_conditions.own_ship
    assert own.speed_kn == 12.0
    assert abs(own.speed_mps - 12.0 * 0.5144) < 1e-6


def test_own_ship_psi_math_conversion():
    data = yaml.safe_load(MINIMAL_YAML)
    spec = ScenarioSpec.model_validate(data)
    own = spec.initial_conditions.own_ship
    # heading_nav_deg=0 (North) → psi_math = pi/2
    assert abs(own.psi_math_rad - math.pi / 2) < 1e-9


def test_missing_required_field_raises():
    data = yaml.safe_load(MINIMAL_YAML)
    del data["scenario_id"]
    with pytest.raises(Exception):
        ScenarioSpec.model_validate(data)


def test_simulation_dt_must_be_0_02():
    data = yaml.safe_load(MINIMAL_YAML)
    data["simulation"]["dt_s"] = 0.05
    with pytest.raises(Exception, match="dt_s"):
        ScenarioSpec.model_validate(data)


def test_simulation_duration_must_be_gte_600():
    data = yaml.safe_load(MINIMAL_YAML)
    data["simulation"]["duration_s"] = 300.0
    with pytest.raises(Exception, match="duration_s"):
        ScenarioSpec.model_validate(data)
