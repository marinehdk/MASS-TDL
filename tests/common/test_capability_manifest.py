import pytest
from pydantic import ValidationError
from common.capability_manifest import CapabilityManifest, HullClass


def test_max_speed_required():
    """max_speed_kn is required — missing it raises ValidationError."""
    with pytest.raises(ValidationError):
        CapabilityManifest(
            rot_max_curve=[(0.0, 8.0), (22.0, 3.0)],
            length_m=45.0,
            hull_class=HullClass.SEMI_PLANING,
        )


def test_rot_max_curve_must_be_monotone_decreasing():
    """rot_max_curve must decrease as speed increases."""
    with pytest.raises(ValidationError):
        CapabilityManifest(
            max_speed_kn=22.0,
            rot_max_curve=[(0.0, 3.0), (22.0, 8.0)],  # increasing — invalid
            length_m=45.0,
            hull_class=HullClass.SEMI_PLANING,
        )


def test_hull_class_enum_rejects_unknown_string():
    with pytest.raises(ValidationError):
        CapabilityManifest(
            max_speed_kn=22.0,
            rot_max_curve=[(0.0, 8.0), (22.0, 3.0)],
            length_m=45.0,
            hull_class="SUBMARINE",  # not in enum
        )


def test_valid_manifest_creates_successfully():
    m = CapabilityManifest(
        max_speed_kn=22.0,
        rot_max_curve=[(0.0, 8.0), (10.0, 6.0), (22.0, 3.0)],
        length_m=45.0,
        hull_class=HullClass.SEMI_PLANING,
    )
    assert m.max_speed_kn == 22.0
    assert m.hull_class == HullClass.SEMI_PLANING


def test_json_schema_export_has_required_fields():
    schema = CapabilityManifest.model_json_schema()
    assert "properties" in schema
    required_fields = {"max_speed_kn", "rot_max_curve", "length_m", "hull_class"}
    assert required_fields.issubset(schema["properties"].keys())


def test_model_roundtrip():
    m = CapabilityManifest(
        max_speed_kn=22.0,
        rot_max_curve=[(0.0, 8.0), (10.0, 6.0), (22.0, 3.0)],
        length_m=45.0,
        hull_class=HullClass.SEMI_PLANING,
    )
    data = m.model_dump()
    m2 = CapabilityManifest.model_validate(data)
    assert m2.max_speed_kn == m.max_speed_kn
    assert m2.hull_class == m.hull_class
    assert m2.rot_max_curve == m.rot_max_curve
