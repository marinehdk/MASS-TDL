"""Tests for YAML scenario resolution and parameter injection (GAP 3 fix).

The functions under test live in lifecycle_bridge.py but are module-level
pure functions that do NOT depend on rclpy/ROS2 at runtime.  Conftest.py
provides ROS2 mocks so these tests run on any platform including macOS.
"""

import json
import math
from pathlib import Path

import pytest

from lifecycle_bridge import _resolve_scenario_yaml, _inject_scenario_params


class TestYamlResolution:

    def test_resolve_imazu_01_ho(self):
        p = _resolve_scenario_yaml("imazu-01-ho")
        assert p is not None, "imazu-01-ho should be found"
        assert p.exists(), f"Resolved path {p} does not exist"

    def test_resolve_colreg_rule14_ho(self):
        p = _resolve_scenario_yaml("colreg-rule14-ho")
        assert p is not None, "colreg-rule14-ho should be found"
        assert p.exists(), f"Resolved path {p} does not exist"

    def test_resolve_nonexistent_returns_none(self):
        p = _resolve_scenario_yaml("this-scenario-does-not-exist-42")
        assert p is None

    def test_own_ship_params(self):
        p = _resolve_scenario_yaml("imazu-01-ho")
        params = _inject_scenario_params(p)
        sd = params["ship_dynamics_node"]
        assert abs(sd["origin_lat"] - 63.44) < 1e-6
        assert abs(sd["origin_lon"] - 10.38) < 1e-6
        assert abs(sd["psi0"] - math.pi / 2.0) < 1e-9
        assert abs(sd["u0"] - 10.0 * 0.514444) < 1e-6
        assert sd["x0"] == 0.0
        assert sd["y0"] == 0.0

    def test_target_vessel_params(self):
        p = _resolve_scenario_yaml("imazu-01-ho")
        params = _inject_scenario_params(p)
        tv = params["target_vessel_node"]
        targets = json.loads(tv["default_targets_json"])
        assert len(targets) == 1
        t0 = targets[0]
        assert abs(t0["latitude"] - 63.557451) < 1e-6
        assert abs(t0["longitude"] - 10.38) < 1e-6
        assert t0["mmsi"] == 100000001
        assert abs(t0["sog_kn"] - 10.0) < 1e-6
        assert abs(t0["heading_deg"] - 180.0) < 1e-6

    def test_env_disturbance_params(self):
        p = _resolve_scenario_yaml("imazu-01-ho")
        params = _inject_scenario_params(p)
        ev = params["env_disturbance_node"]
        assert ev["wind_speed_mps"] == 0.0
        assert ev["wind_dir_deg"] == 0.0
        assert ev["current_speed_mps"] == 0.0
        assert ev["current_dir_deg"] == 0.0

    def test_colreg_scenario_parses(self):
        p = _resolve_scenario_yaml("colreg-rule14-ho")
        params = _inject_scenario_params(p)
        sd = params["ship_dynamics_node"]
        assert abs(sd["origin_lat"] - 63.44) < 1e-6
        assert abs(sd["origin_lon"] - 10.38) < 1e-6
        assert abs(sd["psi0"] - math.pi / 2.0) < 1e-9
        assert abs(sd["u0"] - 12.0 * 0.514444) < 1e-6

    def test_all_22_imazu_yamls_resolve(self):
        yaml_dir = Path("scenarios/IMAZU标准测试")
        yamls = sorted(yaml_dir.glob("imazu-*.yaml"))
        for yaml_path in yamls:
            sid = yaml_path.stem
            p = _resolve_scenario_yaml(sid)
            assert p is not None, f"{sid} should resolve (found {yaml_path})"
            params = _inject_scenario_params(p)
            assert "ship_dynamics_node" in params
            assert "target_vessel_node" in params
            assert "env_disturbance_node" in params
        assert len(yamls) == 22, f"Expected 22 Imazu YAMLs, found {len(yamls)}"

    def test_multi_target_yaml(self):
        import yaml
        import tempfile
        p = _resolve_scenario_yaml("imazu-01-ho")
        with open(p) as f:
            data = yaml.safe_load(f)
        data["targetShips"].append({
            "id": "ts2",
            "static": {"mmsi": 100000002},
            "initial": {
                "position": {"latitude": 63.6, "longitude": 10.4},
                "cog": 90.0, "sog": 8.0, "heading": 90.0,
            },
        })
        with tempfile.NamedTemporaryFile(mode="w", suffix=".yaml", delete=False) as tf:
            yaml.dump(data, tf)
            tf_path = Path(tf.name)
        try:
            params = _inject_scenario_params(tf_path)
            targets = json.loads(params["target_vessel_node"]["default_targets_json"])
            assert len(targets) == 2
            assert targets[1]["sog_kn"] == 8.0
            assert targets[1]["mmsi"] == 100000002
        finally:
            tf_path.unlink()
