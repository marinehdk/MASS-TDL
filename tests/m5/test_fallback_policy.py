"""
Tests for M5 fallback policy handlers (MUST-5 & MUST-9).

MUST-5: FM-4 fallback must use manifest.rot_max(speed_kn), not hardcoded 8°/s.
MUST-9: FM-2 fallback must emit safety_concern_event to M7, not MRM directly.
"""

import pytest

from common.capability_manifest import CapabilityManifest, HullClass
from m5_tactical_planner.fallback_policy import (
    SafetyConcernEvent,
    fm2_handle_collision_imminent,
    fm4_handle_odd_breach,
    fm4_rot_limit,
)


@pytest.fixture
def fcb_manifest() -> CapabilityManifest:
    """Fixture: FCB-like manifest with ROT curve."""
    return CapabilityManifest(
        max_speed_kn=22.0,
        rot_max_curve=[(0.0, 10.0), (20.0, 4.0)],
        length_m=45.0,
        hull_class=HullClass.SEMI_PLANING,
    )


# === FM-4 tests (MUST-5) ===


def test_fm4_rot_limit_uses_manifest(fcb_manifest: CapabilityManifest):
    """FM-4: rot_limit must come from manifest.rot_max(speed_kn)."""
    # At speed 10.0, linear interpolation should give:
    # t = (10.0 - 0.0) / (20.0 - 0.0) = 0.5
    # rot = 10.0 + 0.5 * (4.0 - 10.0) = 10.0 - 3.0 = 7.0
    rot_limit = fm4_rot_limit(speed_kn=10.0, manifest=fcb_manifest)
    assert rot_limit == 7.0


def test_fm4_rot_limit_at_zero_speed(fcb_manifest: CapabilityManifest):
    """FM-4: at zero speed, should return first curve point."""
    rot_limit = fm4_rot_limit(speed_kn=0.0, manifest=fcb_manifest)
    assert rot_limit == 10.0


def test_fm4_rot_limit_at_max_speed(fcb_manifest: CapabilityManifest):
    """FM-4: at max speed, should return last curve point."""
    rot_limit = fm4_rot_limit(speed_kn=20.0, manifest=fcb_manifest)
    assert rot_limit == 4.0


def test_fm4_emits_safety_concern_not_mrm(fcb_manifest: CapabilityManifest):
    """FM-4: must emit safety_concern_event, never MRM directly."""
    event = fm4_handle_odd_breach(speed_kn=5.0, manifest=fcb_manifest)

    # Verify it's a SafetyConcernEvent
    assert isinstance(event, SafetyConcernEvent)

    # Verify event_type is "safety_concern_event"
    assert event.event_type == "safety_concern_event"

    # Verify reason mentions FM-4 or ODD
    assert "FM-4" in event.reason or "ODD" in event.reason

    # Verify rot_limit is present and valid
    assert event.rot_limit_deg_per_s is not None
    assert event.rot_limit_deg_per_s > 0


# === FM-2 tests (MUST-9) ===


def test_fm2_emits_safety_concern_not_mrm():
    """FM-2: must emit safety_concern_event, never MRM directly."""
    event = fm2_handle_collision_imminent()

    # Verify it's a SafetyConcernEvent
    assert isinstance(event, SafetyConcernEvent)

    # Verify event_type is "safety_concern_event"
    assert event.event_type == "safety_concern_event"

    # Verify reason mentions FM-2 or collision
    assert "FM-2" in event.reason or "collision" in event.reason


def test_fm2_does_not_emit_mrm_command():
    """FM-2: must not emit mrm_command; M7 is sole MRM emitter."""
    event = fm2_handle_collision_imminent()

    # Verify event_type is NOT "mrm_command"
    assert event.event_type != "mrm_command"

    # Verify no "MRM" or "mrm" in event_type
    assert "MRM" not in event.event_type
    assert "mrm" not in event.event_type.lower()


# === Prohibition tests ===


def test_no_hardcoded_rot_literal():
    """Prohibition: no ROT_max = 8 or other hardcoded literals in fallback_policy.py."""
    import re
    from pathlib import Path

    # Read the fallback_policy.py file (relative to test location, respecting new src/ layout)
    test_dir = Path(__file__).parent.parent.parent  # tests/ -> tests -> Code -> MASS-L3-Tactical Layer
    fallback_policy_path = test_dir / "src" / "l3_tdl_kernel" / "m5_tactical_planner" / "fallback_policy.py"
    with open(fallback_policy_path) as f:
        content = f.read()

    # Check for hardcoded ROT_max = <digit>
    assert not re.search(
        r"ROT_max\s*=\s*\d", content
    ), "Found hardcoded ROT_max = literal in fallback_policy.py"

    # Check for hardcoded 8.0 or 8 in rot/ROT context
    lines = content.split("\n")
    for line in lines:
        if "rot" in line.lower() or "ROT" in line:
            assert not re.search(
                r"\b8\.0\b", line
            ), f"Found hardcoded 8.0 in line: {line}"
