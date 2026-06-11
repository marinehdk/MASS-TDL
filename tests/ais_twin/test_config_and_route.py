from pathlib import Path
import importlib
from datetime import datetime, timezone

import pytest

from ais_twin.config import load_config
from ais_twin.model import CanonicalAISRecord, RoutePoint
from ais_twin.route import load_route_points, route_bbox, route_duration_hours


def test_safe_route_config_has_expected_bbox_and_capture_window():
    cfg = load_config(Path("src/sim_workbench/ais_twin/config/safe_route_aisstream.yaml"))

    assert cfg.provider == "aisstream"
    assert cfg.capture_duration_hours == 10.0
    assert cfg.route_path == Path("scenarios/集成测试/safe_route.yaml")
    assert cfg.bbox.lat_min == -4.503333
    assert cfg.bbox.lat_max == -1.136667
    assert cfg.bbox.lon_min == 104.786263
    assert cfg.bbox.lon_max == 108.513737
    assert cfg.risk_top_n == 20


def test_console_target_modules_are_importable_with_callable_main():
    for module_name in (
        "ais_twin.capture_cli",
        "ais_twin.replay_node",
        "ais_twin.debug_api",
    ):
        module = importlib.import_module(module_name)

        assert callable(module.main)


def test_safe_route_duration_and_raw_bbox():
    points = load_route_points(Path("scenarios/集成测试/safe_route.yaml"))

    bbox = route_bbox(points)
    duration = route_duration_hours(points)

    assert len(points) == 324
    assert bbox.lat_min == -4.17
    assert bbox.lat_max == -1.47
    assert bbox.lon_min == 105.12
    assert bbox.lon_max == 108.18
    assert round(duration, 2) == 9.56


def test_canonical_record_flags_missing_heading():
    record = CanonicalAISRecord(
        provider="aisstream",
        received_at_utc=datetime(2026, 6, 11, tzinfo=timezone.utc),
        ais_time_utc=datetime(2026, 6, 11, tzinfo=timezone.utc),
        mmsi=123456789,
        lat=-2.0,
        lon=106.0,
        sog_kn=12.4,
        cog_deg=85.0,
        heading_deg=None,
        nav_status="under_way",
        ship_name="TEST VESSEL",
        ship_type=None,
        raw_message_type="PositionReport",
        raw_json={"MessageType": "PositionReport"},
        quality_flags=frozenset({"missing_heading"}),
    )

    assert record.quality_flags == frozenset({"missing_heading"})


def test_route_duration_rejects_non_positive_speed():
    points = [
        RoutePoint(lat=-2.0, lon=106.0, target_sog_kn=0.0),
        RoutePoint(lat=-2.0, lon=106.1, target_sog_kn=10.0),
    ]

    with pytest.raises(ValueError, match="target_sog_kn must be positive"):
        route_duration_hours(points)


def test_canonical_record_raw_json_recursively_immutable_and_hashable():
    record = CanonicalAISRecord(
        provider="aisstream",
        received_at_utc=datetime(2026, 6, 11, tzinfo=timezone.utc),
        ais_time_utc=datetime(2026, 6, 11, tzinfo=timezone.utc),
        mmsi=123456789,
        lat=-2.0,
        lon=106.0,
        sog_kn=12.4,
        cog_deg=85.0,
        heading_deg=None,
        nav_status="under_way",
        ship_name="TEST VESSEL",
        ship_type=None,
        raw_message_type="PositionReport",
        raw_json={"MessageType": "PositionReport", "nested": {"a": 1}, "items": [1]},
        quality_flags=frozenset(),
    )

    with pytest.raises(TypeError):
        record.raw_json["MessageType"] = "Changed"
    with pytest.raises(TypeError):
        record.raw_json["nested"]["a"] = 2
    with pytest.raises(AttributeError):
        record.raw_json["items"].append(2)
    assert isinstance(hash(record), int)
