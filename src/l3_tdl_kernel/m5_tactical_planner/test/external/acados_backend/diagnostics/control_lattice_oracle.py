#!/usr/bin/env python3
"""Independent finite control-lattice feasibility witness for frozen Phase 0 cases.

This oracle does not call acados, HPIPM, CasADi, or production solver code.  It
uses a separately implemented deterministic rollout and reports only a positive
witness.  Exhausting the finite grid is OPEN, never proof of infeasibility.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import pathlib
from typing import Any


N = 80
DT = 15.0
C_U = 9.825342e-3
K_PROP_PER_MASS = 500.0 / 650000.0
K_DRAG_PER_MASS = 100.0 / 650000.0
DELTA_MAX = 0.4
N_MAX = 12.0


def write_json(path: pathlib.Path, value: Any) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n", encoding="utf-8")


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def target_xy(target: dict[str, Any], t_s: float) -> tuple[float, float]:
    return (target["x_m"] + target["sog_mps"] * math.cos(target["cog_rad"]) * t_s,
            target["y_m"] + target["sog_mps"] * math.sin(target["cog_rad"]) * t_s)


def rollout(case: dict[str, Any], amplitude: float, turn_steps: int) -> tuple[list[list[float]], list[list[float]]]:
    own = case["own_ship"]
    state = [own["x_m"], own["y_m"], own["psi_rad"], 0.0, own["u_mps"]]
    n_hold = min(N_MAX, math.sqrt(K_DRAG_PER_MASS / K_PROP_PER_MASS) * own["u_mps"])
    xs: list[list[float]] = []
    us: list[list[float]] = []
    for k in range(N + 1):
        xs.append(state.copy())
        if k == N:
            break
        delta = amplitude if k < turn_steps else -amplitude if k < 2 * turn_steps else 0.0
        us.append([delta, n_hold])
        x, y, psi, r, surge = state
        state = [
            x + surge * DT * math.cos(psi),
            y + surge * DT * math.sin(psi),
            psi + DT * r,
            r + DT * C_U * delta,
            surge + DT * (K_PROP_PER_MASS * n_hold * n_hold - K_DRAG_PER_MASS * surge * surge),
        ]
    return xs, us


def first_reachable_min_alt_stage(case: dict[str, Any]) -> int | None:
    minimum = abs(float(case["colregs"]["min_alteration_rad"]))
    if minimum <= 0.0:
        return None
    # Independent maximum-control reachability bound under the same stated
    # discrete plant: psi_{k+1}=psi_k+DT*r_k, r_{k+1}=r_k+DT*C_U*delta_k.
    psi = float(case["own_ship"]["psi_rad"])
    r = 0.0
    for k in range(1, N + 1):
        psi = psi + DT * r
        r = r + DT * C_U * DELTA_MAX
        if abs(psi - case["own_ship"]["psi_rad"]) + 1.0e-12 >= minimum:
            return k
    return None


def audit(case: dict[str, Any], xs: list[list[float]], us: list[list[float]]) -> dict[str, Any]:
    c = case["constraints"]
    pref = int(case["colregs"]["preferred_direction"])
    min_alt = abs(float(case["colregs"]["min_alteration_rad"]))
    min_alt_start = first_reachable_min_alt_stage(case)
    rows: list[dict[str, Any]] = []
    min_sep = float("inf")
    violations: list[dict[str, Any]] = []
    nx, ny = case["route"]["frame_normal_x"], case["route"]["frame_normal_y"]
    ox, oy = case["route"]["frame_origin_x_m"], case["route"]["frame_origin_y_m"]
    for k, state in enumerate(xs):
        x, y, psi, r, surge = state
        sep = float("inf")
        for target in case["targets"]:
            tx, ty = target_xy(target, k * DT)
            sep = min(sep, math.hypot(x - tx, y - ty))
        min_sep = min(min_sep, sep)
        lateral = (x - ox) * nx + (y - oy) * ny
        state_ok = (c["heading_min_rad"] - 1e-10 <= psi <= c["heading_max_rad"] + 1e-10 and
                    abs(r) <= c["rot_max_rad_s"] + 1e-10 and
                    c["speed_min_mps"] - 1e-10 <= surge <= c["speed_max_mps"] + 1e-10)
        cpa_ok = (not case["targets"]) or sep + 1e-10 >= c["cpa_safe_m"]
        direction_ok = pref == 0 or pref * lateral >= -1e-10
        min_alt_ok = min_alt_start is None or k < min_alt_start or pref * (psi - c["own_ship_psi_rad"]) >= min_alt - 1e-10
        rows.append({"stage": k, "t_s": k * DT, "state": state, "min_separation_m": None if math.isinf(sep) else sep,
                     "lateral_m": lateral, "state_box_ok": state_ok, "cpa_ok": cpa_ok,
                     "direction_ok": direction_ok, "reachable_min_alt_ok": min_alt_ok})
        if not all((state_ok, cpa_ok, direction_ok, min_alt_ok)):
            violations.append(rows[-1])
    controls_ok = all(abs(row[0]) <= DELTA_MAX + 1e-12 and 0.0 <= row[1] <= N_MAX + 1e-12 for row in us)
    return {"feasible": controls_ok and not violations, "controls_ok": controls_ok,
            "first_reachable_min_alt_stage": min_alt_start,
            "minimum_separation_m": None if math.isinf(min_sep) else min_sep,
            "violations": violations, "rows": rows}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--case", type=pathlib.Path, required=True)
    parser.add_argument("--acados-verdict", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    case = json.loads(args.case.read_text(encoding="utf-8"))
    acados = json.loads(args.acados_verdict.read_text(encoding="utf-8"))

    witness = None
    attempts = []
    for turn_steps in range(1, 5):
        for index in range(0, 41):
            amplitude = index / 100.0
            xs, us = rollout(case, amplitude, turn_steps)
            result = audit(case, xs, us)
            attempts.append({"turn_steps": turn_steps, "amplitude_rad": amplitude,
                             "feasible": result["feasible"], "minimum_separation_m": result["minimum_separation_m"]})
            if result["feasible"]:
                witness = {"turn_steps": turn_steps, "amplitude_rad": amplitude,
                           "states": xs, "controls": us, "audit": result}
                break
        if witness is not None:
            break

    raw_counts = acados.get("cold", {}).get("raw_status_counts", {})
    acados_success = raw_counts and set(raw_counts) == {"0"}
    if witness is not None:
        reference = "SIMPLIFIED_MODEL_FEASIBLE_SANITY_ONLY"
        classification = "OPEN: witness is not an independent MMG/GNC reference oracle"
    else:
        reference = "REFERENCE_FEASIBILITY_OPEN"
        classification = "OPEN: finite lattice has no witness and cannot prove infeasibility"

    min_alt_start = first_reachable_min_alt_stage(case)
    production_stage1_conflict = bool(min_alt_start is not None and min_alt_start > 1)
    oracle = {
        "case_id": case["case_id"], "input_sha256": sha256(args.case),
        "independence": "No acados, HPIPM, CasADi, objective, Hessian, or solver seed is used; however, coefficients and 15 s discrete plant intentionally mirror the production surrogate, so this is not an independent physical oracle.",
        "model": {"N": N, "dt_s": DT, "integrator": "separately implemented explicit discrete rollout",
                  "dynamics": "x+=u*dt*cos(psi); y+=u*dt*sin(psi); psi+=dt*r; r+=dt*C_U*delta; surge quadratic prop-drag",
                  "C_U": C_U, "grid": {"turn_steps": [1, 2, 3, 4], "amplitude_rad": "0.00:0.01:0.40",
                  "pattern": "+amplitude for m stages, -amplitude for m stages, then zero"}},
        "limitations": ["CPA sampled only at 15 s nodes", "no rudder slew", "no disturbance/uncertainty", "not independent MMG", "min-alt checked from an internally derived reachable stage"],
        "positive_witness_policy": "A witness is simplified-model sanity only; it does not close reference feasibility. Grid exhaustion also remains OPEN.",
        "reference_status": reference, "four_class_verdict": classification,
        "acados_cold_raw_status_counts": raw_counts,
        "desired_contract_reachability": {"first_reachable_min_alt_stage": min_alt_start,
                                           "production_stage1_min_alt_conflict": production_stage1_conflict,
                                           "note": "Physical/desired-contract feasibility is separate from current NLP bound consistency."},
        "attempt_count": len(attempts), "attempts": attempts,
    }
    write_json(args.output / "reference_oracle.json", oracle)
    write_json(args.output / "reference_witness.json", witness if witness is not None else {"status": "NO_WITNESS_IN_FINITE_GRID"})
    write_json(args.output / "gnc_executability.json", {
        "status": "OPEN", "solver_box_and_reference_dynamics": "PASS" if witness is not None else "OPEN",
        "reason": "Independent MMG/L4 exact execution, route return, disturbances, and uncertainty are outside this lattice oracle.",
        "independent_mmg_sanity_from_sil_reviewer": {
            "rule14_5000": {"min_separation_m": 2091.668, "time_of_min_s": 832.2,
                            "terminal_heading_deg": 51.638, "max_rot_deg_s": 0.782,
                            "speed_range_mps": [2.750, 3.001]},
            "target2500": {"minimum_separation_m": 2103.6, "max_rot_deg_s": 1.32},
            "scope": "sanity only; not a GNC/route-return/uncertainty acceptance oracle"
        }
    })
    print(json.dumps({"case": case["case_id"], "reference": reference,
                      "classification": classification,
                      "witness": None if witness is None else {"turn_steps": witness["turn_steps"],
                                                               "amplitude_rad": witness["amplitude_rad"],
                                                               "minimum_separation_m": witness["audit"]["minimum_separation_m"]}}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
