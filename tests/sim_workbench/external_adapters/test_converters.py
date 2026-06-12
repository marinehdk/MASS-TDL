from types import SimpleNamespace

import pytest

from external_adapters.converters import (
    avoidance_plan_to_path_payload,
    neutral_environment_to_canonical_dict,
    neutral_ownship_to_canonical_dict,
    neutral_targets_to_canonical_dict,
    route_points_to_planned_route_dict,
)
from external_adapters.neutral import (
    NeutralEnvironment,
    NeutralOwnship,
    NeutralRoutePoint,
    NeutralTarget,
)


def test_neutral_targets_to_canonical_dict_maps_l3_fields_and_min_confidence():
    targets = [
        NeutralTarget(
            target_id=7,
            lat=31.1,
            lon=121.2,
            sog_kn=12.5,
            cog_deg=83.0,
            heading_deg=84.0,
            source_sensor="ais",
            confidence=0.8,
        ),
        NeutralTarget(
            target_id=9,
            lat=31.3,
            lon=121.4,
            sog_kn=6.0,
            cog_deg=191.0,
            heading_deg=188.0,
            source_sensor="radar",
            confidence=0.65,
        ),
    ]

    payload = neutral_targets_to_canonical_dict(12, 345, targets)

    assert payload["kind"] == "targets"
    assert payload["schema_version"] == 112
    assert payload["stamp"] == {"sec": 12, "nanosec": 345}
    assert payload["confidence"] == pytest.approx(0.65)
    assert payload["rationale"] == "external neutral targets converted to canonical target set"
    first = payload["targets"][0]
    assert "lat" not in first
    assert "lon" not in first
    assert first == {
        "schema_version": 112,
        "stamp": {"sec": 12, "nanosec": 345},
        "target_id": 7,
        "position": {"latitude": 31.1, "longitude": 121.2, "altitude": 0.0},
        "sog_kn": 12.5,
        "cog_deg": 83.0,
        "heading_deg": 84.0,
        "covariance": [0.0] * 9,
        "classification": "vessel",
        "classification_confidence": 0.0,
        "cpa_m": 0.0,
        "tcpa_s": 0.0,
        "encounter": {
            "schema_version": 112,
            "stamp": {"sec": 12, "nanosec": 345},
            "confidence": 0.0,
            "rationale": "M2 owns classification",
            "encounter_type": 0,
            "relative_bearing_deg": 0.0,
            "aspect_angle_deg": 0.0,
            "is_giveway": False,
        },
        "confidence": 0.8,
        "rationale": "external neutral target converted to canonical l3 target",
        "source_sensor": "ais",
        "cpa_covariance_m2": 0.0,
        "tcpa_covariance_s2": 0.0,
        "intent_confidence": 0.0,
        "brg_deg": 0.0,
        "rng_m": 0.0,
    }
    assert isinstance(first["classification"], str)
    assert payload["targets"][1]["position"] == {"latitude": 31.3, "longitude": 121.4, "altitude": 0.0}
    assert payload["targets"][1]["encounter"]["rationale"] == "M2 owns classification"


def test_neutral_ownship_to_canonical_dict_maps_motion_current_and_covariance():
    ownship = NeutralOwnship(
        stamp_sec=22,
        stamp_nanosec=440,
        lat=30.5,
        lon=122.5,
        sog_kn=10.0,
        cog_deg=45.0,
        heading_deg=47.0,
        u_water=1.2,
        v_water=-0.4,
        r_dot_deg_s=0.03,
        current_speed_kn=1.5,
        current_direction_deg=210.0,
        confidence=0.91,
        nav_mode="auto",
    )

    payload = neutral_ownship_to_canonical_dict(ownship)

    assert payload["kind"] == "ownship"
    assert payload["schema_version"] == 112
    assert payload["stamp"] == {"sec": 22, "nanosec": 440}
    assert payload["position"] == {"latitude": 30.5, "longitude": 122.5, "altitude": 0.0}
    assert "motion" not in payload
    assert "current" not in payload
    assert payload["sog_kn"] == 10.0
    assert payload["cog_deg"] == 45.0
    assert payload["heading_deg"] == 47.0
    assert payload["u_water"] == 1.2
    assert payload["v_water"] == -0.4
    assert payload["r_dot_deg_s"] == 0.03
    assert payload["current_speed_kn"] == 1.5
    assert payload["current_direction_deg"] == 210.0
    assert payload["roll_deg"] == 0.0
    assert payload["pitch_deg"] == 0.0
    assert payload["covariance"] == [0.0] * 36
    assert payload["nav_mode"] == "auto"
    assert payload["confidence"] == pytest.approx(0.91)


