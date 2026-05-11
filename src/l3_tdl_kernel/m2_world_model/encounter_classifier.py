from enum import Enum


class Encounter(Enum):
    HEAD_ON = "HEAD_ON"
    OVERTAKING = "OVERTAKING"
    CROSSING_GIVE_WAY = "CROSSING_GIVE_WAY"
    CROSSING_STAND_ON = "CROSSING_STAND_ON"
    OUT_OF_ENCOUNTER = "OUT_OF_ENCOUNTER"


def classify_encounter(own_hdg_deg: float, target_rel_brg_deg: float) -> Encounter:
    """
    Classify COLREGs encounter type.

    Args:
        own_hdg_deg: Own vessel heading in degrees [0, 360).
        target_rel_brg_deg: Target bearing relative to own heading [0, 360).

    Sector definitions (COLREGs Rules 13, 14, 15):
        HEAD_ON:            [337.5°, 360°) ∪ [0°, 22.5°]   (±22.5° from bow)
        CROSSING_GIVE_WAY:  (22.5°, 112.5°)                (starboard bow)
        OVERTAKING:         [112.5°, 247.5°]                (±67.5° from stern)
        CROSSING_STAND_ON:  (247.5°, 337.5°)                (port bow)
    """
    brg = target_rel_brg_deg % 360.0

    if brg <= 22.5 or brg >= 337.5:
        return Encounter.HEAD_ON
    elif 112.5 <= brg <= 247.5:
        return Encounter.OVERTAKING
    elif 22.5 < brg < 112.5:
        return Encounter.CROSSING_GIVE_WAY
    elif 247.5 < brg < 337.5:
        return Encounter.CROSSING_STAND_ON
    else:
        return Encounter.OUT_OF_ENCOUNTER
