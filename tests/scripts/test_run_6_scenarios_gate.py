import importlib.util
from pathlib import Path

import yaml


def _load_runner():
    path = Path(__file__).resolve().parents[2] / "scripts" / "run_6_scenarios.py"
    spec = importlib.util.spec_from_file_location("run_6_scenarios", path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def test_overall_gate_requires_returned_to_route():
    runner = _load_runner()
    assert runner.compute_overall_pass(
        cpa_ok=True,
        stability_pass=True,
        returned_to_route=False,
    ) is False


def test_overall_gate_passes_when_all_required_signals_pass():
    runner = _load_runner()
    assert runner.compute_overall_pass(
        cpa_ok=True,
        stability_pass=True,
        returned_to_route=True,
    ) is True


def test_route_return_uses_m4_behavior_when_bridge_active_is_stale():
    runner = _load_runner()
    records = [
        {
            "topic": "/l3/m4/behavior_plan",
            "sim_t": 10.0,
            "behavior": 1,
            "avoidance_active": True,
        },
        {
            "topic": "/l3/m4/behavior_plan",
            "sim_t": 20.0,
            "behavior": 0,
            "avoidance_active": True,
        },
        {
            "topic": "/sil/own_ship_state",
            "sim_t": 21.0,
            "lat": 63.4401,
            "lon": 10.38,
            "heading_deg": 0.0,
        },
    ]

    status = runner.compute_route_return_status(
        records,
        lat0=63.44,
        lon0=10.38,
        init_lat=63.44,
        init_lon=10.38,
        init_hdg=0.0,
    )

    assert status["released_after_avoidance"] is True
    assert status["returned_to_route"] is True


def test_clean_probe_yaml_requires_1200s_route_return():
    runner = _load_runner()
    root = Path(__file__).resolve().parents[2]
    for scenario_id in runner.SCENARIOS:
        path = root / "scenarios" / "COLREGs测试" / f"{scenario_id}.yaml"
        data = yaml.safe_load(path.read_text())
        settings = data["metadata"]["simulation_settings"]
        expected = data["metadata"]["expected_outcome"]
        assert settings["total_time"] == 1200.0, scenario_id
        assert expected["returned_to_route_required"] is True, scenario_id
        assert expected["route_return_xte_m_lt"] == 150.0, scenario_id
        assert expected["route_return_heading_deg_lt"] == 10.0, scenario_id
