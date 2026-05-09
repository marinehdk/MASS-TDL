"""Verify bearing sectors for all 10 YAML scenario files (D1.3b acceptance criteria T4a/b/c)."""
import math
import sys
from pathlib import Path

import yaml

sys.path.insert(0, str(Path(__file__).parent))
from scenario_spec import ScenarioSpec


# COLREGs bearing sectors (target bearing from own ship, CW from North, deg)
RULE14_SECTOR = (345.0, 15.0)    # wraps through 0
RULE15_STBD_SECTOR = (15.0, 112.5)
RULE15_PORT_SECTOR = (247.5, 345.0)
# Rule 13: own ship must be in 112.5°–247.5° sector FROM TARGET'S perspective
RULE13_SECTOR_FROM_TARGET = (112.5, 247.5)


def bearing_deg(from_x: float, from_y: float, to_x: float, to_y: float) -> float:
    """Nautical bearing CW from North (deg) from point A to point B in ENU."""
    dx = to_x - from_x   # East
    dy = to_y - from_y   # North
    bearing = math.degrees(math.atan2(dx, dy)) % 360.0
    return bearing


def in_sector(bearing: float, start: float, end: float) -> bool:
    """True if bearing is in [start, end] sector (handles wrap-around through 0)."""
    if start <= end:
        return start <= bearing <= end
    else:  # wraps through 0
        return bearing >= start or bearing <= end


SCENARIO_RULES = {
    "colreg-rule14-ho-001-seed42-v1.0": ("Rule14", RULE14_SECTOR, False),
    "colreg-rule14-ho-002-seed43-v1.0": ("Rule14", RULE14_SECTOR, False),
    "colreg-rule14-ho-003-seed44-v1.0": ("Rule14", RULE14_SECTOR, False),
    "colreg-rule15-cs-001-seed10-v1.0": ("Rule15_Stbd", RULE15_STBD_SECTOR, False),
    "colreg-rule15-cs-002-seed11-v1.0": ("Rule15_Stbd", RULE15_STBD_SECTOR, False),
    "colreg-rule15-cs-003-seed12-v1.0": ("Rule15_Port", RULE15_PORT_SECTOR, False),
    "colreg-rule15-cs-004-seed13-v1.0": ("Rule15_Stbd", RULE15_STBD_SECTOR, False),
    "colreg-rule13-ot-001-seed20-v1.0": ("Rule13", RULE13_SECTOR_FROM_TARGET, True),
    "colreg-rule13-ot-002-seed21-v1.0": ("Rule13", RULE13_SECTOR_FROM_TARGET, True),
    "colreg-rule13-ot-003-seed22-v1.0": ("Rule13", RULE13_SECTOR_FROM_TARGET, True),
}


def verify_all(scenarios_dir: Path) -> bool:
    all_ok = True
    for yaml_path in sorted(scenarios_dir.glob("*.yaml")):
        if yaml_path.name == "schema.yaml":
            continue
        data = yaml.safe_load(yaml_path.read_text())
        spec = ScenarioSpec.model_validate(data)
        sid = yaml_path.stem  # filename without .yaml

        if sid not in SCENARIO_RULES:
            print(f"  SKIP {spec.scenario_id} (not in expected list)")
            continue

        rule, (sector_start, sector_end), from_target = SCENARIO_RULES[sid]
        own = spec.initial_conditions.own_ship
        tgt = spec.initial_conditions.targets[0]

        if from_target:
            # Rule 13: calculate bearing from target to own ship
            b = bearing_deg(tgt.x_m, tgt.y_m, own.x_m, own.y_m)
            label = "own-from-target bearing"
        else:
            # Rule 14/15: calculate bearing from own ship to target
            b = bearing_deg(own.x_m, own.y_m, tgt.x_m, tgt.y_m)
            label = "target bearing from own"

        ok = in_sector(b, sector_start, sector_end)
        status = "OK  " if ok else "FAIL"
        print(f"  {status} {sid}: {label}={b:.1f}° expected in [{sector_start},{sector_end}]")
        if not ok:
            all_ok = False

    return all_ok


if __name__ == "__main__":
    scenarios_dir = Path(__file__).parent.parent.parent / "scenarios" / "colregs"
    print(f"Verifying sectors in: {scenarios_dir}")
    ok = verify_all(scenarios_dir)
    sys.exit(0 if ok else 1)
