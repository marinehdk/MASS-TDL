"""
M5 Tactical Planner Fallback Policy Handlers (MUST-5 & MUST-9).

MUST-5: FM-4 (ODD breach) must query manifest.rot_max(speed_kn); no hardcoded ROT literals.
MUST-9: FM-2 (collision imminent) must emit safety_concern_event to M7; M7 is sole MRM emitter.
"""

from __future__ import annotations

from dataclasses import dataclass

from common.capability_manifest import CapabilityManifest


@dataclass(frozen=True)
class SafetyConcernEvent:
    """Safety concern event emitted by M5 (Doer) to M7 (Checker)."""

    event_type: str
    reason: str
    rot_limit_deg_per_s: float | None = None


def fm4_rot_limit(speed_kn: float, manifest: CapabilityManifest) -> float:
    """
    MUST-5: Return ROT limit (deg/s) for current speed from the manifest curve.

    Args:
        speed_kn: Current vessel speed in knots.
        manifest: CapabilityManifest containing rot_max_curve.

    Returns:
        ROT maximum in degrees per second, interpolated from manifest.
    """
    return manifest.rot_max(speed_kn)


def fm4_handle_odd_breach(
    speed_kn: float, manifest: CapabilityManifest
) -> SafetyConcernEvent:
    """
    MUST-5: FM-4 fallback handler — ODD (Operational Design Domain) breach detected.

    Emits a safety_concern_event to M7 (Checker). M5 does not emit MRM directly.
    ROT limit is queried from the capability manifest, not hardcoded.

    Args:
        speed_kn: Current vessel speed in knots.
        manifest: CapabilityManifest with vessel-specific ROT curve.

    Returns:
        SafetyConcernEvent to be consumed by M7.
    """
    rot_limit = fm4_rot_limit(speed_kn, manifest)
    return SafetyConcernEvent(
        event_type="safety_concern_event",
        reason="FM-4: ODD breach — awaiting MRM from M7",
        rot_limit_deg_per_s=rot_limit,
    )


def fm2_handle_collision_imminent() -> SafetyConcernEvent:
    """
    MUST-9: FM-2 fallback handler — collision imminent scenario.

    Emits a safety_concern_event to M7 (Checker). M5 (Doer) never emits MRM directly.
    M7 (Checker) is the sole authority for initiating MRM (Maneuvering Response Module).

    Returns:
        SafetyConcernEvent to be consumed by M7.
    """
    return SafetyConcernEvent(
        event_type="safety_concern_event",
        reason="FM-2: collision_imminent — M7 to initiate MRM-01",
    )
