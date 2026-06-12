import asyncio

import pytest
from starlette.requests import Request

from sil_orchestrator.encounters_routes import _active_mmsis, _counter, router


class _Result:
    def __init__(self, success=True, message="ok"):
        self.success = success
        self.message = message


class _OwnShip:
    lat = 2.5
    lon = 101.0
    heading = 0.0
    sog = 6.172


class _Bridge:
    def __init__(self):
        self.added = []
        self.removed = []
        self.failed_removals = set()

    def get_latest_own_ship(self):
        return _OwnShip()

    async def add_target(self, **kwargs):
        self.added.append(kwargs)
        return _Result()

    async def remove_target(self, mmsi):
        self.removed.append(mmsi)
        return _Result(success=mmsi not in self.failed_removals)


@pytest.fixture
def request_and_bridge():
    _counter["n"] = 0
    _active_mmsis.clear()
    bridge = _Bridge()
    scope = {
        "type": "http",
        "method": "DELETE",
        "path": "/api/v1/encounters",
        "headers": [],
        "app": type("App", (), {"state": type("State", (), {"bridge": bridge})()})(),
    }
    return Request(scope), bridge


def test_clear_all_encounters_removes_all_injected_mmsis(request_and_bridge):
    asyncio.run(_assert_clear_all_encounters_removes_all_injected_mmsis(request_and_bridge))


async def _assert_clear_all_encounters_removes_all_injected_mmsis(request_and_bridge):
    request, bridge = request_and_bridge
    inject = next(route.endpoint for route in router.routes if route.path == "/api/v1/encounters/inject")
    clear_all = next(route.endpoint for route in router.routes if route.path == "/api/v1/encounters")

    first = await inject(type("Body", (), {"rule": "head_on", "range_nm": None,
                                           "construct_cpa_m": None, "approach_angle_deg": None})(), request)
    second = await inject(type("Body", (), {"rule": "overtaking", "range_nm": None,
                                            "construct_cpa_m": None, "approach_angle_deg": None})(), request)

    resp = await clear_all(request)

    assert resp == {"removed_count": 2}
    assert set(bridge.removed) == {
        first["mmsi"],
        second["mmsi"],
    }
    assert _active_mmsis == set()


def test_overtaking_injection_uses_four_knot_target(request_and_bridge):
    asyncio.run(_assert_overtaking_injection_uses_four_knot_target(request_and_bridge))


async def _assert_overtaking_injection_uses_four_knot_target(request_and_bridge):
    request, bridge = request_and_bridge
    inject = next(route.endpoint for route in router.routes if route.path == "/api/v1/encounters/inject")

    await inject(type("Body", (), {"rule": "overtaking", "range_nm": None,
                                   "construct_cpa_m": None, "approach_angle_deg": None})(), request)

    assert bridge.added[-1]["sog_kn"] == pytest.approx(4.0, abs=0.2)


def test_clear_all_drops_stale_removals_from_registry(request_and_bridge):
    asyncio.run(_assert_clear_all_drops_stale_removals_from_registry(request_and_bridge))


async def _assert_clear_all_drops_stale_removals_from_registry(request_and_bridge):
    request, bridge = request_and_bridge
    inject = next(route.endpoint for route in router.routes if route.path == "/api/v1/encounters/inject")
    clear_all = next(route.endpoint for route in router.routes if route.path == "/api/v1/encounters")

    first = await inject(type("Body", (), {"rule": "head_on", "range_nm": None,
                                           "construct_cpa_m": None, "approach_angle_deg": None})(), request)
    second = await inject(type("Body", (), {"rule": "overtaking", "range_nm": None,
                                            "construct_cpa_m": None, "approach_angle_deg": None})(), request)
    bridge.failed_removals.add(second["mmsi"])

    resp = await clear_all(request)

    assert resp == {"removed_count": 1, "stale_mmsis": [second["mmsi"]]}
    assert _active_mmsis == set()
