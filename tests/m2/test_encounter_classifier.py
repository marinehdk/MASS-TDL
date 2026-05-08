from m2_world_model.encounter_classifier import Encounter, classify_encounter

# ── 4 boundary tests ──────────────────────────────────────────────────────────

def test_boundary_112_5_is_overtaking():
    """112.5° is the exact port-side boundary of OVERTAKING — inclusive."""
    assert classify_encounter(0.0, 112.5) == Encounter.OVERTAKING


def test_boundary_247_5_is_overtaking():
    """247.5° is the exact starboard-side boundary of OVERTAKING — inclusive."""
    assert classify_encounter(0.0, 247.5) == Encounter.OVERTAKING


def test_boundary_22_5_is_head_on():
    """22.5° is the exact starboard boundary of HEAD_ON — inclusive."""
    assert classify_encounter(0.0, 22.5) == Encounter.HEAD_ON


def test_boundary_337_5_is_head_on():
    """337.5° is the exact port boundary of HEAD_ON — inclusive."""
    assert classify_encounter(0.0, 337.5) == Encounter.HEAD_ON


# ── 4 type tests (mid-sector) ─────────────────────────────────────────────────

def test_type_head_on_zero_degrees():
    """0° directly ahead is HEAD_ON."""
    assert classify_encounter(0.0, 0.0) == Encounter.HEAD_ON


def test_type_overtaking_180_degrees():
    """180° directly astern is OVERTAKING."""
    assert classify_encounter(0.0, 180.0) == Encounter.OVERTAKING


def test_type_crossing_give_way_starboard():
    """45° (22.5°,112.5°) — target on starboard bow, own vessel must give way."""
    assert classify_encounter(0.0, 45.0) == Encounter.CROSSING_GIVE_WAY


def test_type_crossing_stand_on_port():
    """300° (247.5°,337.5°) — target on port bow, own vessel stands on."""
    assert classify_encounter(0.0, 300.0) == Encounter.CROSSING_STAND_ON