def test_neutral_environment_to_canonical_dict_sets_weather_and_wave_defaults():
    environment = NeutralEnvironment(
        stamp_sec=30,
        stamp_nanosec=900,
        wind_speed_kn=18.0,
        wind_direction_deg=125.0,
        current_speed_kn=2.5,
        current_direction_deg=80.0,
        visibility_range_nm=5.5,
        confidence=0.72,
    )

    payload = neutral_environment_to_canonical_dict(environment)

    assert payload["kind"] == "environment"
    assert payload["schema_version"] == 112
    assert payload["stamp"] == {"sec": 30, "nanosec": 900}
    assert "wind" not in payload
    assert "current" not in payload
    assert "wave" not in payload
    assert payload["wind_speed_kn"] == 18.0
    assert payload["wind_direction_deg"] == 125.0
    assert payload["wave_height_m"] == 0.0
    assert payload["wave_direction_deg"] == 0.0
    assert payload["current_speed_kn"] == 2.5
    assert payload["current_direction_deg"] == 80.0
    assert payload["visibility_range_nm"] == 5.5
    assert payload["weather_source"] == "sensor"
    assert payload["confidence"] == pytest.approx(0.72)


def test_route_points_to_planned_route_dict_builds_stable_geopath_with_segment_speeds():
    points = [
        NeutralRoutePoint(lat=0.0, lon=0.0, speed_kn=10.0),
        NeutralRoutePoint(lat=0.0, lon=1.0, speed_kn=0.0),
        NeutralRoutePoint(lat=1.0, lon=1.0, speed_kn=20.0),
    ]

    payload = route_points_to_planned_route_dict(40, 500, points)
    republished = route_points_to_planned_route_dict(41, 600, points)

    assert payload["kind"] == "route_in"
    assert payload["schema_version"] == 112
    assert payload["stamp"] == {"sec": 40, "nanosec": 500}
    assert payload["route_id"] > 0
    assert payload["route_id"] == republished["route_id"]
    assert payload["route"] == {
        "header": {"stamp": {"sec": 40, "nanosec": 500}, "frame_id": "wgs84"},
        "poses": [
            {"position": {"latitude": 0.0, "longitude": 0.0, "altitude": 0.0}},
            {"position": {"latitude": 0.0, "longitude": 1.0, "altitude": 0.0}},
            {"position": {"latitude": 1.0, "longitude": 1.0, "altitude": 0.0}},
        ],
    }
    assert payload["total_distance_nm"] == pytest.approx(120.0809, rel=1e-4)
    assert payload["estimated_duration_s"] == pytest.approx(2183071.1522, rel=1e-4)
    assert payload["estimated_duration_s"] > 0.0
    assert payload["speed_profile_kn"] == [10.0, 0.0]
    assert payload["safety_zone"] == "default"
    assert payload["confidence"] == pytest.approx(1.0)

    single_point = route_points_to_planned_route_dict(
        42,
        700,
        [NeutralRoutePoint(lat=2.0, lon=3.0, speed_kn=7.0)],
    )
    assert single_point["speed_profile_kn"] == [7.0]


def test_avoidance_plan_to_path_payload_maps_waypoints_without_ros_imports():
    plan = SimpleNamespace(
        header=SimpleNamespace(stamp=SimpleNamespace(sec=77, nanosec=880)),
        confidence=0.86,
        rationale="rule15 starboard alteration",
        waypoints=[
            SimpleNamespace(
                position=SimpleNamespace(latitude=31.11, longitude=121.22),
                target_speed_kn=8.5,
            ),
            SimpleNamespace(
                position=SimpleNamespace(latitude=31.33, longitude=121.44),
                target_speed_kn=9.0,
            ),
        ],
    )

    payload = avoidance_plan_to_path_payload(plan)

    assert payload == {
        "kind": "route_out_path",
        "stamp": {"sec": 77, "nanosec": 880},
        "confidence": 0.86,
        "rationale": "rule15 starboard alteration",
        "points": [
            {"lat": 31.11, "lon": 121.22, "speed_kn": 8.5},
            {"lat": 31.33, "lon": 121.44, "speed_kn": 9.0},
        ],
    }
