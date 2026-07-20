#!/usr/bin/env python3
"""Run one frozen Mid-MPC case against the existing production acados artifact.

No production source or solver default is changed. Parameter packing, F1 seed,
and active-row bounds mirror mid_mpc_acados_{formulation,solver}.cpp. Evidence
is deliberately verbose and machine-readable so later derivative/ablation work
can trace every claim back to a frozen input and artifact hash.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import pathlib
import shutil
import sys
import time
from typing import Any

import numpy as np
from acados_template import AcadosOcpSolver

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import gen_mid_mpc_acados as gen  # noqa: E402


def sha256(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def write_json(path: pathlib.Path, obj: Any) -> None:
    path.write_text(json.dumps(obj, indent=2, sort_keys=True, allow_nan=False) + "\n", encoding="utf-8")


def finite(value: Any) -> Any:
    if isinstance(value, dict):
        return {str(k): finite(v) for k, v in value.items()}
    if isinstance(value, (list, tuple)):
        return [finite(x) for x in value]
    if isinstance(value, np.ndarray):
        return finite(value.tolist())
    if isinstance(value, (np.floating, float)):
        value = float(value)
        return value if math.isfinite(value) else str(value)
    if isinstance(value, (np.integer, int)):
        return int(value)
    return value


def ou_params(target: dict[str, Any]) -> tuple[float, float]:
    cls = int(target.get("classification", 0))
    sog = float(target.get("sog_mps", 0.0))
    intent = float(target.get("intent_confidence", 0.5))
    if cls == 2:
        return 5.0, 1.0e9
    high_speed = sog > 5.0
    low_intent = intent < 0.3
    if cls == 1:
        if high_speed and low_intent:
            return 100.0, 300.0
        if high_speed:
            return 50.0, 500.0
        if low_intent:
            return 60.0, 400.0
        return 30.0, 600.0
    return 80.0, 400.0


def sigma_pos(target: dict[str, Any], t_s: float) -> float:
    sigma0, tau = ou_params(target)
    if t_s <= 0.0:
        return 0.0
    exponent = -2.0 * t_s / tau
    # Mirror current production helper exactly, including its <-50 branch.
    if exponent < -50.0:
        return sigma0 * math.sqrt(2.0)
    return sigma0 * math.sqrt(1.0 - math.exp(exponent))


def build_seed(case: dict[str, Any]) -> tuple[np.ndarray, np.ndarray]:
    own = case["own_ship"]
    xs = np.zeros((gen.N + 1, 5))
    us = np.zeros((gen.N, 2))
    x = np.array([own["x_m"], own["y_m"], own["psi_rad"], 0.0, own["u_mps"]], dtype=float)
    n_hold = min(12.0, max(0.0, math.sqrt(gen.K_DRAG / gen.K_PROP) * x[4]))
    for k in range(gen.N + 1):
        xs[k] = x
        if k == gen.N:
            break
        us[k] = [0.0, n_hold]
        px, py, psi, r, surge = x
        x = np.array([
            px + surge * gen.DT * math.cos(psi),
            py + surge * gen.DT * math.sin(psi),
            psi + gen.DT * r,
            r,
            surge + gen.DT * (gen.K_PROP_PER_MASS * n_hold * n_hold - gen.K_DRAG_PER_MASS * surge * surge),
        ])
    return xs, us


def pack(case: dict[str, Any], seed_x: np.ndarray) -> list[np.ndarray]:
    own, route, con = case["own_ship"], case["route"], case["constraints"]
    col, prefix, targets = case["colregs"], case["prefix"], case["targets"]
    g = np.zeros(gen.NP_GLOBAL)
    g[0:4] = [own["psi_rad"], own["u_mps"], own["x_m"], own["y_m"]]
    g[4:14] = [
        route["bearing_rad"], route["planned_speed_mps"], con["heading_min_rad"],
        con["heading_max_rad"], con["speed_min_mps"], con["speed_max_mps"],
        con["cpa_safe_m"], con["rot_max_rad_s"], con["own_ship_psi_rad"],
        1.0 if any(int(rule) in (14, 15) for rule in con["applicable_rules"]) else 0.0,
    ]
    g[14:26] = [
        route["frame_origin_x_m"], route["frame_origin_y_m"], route["frame_normal_x"],
        route["frame_normal_y"], route["active_leg_bearing_rad"], route["lateral_scale_m"],
        route["route_weight"], max(0, min(gen.N, int(prefix["active_k"]))),
        col["preferred_direction"], col["min_alteration_rad"],
        1.0 if int(col["primary_role"]) in (1, 2) else 0.0, con["decel_max_mps2"],
    ]
    for i, tgt in enumerate(targets[: gen.NT]):
        base = 26 + 8 * i
        rng = math.hypot(tgt["x_m"] - own["x_m"], tgt["y_m"] - own["y_m"])
        span = max(gen.PWT_OUTER_M - con["cpa_safe_m"], 1.0)
        weight = min(1.0, max(0.0, (gen.PWT_OUTER_M - rng) / span))
        g[base:base + 8] = [
            tgt["x_m"], tgt["y_m"], tgt["cog_rad"], tgt["sog_mps"], weight,
            tgt.get("intent_confidence", 0.5), tgt.get("target_compliance", 0.5),
            tgt.get("classification", 0),
        ]

    params: list[np.ndarray] = []
    active_k = max(0, min(gen.N, int(prefix["active_k"])))
    bearing = float(route["active_leg_bearing_rad"])
    dx, dy = math.cos(bearing), math.sin(bearing)
    ax, ay = float(route["frame_origin_x_m"]), float(route["frame_origin_y_m"])
    extent = max(abs(float(route["planned_speed_mps"])) * (gen.N + 2) * gen.DT, gen.DT)
    for k in range(gen.N + 1):
        ps = np.zeros(gen.NP_PER_STAGE)
        if k < active_k:
            ps[0] = prefix["psi_rad"][k] if k < len(prefix["psi_rad"]) else 0.0
            ps[1] = prefix["u_mps"][k] if k < len(prefix["u_mps"]) else own["u_mps"]
            ps[2] = 1.0
        for i, tgt in enumerate(targets[: gen.NT]):
            t = min(k, gen.N - 1) * gen.DT
            ps[3 + i] = tgt["x_m"] + tgt["sog_mps"] * math.cos(tgt["cog_rad"]) * t
            ps[3 + gen.NT + i] = tgt["y_m"] + tgt["sog_mps"] * math.sin(tgt["cog_rad"]) * t
            ps[gen.PS_SIGMA_POS_OFF + i] = sigma_pos(tgt, t)
        relx, rely = seed_x[k, 0] - ax, seed_x[k, 1] - ay
        tau = min(extent, max(0.0, relx * dx + rely * dy))
        ps[gen.PS_TB_X_OFF], ps[gen.PS_TB_Y_OFF] = ax + tau * dx, ay + tau * dy
        ps[gen.PS_PSI_PREV_OFF] = seed_x[k, 2]
        ps[gen.PS_U_PREV_OFF] = seed_x[k, 4]
        ps[gen.PS_W_TRANS_ACTIVE_OFF] = 0.0
        params.append(np.concatenate([g, ps]))
    return params


def bounds(case: dict[str, Any], stage: int, schedule: str) -> tuple[np.ndarray, np.ndarray]:
    lo, hi = np.zeros(gen.NT + 4), np.full(gen.NT + 4, gen.UH_INF)
    lo[0:2] = 0.0
    hi[0:2] = 0.0
    nt = len(case["targets"])
    lo[2 + nt:2 + gen.NT] = -gen.UH_INF
    hi[2 + nt:2 + gen.NT] = gen.UH_INF
    lateral = int(case["colregs"]["primary_role"]) in (1, 2) and int(case["colregs"]["preferred_direction"]) != 0
    if not lateral:
        lo[2 + gen.NT:] = -gen.UH_INF
        hi[2 + gen.NT:] = gen.UH_INF
    elif schedule == "reachability_scheduled_bounds" and stage < 2:
        # Explicit Euler: r0 is pinned; delta0 changes r1, then psi2. psi1 is unreachable.
        lo[2 + gen.NT + 1] = -gen.UH_INF
        hi[2 + gen.NT + 1] = gen.UH_INF
    return lo, hi


def residual_rows(case: dict[str, Any], params: list[np.ndarray], xs: np.ndarray,
                  slacks: list[np.ndarray], schedule: str) -> dict[str, Any]:
    violations: list[dict[str, Any]] = []
    stages: list[dict[str, Any]] = []
    # Generated dimensions prove path h is absent at stage 0 and terminal N.
    for k in range(1, gen.N):
        p = params[k]
        ps = p[gen.NP_GLOBAL:]
        x = xs[k]
        h = [ps[2] * (x[2] - ps[0]), ps[2] * (x[4] - ps[1])]
        for i in range(gen.NT):
            h.append((x[0] - ps[3 + i]) ** 2 + (x[1] - ps[3 + gen.NT + i]) ** 2 - case["constraints"]["cpa_safe_m"] ** 2)
        lateral = (x[0] - case["route"]["frame_origin_x_m"]) * case["route"]["frame_normal_x"] + (x[1] - case["route"]["frame_origin_y_m"]) * case["route"]["frame_normal_y"]
        pref = case["colregs"]["preferred_direction"]
        h += [pref * lateral, pref * (x[2] - case["constraints"]["own_ship_psi_rad"]) - case["colregs"]["min_alteration_rad"]]
        lo, hi = bounds(case, k, schedule)
        stage_violations = []
        for row, value in enumerate(h):
            effective = float(value)
            slack = 0.0
            if 2 <= row < 2 + len(case["targets"]):
                slack = float(slacks[k][row - 2]) if len(slacks[k]) else 0.0
                effective += slack
            if effective < lo[row] - 1e-8 or effective > hi[row] + 1e-8:
                name = "prefix_psi" if row == 0 else "prefix_u" if row == 1 else f"cpa_target_{row-2}" if row < 2 + gen.NT else "direction" if row == 2 + gen.NT else "min_alt"
                item = {"stage": k, "row": row, "name": name, "value": float(value), "slack": slack, "effective_value": effective, "lower": float(lo[row]), "upper": float(hi[row])}
                stage_violations.append(item)
                violations.append(item)
        stages.append({"stage": k, "min_h": float(min(h)), "violations": stage_violations})
    return {
        "generated_stage0": {"nh_0": 0, "nsh_0": 0, "note": "stage-0 nonlinear h setter has no runtime row"},
        "schedule": schedule,
        "first_violation": violations[0] if violations else None,
        "violation_count": len(violations),
        "violations": violations,
        "stage_summary": stages,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--case", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--repo", type=pathlib.Path, required=True)
    parser.add_argument("--bounds-schedule", choices=["production_bounds", "reachability_scheduled_bounds"], default="production_bounds")
    parser.add_argument("--regenerate", action="store_true", help="fresh diagnostic-only codegen with unchanged production options")
    parser.add_argument("--debug-iterates", action="store_true", help="debug-only store_iterates + extended QP residuals")
    parser.add_argument("--execution-head", required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    case = json.loads(args.case.read_text(encoding="utf-8"))
    shutil.copyfile(args.case, args.output / "input.json")
    input_hash = sha256(args.output / "input.json")

    backend = pathlib.Path(__file__).resolve().parents[1]
    ocp = gen.build_ocp()
    if args.debug_iterates:
        ocp.solver_options.store_iterates = True
        ocp.solver_options.nlp_solver_ext_qp_res = 1
    if args.regenerate:
        generated = args.output / "diagnostic_generated_code"
        if generated.exists():
            shutil.rmtree(generated)
        generated.mkdir(parents=True)
        ocp.code_export_directory = str(generated)
        json_file = args.output / "acados_ocp_m5_mid_mpc_acados.json"
        solver = AcadosOcpSolver(ocp, json_file=str(json_file), generate=True, build=True, verbose=False)
    else:
        generated = backend / "c_generated_code"
        json_file = generated / "acados_ocp_m5_mid_mpc_acados.json"
        ocp.code_export_directory = str(generated)
        solver = AcadosOcpSolver(ocp, json_file=str(json_file), generate=False, build=False, verbose=False)
    generated_lib = generated / "libacados_ocp_solver_m5_mid_mpc_acados.so"

    seed_x, seed_u = build_seed(case)
    params = pack(case, seed_x)
    for k in range(gen.N + 1):
        solver.set(k, "p", params[k])
        solver.set(k, "x", seed_x[k])
        if k < gen.N:
            solver.set(k, "u", seed_u[k])
    x0 = seed_x[0]
    solver.constraints_set(0, "lbx", x0)
    solver.constraints_set(0, "ubx", x0)
    for k in range(1, gen.N):
        lo, hi = bounds(case, k, args.bounds_schedule)
        solver.constraints_set(k, "lh", lo)
        solver.constraints_set(k, "uh", hi)

    zero_slacks = [np.zeros(gen.NT) for _ in range(gen.N + 1)]
    seed_residual = residual_rows(case, params, seed_x, zero_slacks, args.bounds_schedule)
    write_json(args.output / "seed_trajectory.json", {
        "input_sha256": input_hash,
        "x": finite(seed_x), "u": finite(seed_u),
        "constraint_residual": seed_residual,
    })
    write_json(args.output / "iterate_000.json", {
        "kind": "explicit_initial_iterate_before_solve",
        "x": finite(seed_x), "u": finite(seed_u), "sl": finite(np.zeros((gen.N + 1, gen.NT))),
    })

    start = time.monotonic()
    raw_status = int(solver.solve())
    wall_s = time.monotonic() - start
    statistics = np.asarray(solver.get_stats("statistics"), dtype=float)
    residuals = np.asarray(solver.get_stats("residuals"), dtype=float)
    sqp_iter = int(solver.get_stats("sqp_iter"))
    qp_iter = np.asarray(solver.get_stats("qp_iter"), dtype=float)
    alpha = np.asarray(solver.get_stats("alpha"), dtype=float)
    stat_m = int(solver.get_stats("stat_m"))
    stat_n = int(solver.get_stats("stat_n"))
    qp_stat = finite(np.asarray(solver.get_stats("qp_stat"), dtype=float))
    res_eq_all = finite(np.asarray(solver.get_stats("res_eq_all"), dtype=float))
    res_stat_all = finite(np.asarray(solver.get_stats("res_stat_all"), dtype=float))
    xs = np.vstack([solver.get(k, "x") for k in range(gen.N + 1)])
    us = np.vstack([solver.get(k, "u") for k in range(gen.N)])
    slacks = [np.zeros(gen.NT)]
    for k in range(1, gen.N):
        slacks.append(np.asarray(solver.get(k, "sl"), dtype=float))
    slacks.append(np.zeros(gen.NT))
    solution_residual = residual_rows(case, params, xs, slacks, args.bounds_schedule)
    trajectory_delta = max(float(np.max(np.abs(xs - seed_x))), float(np.max(np.abs(us - seed_u))))

    try:
        solver.dump_last_qp_to_json(filename=str(args.output / "last_qp.json"), overwrite=True)
        qp_dump = {"available": True, "path": "last_qp.json", "sha256": sha256(args.output / "last_qp.json")}
    except Exception as exc:  # diagnostic must preserve solver output even if dump API fails
        qp_dump = {"available": False, "error": repr(exc)}
    try:
        solver.store_iterate(filename=str(args.output / "iterate_final.json"), overwrite=True, verbose=False)
        final_iterate = {"available": True, "path": "iterate_final.json", "sha256": sha256(args.output / "iterate_final.json")}
    except Exception as exc:
        final_iterate = {"available": False, "error": repr(exc)}
    stored_iterates: dict[str, Any] = {"enabled": args.debug_iterates, "count": 0, "paths": []}
    if args.debug_iterates:
        try:
            iterate_dir = args.output / "iterate_store"
            iterate_dir.mkdir(exist_ok=True)
            iterates = solver.get_iterates().iterate_list
            paths = []
            for index, iterate in enumerate(iterates):
                path = iterate_dir / f"iterate_{index:03d}.json"
                write_json(path, {
                    "x": finite(iterate.x_traj), "u": finite(iterate.u_traj),
                    "sl": finite(iterate.sl_traj), "su": finite(iterate.su_traj),
                    "pi": finite(iterate.pi_traj), "lam": finite(iterate.lam_traj),
                })
                paths.append({"path": str(path.relative_to(args.output)), "sha256": sha256(path)})
            stored_iterates = {"enabled": True, "count": len(paths), "paths": paths}
        except Exception as exc:
            stored_iterates = {"enabled": True, "count": 0, "error": repr(exc)}

    qp_status_history = statistics[5, :].astype(int).tolist() if statistics.shape[0] > 5 else []
    failed_indices = [i for i, status in enumerate(qp_status_history) if status not in (0, 2)]
    statistics_rows = ["iter", "res_stat", "res_eq", "res_ineq", "res_comp", "qp_status", "qp_iter", "alpha"]
    if statistics.shape[0] >= 12:
        statistics_rows += ["qp_res_stat", "qp_res_eq", "qp_res_ineq", "qp_res_comp"]
    write_json(args.output / "qp_statistics.json", {
        "raw_nlp_status": raw_status,
        "raw_nlp_status_semantic": {0: "ACADOS_SUCCESS", 1: "ACADOS_NAN_DETECTED", 2: "ACADOS_MAXITER", 3: "ACADOS_MINSTEP", 4: "ACADOS_QP_FAILURE", 5: "ACADOS_READY", 6: "ACADOS_UNBOUNDED", 7: "ACADOS_TIMEOUT"}.get(raw_status, "UNKNOWN"),
        "first_failed_qp_statistics_column": failed_indices[0] if failed_indices else None,
        "qp_status_history": qp_status_history,
        "qp_iter_history": finite(qp_iter),
        "qp_stat_getter": qp_stat,
        "sqp_iter": sqp_iter,
        "stat_m": stat_m,
        "stat_n": stat_n,
        "line_search_alpha": finite(alpha),
        "nlp_residuals_stat_eq_ineq_comp": finite(residuals),
        "res_eq_all": res_eq_all,
        "res_stat_all": res_stat_all,
        "statistics_matrix_rows": statistics_rows,
        "statistics_matrix": finite(statistics),
        "qpscaling_status": {"status": "UNAVAILABLE_IN_LINKED_VERSION", "supported": False, "value": None, "reason": "linked acados v0.4.4 exposes no qpscaling_status API/source field"},
        "qp_status_semantics": {"3": "HPIPM_NAN_SOL (linked HPIPM enum; propagated unchanged by acados HPIPM adapter)"},
        "wall_time_s": wall_s,
        "acados_time_tot_s": float(solver.get_stats("time_tot")),
        "acados_time_qp_s": float(solver.get_stats("time_qp")),
        "qp_dump": qp_dump,
        "iterate_final": final_iterate,
        "stored_iterates": stored_iterates,
    })
    write_json(args.output / "derivative_diagnostics.json", {
        "phase": "Phase0 residual only; finite-difference/Jacobian/Hessian evidence belongs to Phase1",
        "seed_constraint_residual": seed_residual,
        "solution_constraint_residual": solution_residual,
        "trajectory_delta_inf": trajectory_delta,
    })
    write_json(args.output / "solution.json", {
        "raw_status": raw_status, "sqp_iter": sqp_iter, "cost": float(solver.get_cost()),
        "max_slack": max((float(np.max(v)) for v in slacks if len(v)), default=0.0),
        "trajectory_delta_inf": trajectory_delta,
        "x": finite(xs), "u": finite(us), "sl": finite(np.vstack(slacks)),
    })
    with (args.output / "trajectory.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        writer.writerow(["stage", "t_s", "x_m", "y_m", "psi_rad", "r_rad_s", "u_mps", "delta_rad", "n_rps"])
        for k in range(gen.N + 1):
            ctrl = us[k] if k < gen.N else ["", ""]
            writer.writerow([k, k * gen.DT, *xs[k], *ctrl])

    manifest = {
        "worktree_prewrite_snapshot": {
            "head": "c46e01045e1ad443cdadb835e62d822fc6b738a7",
            "tracked_diff_sha256": "a158aabacfde094c96a4c6f9a0635c398c0ec16a78e30e2ac49bdc450a901f48",
            "tracked_diff_scope": "12 files +1382/-146",
            "concurrent_preexisting_untracked_scope": [
                "docs/superpowers/reviews/2026-07-20-m5-solver-ab-1200/",
                "tools/benchmarks/m5_solver_ab_1200/",
            ],
        },
        "execution_head": args.execution_head,
        "input_sha256": input_hash,
        "artifact_mode": ("fresh_debug_codegen_diagnostics_enabled" if args.debug_iterates else "fresh_diagnostic_codegen_same_production_options") if args.regenerate else "checked_in_generated_artifact",
        "generated_json_sha256": sha256(json_file),
        "generated_library_sha256": sha256(generated_lib),
        "dimensions": json.loads(json_file.read_text(encoding="utf-8"))["dims"],
        "solver_options": json.loads(json_file.read_text(encoding="utf-8"))["solver_options"],
        "parameter_vector_sha256_by_stage": [hashlib.sha256(p.tobytes()).hexdigest() for p in params],
    }
    write_json(args.output / "codegen_manifest.json", manifest)
    write_json(args.output / "solver_config.json", {"artifact": "production-generated", "bounds_schedule": args.bounds_schedule, "production_default_changed": False, "solver_options": manifest["solver_options"]})
    write_json(args.output / "gnc_executability.json", {"status": "OPEN", "reason": "Phase0 solver diagnostic; independent GNC reviewer required"})
    write_json(args.output / "verdict.json", {
        "case_id": case["case_id"], "input_sha256": input_hash,
        "reference_feasibility": "OPEN", "acados_result": "SUCCESS" if raw_status == 0 else "FAILURE",
        "classification": "OPEN until independent reference oracle completes",
        "first_seed_constraint_violation": seed_residual["first_violation"],
        "raw_status": raw_status,
        "phase0_subgates": {
            "raw_status_semantics": True,
            "generated_runtime_dimensions": True,
            "qp_dump": qp_dump.get("available", False),
            "first_failure_inducing_stage_target_localization": False,
            "live_solve_boundary_input": False,
            "independent_reference_oracle": False,
            "independent_gnc_executability": False,
        },
        "evidence_complete_phase0": False,
    })
    print(json.dumps({"case_id": case["case_id"], "raw_status": raw_status, "sqp_iter": sqp_iter, "first_seed_violation": seed_residual["first_violation"], "output": str(args.output)}, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
