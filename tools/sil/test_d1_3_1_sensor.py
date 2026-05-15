"""Tests for D1.3.1 Sensor Calibration Runner."""
import tempfile
import sys
from pathlib import Path

import pytest
import yaml

sys.path.insert(0, str(Path(__file__).parent))
from d1_3_1_sensor_calibrate import (
    apply_degradation,
    compute_sensor_metrics,
    load_sensor_config,
    validate_against_cg0264,
)

SENSOR_YAML = {
    "radar": {
        "max_range": 12000,
        "measurement_rate": 10,
        "R_ne": 0.5,
        "P_D": 0.95,
        "clutter": 0.01,
    },
    "ais": {
        "max_range": 20000,
        "R_position": 5.0,
        "update_interval": 6,
    },
    "gnss": {
        "hdop": 1.0,
        "position_noise": 1.5,
    },
}


def test_loads_yaml_config():
    with tempfile.NamedTemporaryFile(mode="w", suffix=".yaml", delete=False) as f:
        yaml.dump(SENSOR_YAML, f)
        tmp = f.name
    try:
        config = load_sensor_config(tmp)
        assert config["radar"]["max_range"] == 12000
        assert config["ais"]["update_interval"] == 6
        assert config["gnss"]["hdop"] == 1.0
    finally:
        Path(tmp).unlink()


def test_degradation_applies_correct_multipliers():
    degraded = apply_degradation(SENSOR_YAML, "degraded")

    assert degraded["radar"]["max_range"] == 6000
    assert degraded["radar"]["R_ne"] == 2.0
    assert degraded["radar"]["P_D"] == pytest.approx(0.80)
    assert degraded["radar"]["clutter"] == 0.04

    assert degraded["ais"]["R_position"] == 10.0
    assert degraded["ais"]["update_interval"] == pytest.approx(19.8)
    assert degraded["ais"]["dropout_rate"] == 0.10

    assert degraded["gnss"]["hdop"] == 3.0
    assert degraded["gnss"]["position_noise"] == 4.5
    assert degraded["gnss"]["outage_prob"] == 0.01


def test_cg0264_limits_detected():
    nominal = apply_degradation(SENSOR_YAML, "nominal")
    metrics = compute_sensor_metrics(nominal)

    v_radar = validate_against_cg0264(metrics, "radar", "nominal")
    assert v_radar["passed"]

    v_ais = validate_against_cg0264(metrics, "ais", "nominal")
    assert v_ais["passed"]

    v_gnss = validate_against_cg0264(metrics, "gnss", "nominal")
    assert v_gnss["passed"]

    bad_gnss = {
        "gnss": {"position_std_m": 100.0, "cep95_m": 100.0, "reacq_s": 120.0}
    }
    v_fail = validate_against_cg0264(bad_gnss, "gnss", "nominal")
    assert not v_fail["passed"]
    assert len(v_fail["violations"]) == 2
