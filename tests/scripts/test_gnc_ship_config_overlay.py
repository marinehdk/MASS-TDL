from pathlib import Path

import yaml


ROOT = Path(__file__).resolve().parents[2]
OVERLAY = ROOT / "docker" / "gnc-ship-config-overlay.yaml"


def _params(data: dict, node: str) -> dict:
    return data.get(node, {}).get("ros__parameters", {})


def test_gnc_ship_config_overlay_is_passthrough_config() -> None:
    data = yaml.safe_load(OVERLAY.read_text())

    assert isinstance(data, dict)
    assert "ship_dynamics_node" in data
    assert "thrust_allocation_node" in data


def test_gnc_ship_config_overlay_preserves_thruster_contract() -> None:
    data = yaml.safe_load(OVERLAY.read_text())
    dynamics_thrusters = _params(data, "ship_dynamics_node").get("thrusters", {})
    allocation_names = _params(data, "thrust_allocation_node").get("thruster_names")

    assert allocation_names == ["t1", "t2", "t3", "tb1", "tb2", "r1", "r2"]
    assert all(name in dynamics_thrusters for name in allocation_names)


def test_gnc_ship_config_overlay_allows_m5_emergency_avoidance_ladder() -> None:
    data = yaml.safe_load(OVERLAY.read_text())
    coord_params = _params(data, "coordinate_transform_node")

    assert coord_params["max_dynamic_lateral_delta_m"] == 100.0
    assert coord_params["min_future_update_distance_m"] == 500.0
    assert 0.0 < coord_params["min_route_update_interval_s"] <= 2.0
    assert coord_params["emergency_avoidance_relax_update_guard"] is True
    assert coord_params["emergency_avoidance_min_future_update_distance_m"] == 0.0
    assert coord_params["emergency_avoidance_max_dynamic_lateral_delta_m"] >= 1200.0


def test_gnc_ship_config_overlay_matches_strict_probe_timebase() -> None:
    data = yaml.safe_load(OVERLAY.read_text())
    dynamics_params = _params(data, "ship_dynamics_node")
    guidance_params = _params(data, "ship_guidance_node")
    control_params = _params(data, "ship_control_node")

    assert dynamics_params["time_scale"] == 10.0
    assert dynamics_params["update_rate"] == 500.0
    assert guidance_params["guidance_period_s"] == 0.05
    assert control_params["control_period"] == 0.01
