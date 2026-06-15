from types import SimpleNamespace

import pytest

from external_adapters.converters import route_points_to_planned_route_dict
from external_adapters.l2_route_plan_adaptor import (
    RoutePlanValidationError,
    route_plan_signature,
    route_plan_to_payload,
    should_forward_route,
    stable_route_id_from_string,
)
from external_adapters.neutral import NeutralRoutePoint


def _route_plan(
    *,
    lats=None,
    lons=None,
    speeds=None,
    modes=None,
    route_id="WH-SZ-001",
    route_type="transit",
    frame_id="map",
):
    stamp = SimpleNamespace(sec=40, nanosec=500)
    header = SimpleNamespace(stamp=stamp, frame_id=frame_id)
    return SimpleNamespace(
        header=header,
        latitude=list(lats if lats is not None else [31.0, 31.1, 31.2]),
        longitude=list(lons if lons is not None else [121.0, 121.1, 121.2]),
        speed_limit_mps=list(speeds if speeds is not None else [5.14444, 0.0, 2.57222]),
        navigation_mode=list(modes if modes is not None else ["cruise", "", "approach"]),
        route_id=route_id,
        route_type=route_type,
    )


@pytest.mark.parametrize(
    ("msg", "match"),
    [
        (_route_plan(lats=[], lons=[]), "at least two waypoints"),
        (_route_plan(lats=[31.0], lons=[121.0]), "at least two waypoints"),
        (_route_plan(lats=[31.0, 31.1], lons=[121.0]), "same length"),
        (_route_plan(lats=[31.0, float("nan")], lons=[121.0, 121.1]), "finite"),
        (_route_plan(lats=[31.0, 31.1], lons=[121.0, float("inf")]), "finite"),
        (_route_plan(lats=[31.0, 31.1], lons=[121.0, 121.1], speeds=[1.0]), "speed_limit_mps"),
        (_route_plan(lats=[31.0, 31.1], lons=[121.0, 121.1], speeds=[1.0, 2.0, 3.0]), "speed_limit_mps"),
        (_route_plan(lats=[31.0, 31.1], lons=[121.0, 121.1], modes=["cruise"]), "navigation_mode"),
        (_route_plan(lats=[31.0, 31.1], lons=[121.0, 121.1], modes=["cruise", "approach", "dock"]), "navigation_mode"),
    ],
)
def test_route_plan_to_payload_rejects_invalid_route(msg, match):
    with pytest.raises(RoutePlanValidationError, match=match):
        route_plan_to_payload(msg)


@pytest.mark.parametrize(
    ("route_id", "expected"),
    [
        ("WH-SZ-001", 2670686514),
        ("WH-SZ-002", 2800948709),
    ],
)
def test_stable_route_id_from_string_uses_literal_hash_contract(route_id, expected):
    assert stable_route_id_from_string(route_id) == expected


def test_route_plan_to_payload_maps_external_route_to_tdl_route_in():
    msg = _route_plan(speeds=[2.57222, 0.0, 6.173328])

    payload = route_plan_to_payload(msg, default_speed_kn=10.0)

    assert payload["kind"] == "route_in"
    assert payload["schema_version"] == 112
    assert payload["stamp"] == {"sec": 40, "nanosec": 500}
    assert payload["route_id"] == 2670686514
    assert payload["route"]["header"] == {"stamp": {"sec": 40, "nanosec": 500}, "frame_id": "WGS84"}
    assert [pose["pose"]["position"]["latitude"] for pose in payload["route"]["poses"]] == [31.0, 31.1, 31.2]
    assert [pose["pose"]["position"]["longitude"] for pose in payload["route"]["poses"]] == [121.0, 121.1, 121.2]
    assert payload["speed_profile_kn"] == pytest.approx([5.0, 10.0])
    assert payload["confidence"] == pytest.approx(1.0)
    assert "route_id=WH-SZ-001" in payload["rationale"]
    assert "route_type=transit" in payload["rationale"]
    assert "navigation_modes=cruise,approach" in payload["rationale"]


def test_route_plan_to_payload_accepts_empty_optional_arrays():
    msg = _route_plan(speeds=[], modes=[])

    payload = route_plan_to_payload(msg, default_speed_kn=8.0)

    assert payload["kind"] == "route_in"
    assert payload["speed_profile_kn"] == pytest.approx([8.0, 8.0])


def test_empty_route_id_uses_waypoint_signature_hash():
    msg = _route_plan(route_id="", speeds=[2.57222, 0.0, 6.173328])
    expected_route_id = route_points_to_planned_route_dict(
        40,
        500,
        [
            NeutralRoutePoint(lat=31.0, lon=121.0, speed_kn=5.0),
            NeutralRoutePoint(lat=31.1, lon=121.1, speed_kn=10.0),
            NeutralRoutePoint(lat=31.2, lon=121.2, speed_kn=12.0),
        ],
    )["route_id"]
    assert expected_route_id == 2092677501

    payload = route_plan_to_payload(msg, default_speed_kn=10.0)

    assert payload["route_id"] == 2092677501
    assert payload["route_id"] == expected_route_id
    assert payload["route_id"] != stable_route_id_from_string("WH-SZ-001")


def test_route_plan_signature_and_forward_decision_are_stable():
    first = _route_plan()
    second = _route_plan()
    changed = _route_plan(lons=[121.0, 121.1, 121.25])

    first_signature = route_plan_signature(first)
    assert first_signature == route_plan_signature(second)
    assert first_signature != route_plan_signature(changed)
    assert should_forward_route(None, first_signature)
    assert not should_forward_route(first_signature, first_signature)
    assert should_forward_route(first_signature, route_plan_signature(changed))
