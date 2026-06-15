import json
from pathlib import Path

import pytest
import yaml

import external_adapters.l2_route_seed as l2_route_seed
from external_adapters.l2_route_seed import (
    RouteSeedError,
    scenario_to_bridge_route,
    write_bridge_route_file,
)


def _valid_waypoint(**overrides):
    waypoint = {"latitude": -1.5, "longitude": 105.12, "target_sog_kn": 29.16}
    waypoint.update(overrides)
    return waypoint


def _valid_route():
    return [
        _valid_waypoint(),
        {"latitude": -1.491952, "longitude": 105.136095, "target_sog_kn": 26.24},
    ]


def _write_payload(path, payload):
    path.write_text(yaml.safe_dump(payload, allow_unicode=True, sort_keys=False), encoding="utf-8")


def _write_scenario(path, route, scenario_id="safe_route"):
    path.write_text(
        yaml.safe_dump(
            {
                "ownShip": {"nominalRoute": route},
                "metadata": {"scenario_id": scenario_id},
            },
            allow_unicode=True,
            sort_keys=False,
        ),
        encoding="utf-8",
    )


def test_scenario_to_bridge_route_maps_nominal_route(tmp_path):
    scenario = tmp_path / "stem_fallback_should_not_win.yaml"
    _write_scenario(scenario, _valid_route(), scenario_id="safe_route")

    route = scenario_to_bridge_route(scenario)

    assert route["route_id"] == "safe_route-initial"
    assert route["route_type"] == "transit"
    assert route["selected_key"] == "safe_route"
    assert route["sample_points"] == [
        {"lat": -1.5, "lon": 105.12, "speed_kn": 29.16},
        {"lat": -1.491952, "lon": 105.136095, "speed_kn": 26.24},
    ]


@pytest.mark.parametrize(
    ("payload", "match"),
    [
        ({"metadata": {"scenario_id": "missing_ownship"}}, "ownShip"),
        ({"ownShip": {}, "metadata": {"scenario_id": "missing_route"}}, "nominalRoute"),
        (
            {"ownShip": {"nominalRoute": "not-a-list"}, "metadata": {"scenario_id": "route_not_list"}},
            "nominalRoute",
        ),
        (
            {"ownShip": {"nominalRoute": [_valid_waypoint(), "not-a-waypoint"]}, "metadata": {"scenario_id": "bad_wp"}},
            "waypoint",
        ),
        (
            {
                "ownShip": {
                    "nominalRoute": [
                        {"latitude": -1.5, "target_sog_kn": 29.16},
                        _valid_waypoint(),
                    ]
                },
                "metadata": {"scenario_id": "missing_lon"},
            },
            "longitude",
        ),
        (
            {
                "ownShip": {
                    "nominalRoute": [
                        {"longitude": 105.12, "target_sog_kn": 29.16},
                        _valid_waypoint(),
                    ]
                },
                "metadata": {"scenario_id": "missing_lat"},
            },
            "latitude",
        ),
        (
            {
                "ownShip": {"nominalRoute": [_valid_waypoint(latitude=float("nan")), _valid_waypoint()]},
                "metadata": {"scenario_id": "nan_lat"},
            },
            "finite",
        ),
        (
            {
                "ownShip": {"nominalRoute": [_valid_waypoint(longitude=float("inf")), _valid_waypoint()]},
                "metadata": {"scenario_id": "inf_lon"},
            },
            "finite",
        ),
        (
            {
                "ownShip": {"nominalRoute": [_valid_waypoint(target_sog_kn=float("-inf")), _valid_waypoint()]},
                "metadata": {"scenario_id": "inf_speed"},
            },
            "finite",
        ),
        (
            {
                "ownShip": {"nominalRoute": [_valid_waypoint(latitude=90.1), _valid_waypoint()]},
                "metadata": {"scenario_id": "lat_range"},
            },
            "latitude",
        ),
        (
            {
                "ownShip": {"nominalRoute": [_valid_waypoint(longitude=-180.1), _valid_waypoint()]},
                "metadata": {"scenario_id": "lon_range"},
            },
            "longitude",
        ),
        (
            {
                "ownShip": {"nominalRoute": [_valid_waypoint(target_sog_kn=-0.1), _valid_waypoint()]},
                "metadata": {"scenario_id": "negative_speed"},
            },
            "speed",
        ),
    ],
)
def test_scenario_to_bridge_route_rejects_invalid_nominal_route(tmp_path, payload, match):
    scenario = tmp_path / "invalid.yaml"
    _write_payload(scenario, payload)

    with pytest.raises(RouteSeedError, match=match):
        scenario_to_bridge_route(scenario)


def test_scenario_to_bridge_route_rejects_short_route(tmp_path):
    scenario = tmp_path / "short.yaml"
    _write_scenario(scenario, [_valid_waypoint()])

    with pytest.raises(RouteSeedError, match="at least two"):
        scenario_to_bridge_route(scenario)


def test_write_bridge_route_file_uses_same_directory_atomic_replace(tmp_path, monkeypatch):
    output = tmp_path / "gnc_bridge_route.json"
    route = {
        "route_id": "safe_route-initial",
        "route_type": "transit",
        "selected_key": "safe_route",
        "sample_points": [
            {"lat": -1.5, "lon": 105.12, "speed_kn": 29.16},
            {"lat": -1.49, "lon": 105.13, "speed_kn": 29.16},
        ],
    }
    calls = []
    original_replace = l2_route_seed.os.replace

    def recording_replace(src, dst):
        src_path = Path(src)
        dst_path = Path(dst)
        assert src_path.parent == output.parent
        assert src_path != output
        assert dst_path == output
        assert json.loads(src_path.read_text(encoding="utf-8")) == route
        calls.append((src_path, dst_path))
        original_replace(src, dst)

    monkeypatch.setattr(l2_route_seed.os, "replace", recording_replace)

    write_bridge_route_file(route, output)

    assert len(calls) == 1
    temp_path, final_path = calls[0]
    assert final_path == output
    assert json.loads(output.read_text(encoding="utf-8")) == route
    assert not temp_path.exists()
