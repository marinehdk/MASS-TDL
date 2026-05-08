import pytest

from common.capability_manifest import CapabilityManifest, HullClass
from m2_world_model.track_validator import validate_track

_MANIFEST = CapabilityManifest(
    max_speed_kn=22.0,
    rot_max_curve=[(0.0, 8.0), (10.0, 6.0), (22.0, 3.0)],
    length_m=45.0,
    hull_class=HullClass.SEMI_PLANING,
)


def test_sog_within_limit_is_valid():
    """20 kn <= 22 * 1.2 = 26.4 kn — valid."""
    result = validate_track(20.0, _MANIFEST)
    assert result.valid is True


def test_sog_at_exact_limit_is_valid():
    """26.4 kn == 22 * 1.2 — exactly at boundary, valid."""
    result = validate_track(26.4, _MANIFEST)
    assert result.valid is True


def test_sog_exceeds_limit_is_invalid():
    """27 kn > 26.4 kn — invalid."""
    result = validate_track(27.0, _MANIFEST)
    assert result.valid is False
    assert "26.4" in result.reason


def test_no_manifest_raises_value_error():
    with pytest.raises(ValueError, match="manifest"):
        validate_track(20.0, None)
