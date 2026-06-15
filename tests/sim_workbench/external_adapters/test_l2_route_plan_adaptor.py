from types import SimpleNamespace

import pytest

from external_adapters.converters import route_points_to_planned_route_dict
from external_adapters.l2_route_plan_adaptor import (
    ACTIVE_STATE,
    L2RoutePlanAdaptorNode,
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


class _FakeLogger:
    def info(self, message):
        pass

    def warn(self, message):
        pass

    def error(self, message):
        pass


def _adaptor_node(*, active=False, strict_active=True, send_payload=None):
    node = object.__new__(L2RoutePlanAdaptorNode)
    node._default_speed_kn = 10.0
    node._strict_active = strict_active
    node._active = active
    node._last_signature = None
    node._pending_route = None
    node._pending_signature = None
    node._forwarded_once = False
    node._seen_valid_route = False
    node.get_logger = lambda: _FakeLogger()
    node._send_payload = send_payload or (lambda payload: None)
    return node


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
    ("msg", "match"),
    [
        (_route_plan(lats=[90.1, 31.1], lons=[121.0, 121.1], speeds=[], modes=[]), "latitude"),
        (_route_plan(lats=[31.0, -90.1], lons=[121.0, 121.1], speeds=[], modes=[]), "latitude"),
        (_route_plan(lats=[31.0, 31.1], lons=[180.1, 121.1], speeds=[], modes=[]), "longitude"),
        (_route_plan(lats=[31.0, 31.1], lons=[121.0, -180.1], speeds=[], modes=[]), "longitude"),
        (_route_plan(lats=[31.0, 31.1], lons=[121.0, 121.1], speeds=[1.0, -0.1], modes=[]), "speed_limit_mps"),
    ],
)
def test_route_plan_to_payload_rejects_invalid_wgs84_and_negative_speed(msg, match):
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


def test_startup_timeout_does_not_exit_after_valid_route_cached_before_active(monkeypatch):
    exit_codes = []
    monkeypatch.setattr(
        "external_adapters.l2_route_plan_adaptor.os._exit",
        lambda code: exit_codes.append(code),
    )
    node = _adaptor_node(active=False, strict_active=True)

    node._on_route_plan(_route_plan())
    node._on_startup_timeout()

    assert exit_codes == []


def test_cached_route_forwards_once_on_active_and_duplicate_does_not_resend():
    sent_payloads = []
    node = _adaptor_node(
        active=False,
        strict_active=True,
        send_payload=lambda payload: sent_payloads.append(payload),
    )
    msg = _route_plan()

    node._on_route_plan(msg)
    node._on_lifecycle_status(SimpleNamespace(current_state=ACTIVE_STATE))
    node._on_route_plan(_route_plan())

    assert len(sent_payloads) == 1
    assert sent_payloads[0]["route_id"] == stable_route_id_from_string("WH-SZ-001")


def test_send_failure_keeps_pending_route_and_retry_later_forwards():
    sent_payloads = []

    def flaky_send(payload):
        sent_payloads.append(payload)
        if len(sent_payloads) == 1:
            raise OSError("ingress unavailable")

    node = _adaptor_node(active=True, strict_active=False, send_payload=flaky_send)
    msg = _route_plan(route_id="WH-SZ-002")
    signature = route_plan_signature(msg)

    node._on_route_plan(msg)

    assert node._pending_route is msg
    assert node._pending_signature == signature

    node._on_retry_timer()

    assert len(sent_payloads) == 2
    assert node._pending_route is None
    assert node._pending_signature is None
    assert node._last_signature == signature


def test_newer_success_clears_stale_pending_route_after_prior_send_failure():
    sent_route_ids = []

    def send_fails_for_route_a(payload):
        sent_route_ids.append(payload["route_id"])
        if payload["route_id"] == stable_route_id_from_string("WH-SZ-001"):
            raise OSError("ingress unavailable")

    node = _adaptor_node(
        active=True,
        strict_active=False,
        send_payload=send_fails_for_route_a,
    )
    route_a = _route_plan(route_id="WH-SZ-001")
    route_b = _route_plan(route_id="WH-SZ-002", lons=[121.0, 121.1, 121.25])
    signature_a = route_plan_signature(route_a)
    signature_b = route_plan_signature(route_b)

    node._on_route_plan(route_a)
    assert node._pending_signature == signature_a

    node._on_route_plan(route_b)
    assert node._last_signature == signature_b

    node._on_retry_timer()

    assert sent_route_ids == [
        stable_route_id_from_string("WH-SZ-001"),
        stable_route_id_from_string("WH-SZ-002"),
    ]
    assert node._pending_route is None
    assert node._pending_signature is None
    assert node._last_signature == signature_b


def test_duplicate_current_route_clears_stale_pending_after_prior_send_failure():
    sent_route_ids = []
    failed_route_a_once = False
    route_a_id = stable_route_id_from_string("WH-SZ-001")
    route_b_id = stable_route_id_from_string("WH-SZ-002")

    def send_fails_first_route_a(payload):
        nonlocal failed_route_a_once
        sent_route_ids.append(payload["route_id"])
        if payload["route_id"] == route_a_id and not failed_route_a_once:
            failed_route_a_once = True
            raise OSError("ingress unavailable")

    node = _adaptor_node(
        active=True,
        strict_active=False,
        send_payload=send_fails_first_route_a,
    )
    route_b = _route_plan(route_id="WH-SZ-002", lons=[121.0, 121.1, 121.25])
    route_a = _route_plan(route_id="WH-SZ-001")
    signature_b = route_plan_signature(route_b)

    node._on_route_plan(route_b)
    assert node._last_signature == signature_b

    node._on_route_plan(route_a)
    assert node._pending_route is route_a

    node._on_route_plan(_route_plan(route_id="WH-SZ-002", lons=[121.0, 121.1, 121.25]))
    node._on_retry_timer()

    assert sent_route_ids == [route_b_id, route_a_id]
    assert node._pending_route is None
    assert node._pending_signature is None
    assert node._last_signature == signature_b
