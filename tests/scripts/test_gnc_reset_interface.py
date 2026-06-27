"""GNC reset interface: orchestrator emits ShipReset on configure.

Tests the pure helper _build_ship_reset that extracts lat/lon/heading/sog
from scenario ownShip.initial. The orchestrator (lifecycle_bridge) imports
sil_msgs at module load; when sil_msgs is unavailable (non-ROS host), the
test patches the ShipReset construction so the pure-extraction logic is
still covered.
"""
import pytest


def _load_lifecycle_bridge():
    import importlib
    import sys
    sys.path.insert(0, ".")
    try:
        mod = importlib.import_module("src.sil_orchestrator.lifecycle_bridge")
        return mod
    except Exception:
        pytest.skip("lifecycle_bridge import requires ROS/sil_msgs environment")


def test_build_ship_reset_from_ownship_initial():
    """_build_ship_reset extracts lat/lon/heading/sog from scenario ownShip.initial."""
    mod = _load_lifecycle_bridge()
    scen = {
        "ownShip": {
            "initial": {
                "position": {"latitude": 63.44, "longitude": 10.38},
                "heading": 0.0,
                "sog": 6.0,
            }
        }
    }
    msg = mod._build_ship_reset(scen)
    assert msg is not None
    assert msg.latitude == pytest.approx(63.44)
    assert msg.longitude == pytest.approx(10.38)
    assert msg.heading_deg == pytest.approx(0.0)
    assert msg.sog_kn == pytest.approx(6.0)


def test_build_ship_reset_none_when_no_ownship():
    """Missing ownShip.initial returns None (no reset emitted)."""
    mod = _load_lifecycle_bridge()
    msg = mod._build_ship_reset({})
    assert msg is None


def test_build_ship_reset_none_when_no_position():
    """ownShip.initial without position returns None."""
    mod = _load_lifecycle_bridge()
    msg = mod._build_ship_reset({"ownShip": {"initial": {"heading": 0.0}}})
    assert msg is None


def test_build_ship_reset_defaults_missing_heading_sog():
    """Missing heading/sog default to 0.0 (position is the required field)."""
    mod = _load_lifecycle_bridge()
    msg = mod._build_ship_reset({
        "ownShip": {"initial": {"position": {"latitude": 1.0, "longitude": 2.0}}}
    })
    assert msg is not None
    assert msg.heading_deg == pytest.approx(0.0)
    assert msg.sog_kn == pytest.approx(0.0)
