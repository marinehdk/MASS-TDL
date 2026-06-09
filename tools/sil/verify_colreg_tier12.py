"""Local verification for the Tier-1/Tier-2 COLREGs scenarios (no A4000 needed).

For each generated file it checks:
  1. Schema-valid against scenarios/fcb_traffic_situation.schema.json (Draft-07),
     the same validation path as sil_orchestrator.scenario_store.ScenarioStore.validate.
  2. The loader (tools.sil.scenario_spec.ScenarioSpec.from_file) parses it.
  3. Encounter geometry: relative bearing of each target re-derived from the
     initial conditions, fed through the REAL M2 classifier
     (l3_tdl_kernel.m2_world_model.encounter_classifier.classify_encounter),
     must equal the intended type; and the analytical straight-line DCPA per
     target must be < 500 m (genuine collision risk).

Exit code 0 only if every file passes every check.

Run:  python -m tools.sil.verify_colreg_tier12   (from repo root)
"""
from __future__ import annotations

import json
import math
import sys
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[2]
SCEN_DIR = ROOT / "scenarios" / "COLREGs测试"
SCHEMA = ROOT / "scenarios" / "fcb_traffic_situation.schema.json"

sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "src" / "l3_tdl_kernel" / "m2_world_model"))

from tools.sil.scenario_spec import ScenarioSpec  # noqa: E402

try:
    from encounter_classifier import classify_encounter  # type: ignore  # noqa: E402
except ImportError:
    sys.path.insert(0, str(ROOT / "src" / "l3_tdl_kernel"))
    from m2_world_model.encounter_classifier import classify_encounter  # type: ignore

KN = 0.5144
DCPA_RISK_M = 500.0

# Expected M2 classification per target (index order matches targetShips[]).
# NOTE: classify_encounter is bearing-only; a slower same-course target dead
# ahead (own overtaking it) classifies as HEAD_ON — documented limitation.
EXPECTED = {
    "colreg-rule14-ho": ["HEAD_ON"],
    "colreg-rule14-ho-port": ["HEAD_ON"],          # 5deg to port, still head-on
    "colreg-rule13-ot": ["HEAD_ON"],               # bearing-only: dead-ahead overtaking -> HEAD_ON
    "colreg-rule15-cs": ["CROSSING_GIVE_WAY"],
    "colreg-rule15-cs-2": ["CROSSING_GIVE_WAY"],
    "colreg-rule15-cs-edge": ["CROSSING_GIVE_WAY"],   # rel_brg 25, just past the head-on edge
    "colreg-rule15-ot-boundary": ["CROSSING_GIVE_WAY"],  # rel_brg 108, just inside the overtaking edge
    "colreg-rule17-cr-so": ["CROSSING_STAND_ON"],
}


def _enu(lat: float, lon: float, lat0: float, lon0: float) -> tuple[float, float]:
    e = (lon - lon0) * 111120.0 * math.cos(math.radians(lat0))
    n = (lat - lat0) * 111120.0
    return e, n


def _rel_bearing(own_e, own_n, own_hdg, tx, ty) -> float:
    abs_brg = math.degrees(math.atan2(tx - own_e, ty - own_n)) % 360.0
    return (abs_brg - own_hdg) % 360.0


def _straight_dcpa(own_e, own_n, own_hdg, own_sog_kn, tx, ty, t_cog, t_sog_kn) -> float:
    rx, ry = tx - own_e, ty - own_n
    ovx = own_sog_kn * KN * math.sin(math.radians(own_hdg))
    ovy = own_sog_kn * KN * math.cos(math.radians(own_hdg))
    tvx = t_sog_kn * KN * math.sin(math.radians(t_cog))
    tvy = t_sog_kn * KN * math.cos(math.radians(t_cog))
    vrx, vry = tvx - ovx, tvy - ovy
    vv = vrx * vrx + vry * vry
    if vv < 1e-9:
        return math.hypot(rx, ry)
    t_star = -(rx * vrx + ry * vry) / vv
    if t_star < 0:
        return math.hypot(rx, ry)
    return math.hypot(rx + vrx * t_star, ry + vry * t_star)


def _validate_schema(doc: dict) -> list[str]:
    import jsonschema
    schema = json.loads(SCHEMA.read_text())
    v = jsonschema.Draft7Validator(schema)
    out = []
    for err in v.iter_errors(doc):
        loc = ".".join(str(p) for p in err.absolute_path) or "(root)"
        out.append(f"{loc}: {err.message}")
    return out


def main() -> int:
    failures = 0
    for stem, expected in EXPECTED.items():
        path = SCEN_DIR / f"{stem}.yaml"
        print(f"\n=== {stem} ===")
        if not path.exists():
            print("  FAIL: file missing")
            failures += 1
            continue
        doc = yaml.safe_load(path.read_text())

        # 1. schema
        errs = _validate_schema(doc)
        print(f"  [1] schema: {'OK' if not errs else 'FAIL'}")
        if errs:
            for e in errs:
                print(f"        - {e}")
            failures += 1

        # 2. loader
        try:
            spec = ScenarioSpec.from_file(path)
            print(f"  [2] loader: OK ({len(spec.initial_conditions.targets)} target(s))")
        except Exception as exc:  # noqa: BLE001
            print(f"  [2] loader: FAIL ({exc})")
            failures += 1

        # 3. geometry + classification + DCPA
        sim = doc["metadata"]["simulation_settings"]
        lat0, lon0 = sim.get("coordinate_origin", [63.44, 10.38])
        own_init = doc["ownShip"]["initial"]
        own_hdg = float(own_init["heading"])
        own_sog = float(own_init["sog"])
        oe, on = _enu(own_init["position"]["latitude"], own_init["position"]["longitude"], lat0, lon0)
        targets = doc["targetShips"]
        if len(targets) != len(expected):
            print(f"  [3] FAIL: {len(targets)} targets, expected {len(expected)}")
            failures += 1
            continue
        for i, (ts, exp) in enumerate(zip(targets, expected)):
            ti = ts["initial"]
            te, tn = _enu(ti["position"]["latitude"], ti["position"]["longitude"], lat0, lon0)
            relb = _rel_bearing(oe, on, own_hdg, te, tn)
            cls = classify_encounter(own_hdg, relb).value
            dcpa = _straight_dcpa(oe, on, own_hdg, own_sog, te, tn,
                                  float(ti["cog"]), float(ti["sog"]))
            cls_ok = cls == exp
            dcpa_ok = dcpa < DCPA_RISK_M
            tag = "OK" if (cls_ok and dcpa_ok) else "FAIL"
            print(f"  [3] ts{i+1}: rel_brg={relb:6.1f}  class={cls:18s} "
                  f"(want {exp:18s}) dcpa={dcpa:6.1f} m  -> {tag}")
            if not (cls_ok and dcpa_ok):
                failures += 1

    print(f"\n{'ALL PASS' if failures == 0 else f'{failures} CHECK(S) FAILED'}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
