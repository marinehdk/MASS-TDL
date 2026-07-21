#!/usr/bin/env python3
"""One-variable acados ablation runner: five fresh capsules + twenty warm solves."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import pathlib
import shutil
import sys
import time
from typing import Any

import casadi as ca
import numpy as np
from acados_template import AcadosOcpSolver

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import run_phase0_case as p0  # noqa: E402


VARIANTS = {
    "baseline": "Unmodified production solver options and straight F1 seed",
    "starboard_seed": "Only initial iterate changes to dynamics-feasible starboard maneuver",
    "partial_condensing": "Only condensing strategy: PARTIAL_CONDENSING_HPIPM cond_N=20",
    "hpipm_robust": "Only hpipm_mode: BALANCE -> ROBUST",
    "cond_ric_alg_only": "Only condensing Riccati algorithm: square-root(1) -> classical(0)",
    "qp_ric_alg_only": "Only HPIPM QP Riccati algorithm: square-root(1) -> classical(0)",
    "classical_riccati": "Combined condensing and QP Riccati algorithms; retained but not used for single-variable attribution",
    "residual_slack_init": "Only initial lower CPA slacks: zero -> max(0,-h)+eps",
    "dimensionless_cpa": "Only CPA constraint scale: h[m^2] -> h/R_safe^2",
    "deterministic_collision": "Only P7 sigma inputs forced to zero; same center collision cost",
    "feasible_qp_funnel": "Only NLP/globalization: SQP+merit -> SQP_WITH_FEASIBLE_QP+funnel",
    "reachability_scheduled_bounds": "Extra: disable min-alt at unreachable stage1 only",
    "gershgorin_lm": "Named GERSHGORIN_* regularize_method (unsupported in linked v0.4.4)",
    "gershgorin_derived_lm": "Only fixed Levenberg-Marquardt term, derived from baseline condensed-Hessian Gershgorin bound",
    "project_regularization": "Only regularize_method: NO_REGULARIZE -> PROJECT",
    "gauss_newton_hessian": "Only hessian_approx: EXACT -> GAUSS_NEWTON (support probe for EXTERNAL cost)",
    "collision_cost_off": "Only active-target collision cost weights set to zero; CPA constraint rows retained",
    "cpa_rows_relaxed": "Only CPA constraint lower bounds relaxed; collision cost retained",
    "lm_dimensionless_interaction": "Pre-registered two-factor interaction: stage-Gershgorin LM plus dimensionless CPA rows",
}


def starboard_seed(case: dict[str, Any]) -> tuple[np.ndarray, np.ndarray]:
    own = case["own_ship"]
    xs = np.zeros((p0.gen.N + 1, 5)); us = np.zeros((p0.gen.N, 2))
    x = np.array([own["x_m"], own["y_m"], own["psi_rad"], 0.0, own["u_mps"]], dtype=float)
    n_hold = min(12.0, max(0.0, math.sqrt(p0.gen.K_DRAG / p0.gen.K_PROP) * x[4]))
    for k in range(p0.gen.N + 1):
        xs[k] = x
        if k == p0.gen.N:
            break
        delta = 0.004 if k < 10 else -0.004 if k < 20 else 0.0
        us[k] = [delta, n_hold]
        px, py, psi, r, surge = x
        x = np.array([
            px + surge * p0.gen.DT * math.cos(psi),
            py + surge * p0.gen.DT * math.sin(psi),
            psi + p0.gen.DT * r,
            r + p0.gen.DT * p0.gen.C_U * delta,
            surge + p0.gen.DT * (p0.gen.K_PROP_PER_MASS * n_hold * n_hold - p0.gen.K_DRAG_PER_MASS * surge * surge),
        ])
    return xs, us


def mutate_ocp(ocp: Any, variant: str, lm_mu: float | None) -> None:
    opts = ocp.solver_options
    opts.nlp_solver_ext_qp_res = 1
    if variant == "partial_condensing":
        opts.qp_solver = "PARTIAL_CONDENSING_HPIPM"
        opts.qp_solver_cond_N = 20
    elif variant == "hpipm_robust":
        opts.hpipm_mode = "ROBUST"
    elif variant == "cond_ric_alg_only":
        opts.qp_solver_cond_ric_alg = 0
    elif variant == "qp_ric_alg_only":
        opts.qp_solver_ric_alg = 0
    elif variant == "classical_riccati":
        opts.qp_solver_ric_alg = 0
        opts.qp_solver_cond_ric_alg = 0
    elif variant in ("gershgorin_derived_lm", "lm_dimensionless_interaction"):
        if lm_mu is None:
            raise RuntimeError("gershgorin_derived_lm requires a baseline-derived LM value")
        opts.levenberg_marquardt = lm_mu
    elif variant == "project_regularization":
        opts.regularize_method = "PROJECT"
    elif variant == "gauss_newton_hessian":
        opts.hessian_approx = "GAUSS_NEWTON"
    elif variant == "feasible_qp_funnel":
        opts.nlp_solver_type = "SQP_WITH_FEASIBLE_QP"
        opts.globalization = "FUNNEL_L1PEN_LINESEARCH"
    elif variant == "dimensionless_cpa":
        h = ocp.model.con_h_expr
        cpa = ocp.model.p[p0.gen.G_CPA_SAFE]
        ocp.model.con_h_expr = ca.vertcat(h[0:2], h[2:2 + p0.gen.NT] / (cpa * cpa), h[2 + p0.gen.NT:])
    if variant == "lm_dimensionless_interaction":
        h = ocp.model.con_h_expr
        cpa = ocp.model.p[p0.gen.G_CPA_SAFE]
        ocp.model.con_h_expr = ca.vertcat(h[0:2], h[2:2 + p0.gen.NT] / (cpa * cpa), h[2 + p0.gen.NT:])


def configure_solver(solver: AcadosOcpSolver, case: dict[str, Any], variant: str,
                     seed_x: np.ndarray, seed_u: np.ndarray, params: list[np.ndarray]) -> None:
    if variant in ("deterministic_collision", "collision_cost_off"):
        params = [p.copy() for p in params]
        if variant == "deterministic_collision":
            for p in params:
                p[p0.gen.NP_GLOBAL + p0.gen.PS_SIGMA_POS_OFF:
                  p0.gen.NP_GLOBAL + p0.gen.PS_SIGMA_POS_OFF + p0.gen.NT] = 0.0
        else:
            for p in params:
                for i in range(min(len(case["targets"]), p0.gen.NT)):
                    p[p0.gen.G_TARGETS + 8 * i + 4] = 0.0
    for k in range(p0.gen.N + 1):
        solver.set(k, "p", params[k]); solver.set(k, "x", seed_x[k])
        if k < p0.gen.N:
            solver.set(k, "u", seed_u[k])
    solver.constraints_set(0, "lbx", seed_x[0]); solver.constraints_set(0, "ubx", seed_x[0])
    schedule = "reachability_scheduled_bounds" if variant == "reachability_scheduled_bounds" else "production_bounds"
    for k in range(1, p0.gen.N):
        lo, hi = p0.bounds(case, k, schedule)
        if variant == "cpa_rows_relaxed":
            lo[2:2 + p0.gen.NT] = -1.0e15
        solver.constraints_set(k, "lh", lo); solver.constraints_set(k, "uh", hi)
        if variant == "residual_slack_init":
            sl = np.zeros(p0.gen.NT)
            for i, target in enumerate(case["targets"][:p0.gen.NT]):
                t = min(k, p0.gen.N - 1) * p0.gen.DT
                tx = target["x_m"] + target["sog_mps"] * math.cos(target["cog_rad"]) * t
                ty = target["y_m"] + target["sog_mps"] * math.sin(target["cog_rad"]) * t
                h = (seed_x[k, 0] - tx) ** 2 + (seed_x[k, 1] - ty) ** 2 - case["constraints"]["cpa_safe_m"] ** 2
                sl[i] = max(0.0, -h) + (1.0e-9 if h < 0.0 else 0.0)
            solver.set(k, "sl", sl)


def solve_record(solver: AcadosOcpSolver, sequence: str, index: int) -> dict[str, Any]:
    begin = time.monotonic(); raw = int(solver.solve()); wall = time.monotonic() - begin
    statistics = np.asarray(solver.get_stats("statistics"), dtype=float)
    residuals = np.asarray(solver.get_stats("residuals"), dtype=float)
    qp_status = statistics[5, :].astype(int).tolist() if statistics.shape[0] > 5 else []
    try:
        alpha = p0.finite(np.asarray(solver.get_stats("alpha"), dtype=float))
    except Exception as exc:
        alpha = {"available": False, "exception": str(exc)}
    return {
        "sequence": sequence, "index": index, "raw_status": raw,
        "raw_semantic": {0: "SUCCESS", 1: "NAN_DETECTED", 2: "MAXITER", 3: "MINSTEP", 4: "QP_FAILURE", 5: "READY", 6: "UNBOUNDED", 7: "TIMEOUT"}.get(raw, "UNKNOWN"),
        "sqp_iter": int(solver.get_stats("sqp_iter")),
        "qp_status_history": qp_status,
        "first_failed_qp_column": next((i for i, value in enumerate(qp_status) if value not in (0, 2)), None),
        "qp_iter_history": p0.finite(np.asarray(solver.get_stats("qp_iter"), dtype=float)),
        "alpha": alpha,
        "nlp_residuals": p0.finite(residuals),
        "wall_time_s": wall, "acados_time_tot_s": float(solver.get_stats("time_tot")),
        "cost": float(solver.get_cost()),
    }


def aggregate(records: list[dict[str, Any]], sequence: str) -> dict[str, Any]:
    rows = [row for row in records if row["sequence"] == sequence]
    statuses: dict[str, int] = {}
    for row in rows:
        statuses[str(row["raw_status"])] = statuses.get(str(row["raw_status"]), 0) + 1
    return {
        "count": len(rows), "raw_status_counts": statuses,
        "success_count": sum(row["raw_status"] == 0 for row in rows),
        "median_wall_s": float(np.median([row["wall_time_s"] for row in rows])) if rows else None,
        "median_sqp_iter": float(np.median([row["sqp_iter"] for row in rows])) if rows else None,
        "all_finite_residuals": all(all(math.isfinite(float(v)) for v in row["nlp_residuals"]) for row in rows),
    }


def write_not_run_evidence(output: pathlib.Path, case: dict[str, Any], config: dict[str, Any],
                           execution_head: str, status: str, reason: str) -> None:
    p0.write_json(output / "solver_config.json", {**config, "supported": False, "status": status, "reason": reason})
    p0.write_json(output / "verdict.json", {"case_id": case["case_id"], "variant": config["variant"],
                  "status": status, "no_solver_run": True, "reason": reason})
    p0.write_json(output / "qp_statistics.json", {"cold": {"count": 0}, "warm": {"count": 0}, "reason": reason})
    for name, content in {
        "codegen_manifest.json": {"execution_head": execution_head, "generated": False, "reason": reason},
        "seed_trajectory.json": {"status": "NOT_RUN"}, "iterate_000.json": {"status": "NOT_RUN"},
        "derivative_diagnostics.json": {"status": "NOT_RUN", "reason": reason},
        "solution.json": {"status": "NOT_RUN"}, "gnc_executability.json": {"status": "OPEN"},
    }.items():
        p0.write_json(output / name, content)
    (output / "trajectory.csv").write_text("stage,t_s,x_m,y_m,psi_rad,r_rad_s,u_mps,delta_rad,n_rps\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--case", type=pathlib.Path, required=True)
    parser.add_argument("--variant", choices=sorted(VARIANTS), required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--execution-head", required=True)
    args = parser.parse_args(); args.output.mkdir(parents=True, exist_ok=True)
    case = json.loads(args.case.read_text(encoding="utf-8")); shutil.copyfile(args.case, args.output / "input.json")

    config = {"variant": args.variant, "description": VARIANTS[args.variant], "production_default_changed": False,
              "cold_required": 5, "warm_required": 20,
              "single_variable": args.variant not in ("classical_riccati", "lm_dimensionless_interaction"),
              "interaction_pre_registered": args.variant == "lm_dimensionless_interaction"}
    if args.variant == "gershgorin_lm":
        config.update({"supported": False, "status": "UNSUPPORTED_IN_LINKED_VERSION",
                       "allowed_regularize_methods": ["NO_REGULARIZE", "MIRROR", "PROJECT", "PROJECT_REDUC_HESS", "CONVEXIFY"],
                       "note": "The linked release still supports solver_options.levenberg_marquardt; see the separately executed gershgorin_derived_lm arm.",
                       "offline_spectrum_evidence": "fresh_production_config/<case>/derivative_diagnostics.json::offline_gershgorin_levenberg_marquardt"})
        write_not_run_evidence(args.output, case, config, args.execution_head,
                               "UNSUPPORTED_IN_LINKED_VERSION", "No named GERSHGORIN_* regularize_method enum")
        print(json.dumps({"variant": args.variant, "status": "UNSUPPORTED_IN_LINKED_VERSION"}))
        return 0

    lm_mu = None
    if args.variant in ("gershgorin_derived_lm", "lm_dimensionless_interaction"):
        baseline_diag = args.output.parents[2] / "fresh_production_config" / case["case_id"] / "derivative_diagnostics.json"
        baseline = json.loads(baseline_diag.read_text(encoding="utf-8"))
        lm_mu = float(baseline["qp_kkt_scaling_audit"]["offline_gershgorin_levenberg_marquardt"]["diagonal_shift"])
        config.update({"levenberg_marquardt": lm_mu, "mu_source": str(baseline_diag),
                       "mu_definition": "max(0, 1e-4 - min_stage_[R S;S^T Q]_gershgorin_lower_bound)"})

    seed_x, seed_u = starboard_seed(case) if args.variant == "starboard_seed" else p0.build_seed(case)
    params = p0.pack(case, seed_x)
    generated = args.output / "variant_generated_code"
    if generated.exists(): shutil.rmtree(generated)
    generated.mkdir()
    ocp = p0.gen.build_ocp(); mutate_ocp(ocp, args.variant, lm_mu); ocp.code_export_directory = str(generated)
    json_file = args.output / f"acados_ocp_{p0.gen.SOLVER_NAME}.json"
    try:
        build_solver = AcadosOcpSolver(ocp, json_file=str(json_file), generate=True, build=True, verbose=False)
    except Exception as exc:
        reason = f"{type(exc).__name__}: {exc}"
        write_not_run_evidence(args.output, case, config, args.execution_head, "CODEGEN_OR_BUILD_REJECT", reason)
        print(json.dumps({"case": case["case_id"], "variant": args.variant,
                          "status": "CODEGEN_OR_BUILD_REJECT", "reason": reason}, indent=2))
        return 0
    del build_solver

    def new_solver() -> AcadosOcpSolver:
        solver = AcadosOcpSolver(ocp, json_file=str(json_file), generate=False, build=False, verbose=False)
        configure_solver(solver, case, args.variant, seed_x, seed_u, params)
        return solver

    records: list[dict[str, Any]] = []
    for index in range(5):
        solver = new_solver(); records.append(solve_record(solver, "cold", index)); del solver
    warm_solver = new_solver(); primer = solve_record(warm_solver, "warm_primer", 0)
    for index in range(20): records.append(solve_record(warm_solver, "warm", index))
    try: warm_solver.dump_last_qp_to_json(filename=str(args.output / "last_qp.json"), overwrite=True)
    except Exception: pass
    xs = np.vstack([warm_solver.get(k, "x") for k in range(p0.gen.N + 1)])
    us = np.vstack([warm_solver.get(k, "u") for k in range(p0.gen.N)])
    slacks = [np.zeros(p0.gen.NT)] + [np.asarray(warm_solver.get(k, "sl"), dtype=float) for k in range(1, p0.gen.N)] + [np.zeros(p0.gen.NT)]
    warm_solver.store_iterate(filename=str(args.output / "iterate_final.json"), overwrite=True, verbose=False)

    p0.write_json(args.output / "solver_config.json", {**config, "supported": True, "solver_options": json.loads(json_file.read_text(encoding="utf-8"))["solver_options"]})
    p0.write_json(args.output / "codegen_manifest.json", {"execution_head": args.execution_head, "input_sha256": p0.sha256(args.output / "input.json"), "json_sha256": p0.sha256(json_file), "library_sha256": p0.sha256(generated / f"libacados_ocp_solver_{p0.gen.SOLVER_NAME}.so"), "dimensions": json.loads(json_file.read_text(encoding="utf-8"))["dims"]})
    p0.write_json(args.output / "seed_trajectory.json", {"kind": args.variant, "x": p0.finite(seed_x), "u": p0.finite(seed_u)})
    p0.write_json(args.output / "iterate_000.json", {"kind": "explicit_initial_iterate", "x": p0.finite(seed_x), "u": p0.finite(seed_u), "slack_init": "residual_based" if args.variant == "residual_slack_init" else "zero"})
    with (args.output / "runs.jsonl").open("w", encoding="utf-8") as stream:
        stream.write(json.dumps(primer, sort_keys=True) + "\n")
        for row in records: stream.write(json.dumps(row, sort_keys=True) + "\n")
    summary = {"primer": primer, "cold": aggregate(records, "cold"), "warm": aggregate(records, "warm"), "runs_jsonl": "runs.jsonl", "last_qp": "last_qp.json" if (args.output / "last_qp.json").exists() else None}
    p0.write_json(args.output / "qp_statistics.json", summary)
    p0.write_json(args.output / "derivative_diagnostics.json", {"status": "variant-specific derivative recomputation deferred; baseline Phase1 is source", "variant": args.variant})
    p0.write_json(args.output / "solution.json", {"final_warm": records[-1], "x": p0.finite(xs), "u": p0.finite(us), "sl": p0.finite(slacks)})
    with (args.output / "trajectory.csv").open("w", encoding="utf-8") as stream:
        stream.write("stage,t_s,x_m,y_m,psi_rad,r_rad_s,u_mps,delta_rad,n_rps\n")
        for k in range(p0.gen.N + 1):
            ctrl = us[k].tolist() if k < p0.gen.N else ["", ""]
            stream.write(",".join(map(str, [k, k * p0.gen.DT, *xs[k].tolist(), *ctrl])) + "\n")
    p0.write_json(args.output / "gnc_executability.json", {"status": "OPEN", "reason": "Independent GNC review pending"})
    p0.write_json(args.output / "verdict.json", {"case_id": case["case_id"], "variant": args.variant, "cold": summary["cold"], "warm": summary["warm"], "reference_feasibility": "OPEN", "production_changed": False})
    print(json.dumps({"case": case["case_id"], "variant": args.variant, "cold": summary["cold"], "warm": summary["warm"]}, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
