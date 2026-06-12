from pathlib import Path

import pytest

from sil_orchestrator.integration.profiles import (
    AdapterState,
    IntegrationProfileError,
    load_profile,
    load_profiles,
)


PROFILE_DIR = Path(__file__).resolve().parents[2] / "config" / "integration_profiles"


def test_load_default_profile_keeps_external_disabled():
    profile = load_profile(PROFILE_DIR / "default.yaml")

    assert profile.name == "default"
    assert profile.mode == "default"
    assert profile.tdl_domain_id == 42
    assert profile.external_domains == {}
    assert {
        name: adapter.state for name, adapter in profile.adapters.items()
    } == {
        "simulation": AdapterState.DISABLED,
        "yougc": AdapterState.DISABLED,
        "route_level_closed_loop": AdapterState.DISABLED,
    }
    assert profile.freshness.ownship_ms == 500
    assert profile.freshness.targets_ms == 2000
    assert profile.freshness.environment_ms == 10000
    assert profile.safety.route_out_requires_screen02_pass is True
    assert profile.safety.forbid_low_level_control is True


def test_load_a4000_profile_enables_route_level_closed_loop():
    profile = load_profile(PROFILE_DIR / "a4000_external.yaml")

    assert profile.name == "a4000_external"
    assert profile.mode == "external"
    assert profile.tdl_domain_id == 42
    assert profile.adapters["simulation"].state is AdapterState.ENABLED
    assert profile.adapters["yougc"].state is AdapterState.ENABLED
    assert profile.adapters["route_level_closed_loop"].state is AdapterState.ENABLED

    simulation = profile.external_domains["simulation"]
    assert simulation.domain_id == 10
    assert simulation.setup == "/home/mass/simulation/船舶动力学/gnc_ws/install/setup.bash"
    assert simulation.required_topics == {
        "/route_planning/route_plan": "ship_interfaces/msg/RoutePlan",
        "/ship/waypoints": "nav_msgs/msg/Path",
        "/ship/odometry": "nav_msgs/msg/Odometry",
    }

    yougc = profile.external_domains["yougc"]
    assert yougc.domain_id == 11
    assert yougc.setup == "/home/mass/yougc/ros2_ws/install/setup.bash"
    assert yougc.required_topics == {
        "/fusion/tracked_targets": "nmea_interfaces/msg/TrackedTargetArray",
        "/gps/fix": "nmea_interfaces/msg/Gps",
        "/heading": "nmea_interfaces/msg/Heading",
    }
    assert profile.safety.route_out_requires_screen02_pass is True
    assert profile.safety.forbid_low_level_control is True


def test_load_profiles_returns_name_keyed_profiles():
    profiles = load_profiles(PROFILE_DIR)

    assert set(profiles) == {"default", "a4000_external"}
    assert profiles["default"].name == "default"
    assert profiles["a4000_external"].mode == "external"


def test_rejects_external_profile_without_domain(tmp_path):
    profile_path = tmp_path / "missing_domains.yaml"
    profile_path.write_text(
        """
name: broken
mode: external
tdl_domain_id: 42
adapters:
  simulation: enabled
freshness:
  ownship_ms: 500
  targets_ms: 2000
  environment_ms: 10000
safety:
  route_out_requires_screen02_pass: true
  forbid_low_level_control: true
""",
        encoding="utf-8",
    )

    with pytest.raises(IntegrationProfileError, match="external_domains"):
        load_profile(profile_path)
