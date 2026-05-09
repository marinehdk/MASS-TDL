from dataclasses import dataclass

from common.capability_manifest import CapabilityManifest

_SPEED_TOLERANCE_FACTOR = 1.2


@dataclass
class ValidationResult:
    valid: bool
    reason: str = ""


def validate_track(track_sog_kn: float, manifest: CapabilityManifest | None) -> ValidationResult:
    """Validate track speed-over-ground against vessel capability manifest."""
    if manifest is None:
        raise ValueError("manifest is required for track validation")
    threshold = manifest.max_speed_kn * _SPEED_TOLERANCE_FACTOR
    if track_sog_kn <= threshold:
        return ValidationResult(valid=True)
    return ValidationResult(
        valid=False,
        reason=f"sog {track_sog_kn:.1f} kn exceeds {threshold:.1f} kn limit (manifest.max_speed_kn={manifest.max_speed_kn})",
    )
