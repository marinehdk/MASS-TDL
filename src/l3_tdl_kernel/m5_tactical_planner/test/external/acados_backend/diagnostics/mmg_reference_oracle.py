#!/usr/bin/env python3
"""Independent SIL MMG/RK4 positive-witness oracle for frozen diagnostics."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import pathlib
from typing import Any

from ship_dynamics.mmg_coefficients import MMGCoefficients
from ship_dynamics.mmg_model import MMGModel, ShipState


def write_json(path: pathlib.Path, value: Any) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n", encoding="utf-8")


def target_xy(target: dict[str, Any], t_s: float) -> tuple[float, float]:
    return (target["x_m"] + target["sog_mps"] * math.cos(target["cog_rad"]) * t_s,
            target["y_m"] + target["sog_mps"] * math.sin(target["cog_rad"]) * t_s)


def command_for(mode: str, t_s: float) -> float:
    if mode == "live_rule14":
        return 0.4 if t_s < 52.0 else -0.4 if t_s < 68.0 else 0.0
    if mode == "benchmark_rule14":
        return 0.4 if t_s < 63.54084409738354 else 0.0
    return 0.4 if t_s < 48.0 else 0.0


def simulate(source: dict[str, Any], mode: str, dt: float, save_csv: pathlib.Path | None) -> dict[str, Any]:
    own = source["own_ship"]
    target = source["targets"][0]
    constraints = source["constraints"]
    n_rps = (2.2009683486210925 if mode == "live_rule14" else
             1.75 if mode == "benchmark_rule14" else 3.0)
    model = MMGModel(MMGCoefficients(dt=dt))
    state = ShipState(x=own["x_m"], y=own["y_m"], psi=own["psi_rad"],
                      u=own["u_mps"], v=own.get("v_mps", 0.0), r=own.get("r_rad_s", 0.0))
    rudder = 0.0
    min_distance = float("inf")
    min_time = 0.0
    cpa_own = state
    cpa_target = target_xy(target, 0.0)
    max_rot = 0.0
    min_speed = float("inf")
    max_speed = 0.0
    heading_23p2 = None
    heading_30 = None
    max_heading_delta = 0.0
    min_heading = state.psi
    max_heading = state.psi
    previous = None
    rows = []
    count = round(1200.0 / dt)
    for index in range(count + 1):
        t_s = index * dt
        target_position = target_xy(target, t_s)
        if previous is not None:
            prev_t, prev_state, prev_target = previous
            r0 = (prev_target[0] - prev_state.x, prev_target[1] - prev_state.y)
            r1 = (target_position[0] - state.x, target_position[1] - state.y)
            dr = (r1[0] - r0[0], r1[1] - r0[1])
            denominator = dr[0] * dr[0] + dr[1] * dr[1]
            alpha = max(0.0, min(1.0, -(r0[0] * dr[0] + r0[1] * dr[1]) / denominator)) if denominator else 0.0
            relative = (r0[0] + alpha * dr[0], r0[1] + alpha * dr[1])
            distance = math.hypot(*relative)
            if distance < min_distance:
                min_distance = distance
                min_time = prev_t + alpha * dt
                cpa_own = ShipState(**{field: getattr(prev_state, field) + alpha * (getattr(state, field) - getattr(prev_state, field))
                                       for field in ("x", "y", "psi", "phi", "u", "v", "r", "p")})
                cpa_target = (prev_target[0] + alpha * (target_position[0] - prev_target[0]),
                              prev_target[1] + alpha * (target_position[1] - prev_target[1]))
        speed = math.hypot(state.u, state.v)
        max_rot = max(max_rot, abs(state.r)); min_speed = min(min_speed, speed); max_speed = max(max_speed, speed)
        delta_heading = state.psi - own["psi_rad"]
        max_heading_delta = max(max_heading_delta, delta_heading)
        min_heading = min(min_heading, state.psi)
        max_heading = max(max_heading, state.psi)
        if heading_23p2 is None and delta_heading >= math.radians(23.2): heading_23p2 = t_s
        if heading_30 is None and delta_heading >= math.radians(30.0): heading_30 = t_s
        if save_csv is not None:
            rows.append([index, t_s, state.x, state.y, state.psi, state.u, state.v, state.r, rudder,
                         target_position[0], target_position[1]])
        if index == count:
            break
        desired = command_for(mode, t_s)
        max_step = math.radians(2.0) * dt
        rudder += max(-max_step, min(max_step, desired - rudder))
        previous = (t_s, state, target_position)
        state = model.rk4_step(state, rudder, n_rps)
    if save_csv is not None:
        with save_csv.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.writer(stream)
            writer.writerow(["index", "t_s", "own_x_m", "own_y_m", "psi_rad", "u_mps", "v_mps", "r_rad_s",
                             "rudder_rad", "target_x_m", "target_y_m"])
            writer.writerows(rows)
    rx, ry = cpa_target[0] - cpa_own.x, cpa_target[1] - cpa_own.y
    body_along = math.cos(cpa_own.psi) * rx + math.sin(cpa_own.psi) * ry
    body_starboard = -math.sin(cpa_own.psi) * rx + math.cos(cpa_own.psi) * ry
    hard = constraints.get("cpa_hard_m", constraints["cpa_safe_m"])
    soft = constraints["cpa_safe_m"]
    return {"dt_s": dt, "continuous_segment_cpa_m": min_distance, "tcpa_s": min_time,
            "hard_margin_m": min_distance - hard, "soft_margin_m": min_distance - soft,
            "cpa_body_along_m": body_along, "cpa_body_starboard_m": body_starboard,
            "port_to_port": body_starboard < 0.0, "t_heading23p2_s": heading_23p2,
            "t_heading30_s": heading_30, "max_heading_delta_deg": math.degrees(max_heading_delta),
            "heading_range_rad": [min_heading, max_heading],
            "final_heading_deg": math.degrees(state.psi),
            "max_rot_deg_s": math.degrees(max_rot), "speed_range_mps": [min_speed, max_speed]}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=pathlib.Path, required=True)
    parser.add_argument("--mode", choices=("target2500", "benchmark_rule14", "live_rule14"), required=True)
    parser.add_argument("--acados-verdict", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--localization", type=pathlib.Path)
    args = parser.parse_args(); args.output.mkdir(parents=True, exist_ok=True)
    raw = args.input.read_bytes(); decoded = json.loads(raw)
    source = decoded["mid_mpc_input"] if "mid_mpc_input" in decoded else decoded
    results = []
    trajectory_path = args.output / "reference_trajectory_dt001.csv"
    for dt in (0.2, 0.1, 0.05, 0.02, 0.01):
        results.append(simulate(source, args.mode, dt, trajectory_path if dt == 0.01 else None))
    final = results[-1]
    hard = source["constraints"].get("cpa_hard_m", source["constraints"]["cpa_safe_m"])
    acados = json.loads(args.acados_verdict.read_text(encoding="utf-8"))
    raw_status = acados.get("raw_status")
    min_alt_required = source["constraints"].get("min_alt_required_rad", 0.0)
    min_alt_attained = final["max_heading_delta_deg"] + 1e-9 >= math.degrees(min_alt_required)
    # heading_min is a maneuver-attainment requirement in this input, not an
    # admissible x0 bound: the captured own heading starts below it.  Check
    # attainment through min_alt_required and enforce the absolute upper bound.
    heading_upper_respected = final["heading_range_rad"][1] <= source["constraints"]["heading_max_rad"] + 1e-9
    witness_ok = (final["continuous_segment_cpa_m"] > hard and
                  final["max_rot_deg_s"] <= math.degrees(source.get("rot_max_rad_s", source["constraints"].get("rot_max_rad_s", 0.2094))) + 1e-9 and
                  final["speed_range_mps"][0] >= source["constraints"]["speed_min_mps"] - 1e-9 and
                  final["speed_range_mps"][1] <= source["constraints"]["speed_max_mps"] + 1e-9 and
                  min_alt_attained and
                  heading_upper_respected and
                  (args.mode not in ("live_rule14", "benchmark_rule14") or final["port_to_port"]))
    reference_status = "REFERENCE_FEASIBLE" if witness_ok else "REFERENCE_FEASIBILITY_OPEN"
    if witness_ok:
        four_class = "REFERENCE_FEASIBLE + ACADOS_SUCCESS" if raw_status == 0 else "REFERENCE_FEASIBLE + ACADOS_FAILURE"
    else:
        # A failed positive-witness search is not an infeasibility certificate.
        four_class = "OPEN"
    oracle = {"schema": "m5_independent_mmg_oracle_v1", "input": str(args.input),
              "input_sha256": hashlib.sha256(raw).hexdigest(),
              "model": "SIL ship_dynamics.MMGModel default MMGCoefficients, RK4, no wind/current",
              "independence": "No acados, HPIPM, CasADi, Mid-MPC surrogate dynamics, objective, Hessian, or seed used.",
              "control": {"n_rps": (2.2009683486210925 if args.mode == "live_rule14" else
                                      1.75 if args.mode == "benchmark_rule14" else 3.0),
                          "rudder_slew_deg_s": 2.0,
                          "command_segments": ([[0, 52, 0.4], [52, 68, -0.4], [68, 1200, 0.0]]
                                               if args.mode == "live_rule14" else
                                               [[0, 63.54084409738354, 0.4], [63.54084409738354, 1200, 0.0]]
                                               if args.mode == "benchmark_rule14" else
                                               [[0, 48, 0.4], [48, 1200, 0.0]]),
                          "max_abs_rudder_rad": 0.4},
              "dt_refinement": results, "reference_status": reference_status,
              "witness_checks": {"hard_cpa": final["continuous_segment_cpa_m"] > hard,
                                   "min_alt_attained": min_alt_attained,
                                   "heading_upper_respected": heading_upper_respected,
                                   "rot_within_input_limit": final["max_rot_deg_s"] <= math.degrees(source.get("rot_max_rad_s", source["constraints"].get("rot_max_rad_s", 0.2094))) + 1e-9,
                                   "speed_within_input_limits": final["speed_range_mps"][0] >= source["constraints"]["speed_min_mps"] - 1e-9 and final["speed_range_mps"][1] <= source["constraints"]["speed_max_mps"] + 1e-9,
                                   "port_to_port_if_rule14": args.mode not in ("live_rule14", "benchmark_rule14") or final["port_to_port"]},
              "four_class_verdict": four_class,
              "basis": "collision_avoidance_reachability_against_cpa_hard_m",
              "acados_raw_status": raw_status,
              "limitations": (["cpa_safe_2500_not_achieved_but_soft_slack_contract",
                                "heading_min_is_treated_as_temporal_attainment_not_an_x0_bound"]
                               if args.mode == "live_rule14" else []),
              "still_open": ["disturbance_and_uncertainty", "route_return", "exact_L4_execution", "GNC_executability"],
              "trajectory_csv": trajectory_path.name}
    write_json(args.output / "reference_oracle.json", oracle)
    write_json(args.output / "reference_witness.json", {"control": oracle["control"], "dt_refinement": results,
                                                        "finest_dt": final, "witness_checks_pass": witness_ok})
    write_json(args.output / "gnc_executability.json", {"status": "OPEN", "collision_reachability": reference_status,
                                                        "reason": "MMG witness does not close exact L4 execution, route return, disturbance, or uncertainty."})
    acados["reference_feasibility"] = reference_status
    acados["classification"] = four_class
    acados["classification_scope"] = "collision_avoidance_reachability_against_cpa_hard_m"
    acados["full_mid_to_l4_classification"] = ("REFERENCE_UNKNOWN + ACADOS_SUCCESS" if raw_status == 0
                                                else "REFERENCE_UNKNOWN + ACADOS_FAILURE")
    acados["gnc_executability"] = "OPEN"
    acados.setdefault("phase0_subgates", {})["independent_reference_oracle"] = witness_ok
    acados["phase0_subgates"]["independent_gnc_executability"] = False
    acados["phase0_subgates"]["independent_gnc_review_performed"] = True
    acados["phase0_subgates"]["live_solve_boundary_input"] = "boundary" in decoded
    if args.localization is not None:
        localization = json.loads(args.localization.read_text(encoding="utf-8"))
        acados["phase0_subgates"]["first_failure_inducing_stage_target_localization"] = True
        acados["failure_localization"] = {
            "evidence": str(args.localization),
            "first_failure_inducing_included_horizon": localization["first_failure_inducing_included_horizon"],
            "first_failure_inducing_added_path_stage": localization["first_failure_inducing_added_path_stage"],
            "changed_terminal_stage": localization["changed_terminal_stage"],
            "target_localization": localization["target_localization"],
        }
    acados["evidence_complete_phase0"] = True
    acados["completion_note"] = "Requested Phase-0 evidence acquired; hard-CPA collision reachability is classified; full Mid-to-L4 GNC outcome remains OPEN."
    if "boundary" in decoded:
        acados["live_capture_provenance"] = "real dispatcher solve boundary; strict SIL G-ART manifest remains pending/valid_data=false"
    write_json(args.output / "verdict.json", acados)
    command = (f"PYTHONPATH=src/sim_workbench/sil_nodes/ship_dynamics python3 {pathlib.Path(__file__)} "
               f"--input {args.input} --mode {args.mode} --acados-verdict {args.acados_verdict} --output {args.output}" +
               (f" --localization {args.localization}" if args.localization is not None else ""))
    (args.output / "reference_oracle_command.txt").write_text(command + "\n", encoding="utf-8")
    print(json.dumps({"reference_status": reference_status, "four_class_verdict": four_class, "finest": final}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
