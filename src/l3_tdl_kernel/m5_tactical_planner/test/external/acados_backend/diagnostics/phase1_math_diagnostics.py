#!/usr/bin/env python3
"""Phase-1 derivative, Hessian, condensed-Jacobian, and QP/KKT diagnostics."""

from __future__ import annotations

import argparse
import json
import math
import pathlib
import sys
from typing import Any

import casadi as ca
import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import run_phase0_case as p0  # noqa: E402


def eig_summary(matrix: np.ndarray, tol: float = 1.0e-10) -> dict[str, Any]:
    sym = 0.5 * (matrix + matrix.T)
    values = np.linalg.eigvalsh(sym)
    abs_values = np.abs(values)
    nonzero = abs_values[abs_values > tol]
    return {
        "shape": list(matrix.shape),
        "finite": bool(np.all(np.isfinite(matrix))),
        "symmetry_inf": float(np.max(np.abs(matrix - matrix.T))),
        "eigen_min": float(values[0]),
        "eigen_max": float(values[-1]),
        "negative_count": int(np.sum(values < -tol)),
        "near_zero_count": int(np.sum(abs_values <= tol)),
        "abs_condition_nonzero": float(np.max(nonzero) / np.min(nonzero)) if nonzero.size else None,
    }


def fd_h(x: np.ndarray, tx: float, ty: float, radius: float, eps: float) -> tuple[float, np.ndarray, np.ndarray]:
    def value(z: np.ndarray) -> float:
        return float((z[0] - tx) ** 2 + (z[1] - ty) ** 2 - radius ** 2)
    n = len(x)
    grad = np.zeros(n)
    hess = np.zeros((n, n))
    f0 = value(x)
    for i in range(n):
        ei = np.zeros(n); ei[i] = eps
        grad[i] = (value(x + ei) - value(x - ei)) / (2.0 * eps)
        hess[i, i] = (value(x + ei) - 2.0 * f0 + value(x - ei)) / (eps * eps)
        for j in range(i):
            ej = np.zeros(n); ej[j] = eps
            mixed = (value(x + ei + ej) - value(x + ei - ej) - value(x - ei + ej) + value(x - ei - ej)) / (4.0 * eps * eps)
            hess[i, j] = hess[j, i] = mixed
    return f0, grad, hess


def collision_functions(symbol_type: str) -> tuple[ca.Function, ca.Function]:
    sym = ca.SX.sym if symbol_type == "SX" else ca.MX.sym
    pos = sym(f"pos_{symbol_type}", 2)
    prm = sym(f"prm_{symbol_type}", 6)  # tx,ty,sigma,cpa,range_weight,intent
    tx, ty, sigma, cpa, weight, intent = [prm[i] for i in range(6)]
    scale = p0.gen.W_COLREG / p0.gen.NT * weight * (1.0 + (1.0 - intent))

    def barrier(dx: Any, dy: Any) -> Any:
        return ca.exp(-p0.gen.ZETA * (ca.sqrt(dx * dx + dy * dy + p0.gen.K_SQRT_GUARD) - cpa))

    deterministic = scale * barrier(pos[0] - tx, pos[1] - ty)
    edge = (1.0 - 1.0e-3) / 4.0
    robust = scale * (
        1.0e-3 * barrier(pos[0] - tx, pos[1] - ty)
        + edge * barrier(pos[0] - (tx + sigma), pos[1] - ty)
        + edge * barrier(pos[0] - (tx - sigma), pos[1] - ty)
        + edge * barrier(pos[0] - tx, pos[1] - (ty + sigma))
        + edge * barrier(pos[0] - tx, pos[1] - (ty - sigma))
    )
    outputs = []
    for expr in (deterministic, robust):
        outputs += [expr, ca.gradient(expr, pos), ca.hessian(expr, pos)[0]]
    return (
        ca.Function(f"collision_{symbol_type}", [pos, prm], outputs),
        ca.Function(f"cpa_h_{symbol_type}", [pos, prm], [
            (pos[0] - tx) ** 2 + (pos[1] - ty) ** 2 - cpa ** 2,
            ca.gradient((pos[0] - tx) ** 2 + (pos[1] - ty) ** 2 - cpa ** 2, pos),
            ca.hessian((pos[0] - tx) ** 2 + (pos[1] - ty) ** 2 - cpa ** 2, pos)[0],
        ]),
    )


def dynamics_jacobians(seed_x: np.ndarray, seed_u: np.ndarray) -> tuple[list[np.ndarray], list[np.ndarray], list[np.ndarray]]:
    x = ca.SX.sym("x", 5); u = ca.SX.sym("u", 2)
    f = ca.vertcat(
        x[0] + x[4] * p0.gen.DT * ca.cos(x[2]),
        x[1] + x[4] * p0.gen.DT * ca.sin(x[2]),
        x[2] + p0.gen.DT * x[3],
        x[3] + p0.gen.DT * p0.gen.C_U * u[0],
        x[4] + p0.gen.DT * (p0.gen.K_PROP_PER_MASS * u[1] ** 2 - p0.gen.K_DRAG_PER_MASS * x[4] ** 2),
    )
    fn = ca.Function("dyn_jac", [x, u], [ca.jacobian(f, x), ca.jacobian(f, u)])
    As, Bs = [], []
    n_control = p0.gen.N * 2
    G = np.zeros((5, n_control))
    Gs = [G.copy()]
    for k in range(p0.gen.N):
        A, B = fn(seed_x[k], seed_u[k])
        A, B = np.asarray(A, dtype=float), np.asarray(B, dtype=float)
        As.append(A); Bs.append(B)
        G = A @ G
        G[:, 2 * k:2 * k + 2] += B
        Gs.append(G.copy())
    return As, Bs, Gs


def target_at(case: dict[str, Any], target: dict[str, Any], k: int) -> tuple[float, float]:
    t = min(k, p0.gen.N - 1) * p0.gen.DT
    return (
        target["x_m"] + target["sog_mps"] * math.cos(target["cog_rad"]) * t,
        target["y_m"] + target["sog_mps"] * math.sin(target["cog_rad"]) * t,
    )


def derivative_audit(case: dict[str, Any]) -> dict[str, Any]:
    seed_x, seed_u = p0.build_seed(case)
    _, _, Gs = dynamics_jacobians(seed_x, seed_u)
    sx_collision, sx_h = collision_functions("SX")
    mx_collision, mx_h = collision_functions("MX")
    cpa_rows, condensed_rows, collision_rows = [], [], []
    parity_max = {"cpa_value": 0.0, "cpa_jacobian": 0.0, "cpa_hessian": 0.0,
                  "collision_value": 0.0, "collision_jacobian": 0.0, "collision_hessian": 0.0}
    for target_index, target in enumerate(case["targets"]):
        weight_span = max(p0.gen.PWT_OUTER_M - case["constraints"]["cpa_safe_m"], 1.0)
        initial_range = math.hypot(target["x_m"] - case["own_ship"]["x_m"], target["y_m"] - case["own_ship"]["y_m"])
        weight = min(1.0, max(0.0, (p0.gen.PWT_OUTER_M - initial_range) / weight_span))
        for k in range(p0.gen.N + 1):
            tx, ty = target_at(case, target, k)
            sig = p0.sigma_pos(target, min(k, p0.gen.N - 1) * p0.gen.DT)
            prm = np.array([tx, ty, sig, case["constraints"]["cpa_safe_m"], weight, target.get("intent_confidence", 0.5)])
            pos = seed_x[k, :2]
            sxhv = sx_h(pos, prm); mxhv = mx_h(pos, prm)
            exact_value = float(sxhv[0]); exact_grad2 = np.asarray(sxhv[1], dtype=float).reshape(2); exact_hess2 = np.asarray(sxhv[2], dtype=float)
            fd_value, fd_grad, fd_hess = fd_h(seed_x[k], tx, ty, prm[3], 1.0e-2)
            analytic_grad = np.array([exact_grad2[0], exact_grad2[1], 0.0, 0.0, 0.0])
            analytic_hess = np.zeros((5, 5)); analytic_hess[:2, :2] = exact_hess2
            cpa_rows.append({
                "stage": k, "target": target_index, "value": exact_value,
                "relative_xy": [float(pos[0] - tx), float(pos[1] - ty)],
                "analytic_jacobian_state": p0.finite(analytic_grad),
                "fd_jacobian_state": p0.finite(fd_grad),
                "jacobian_fd_error_inf": float(np.max(np.abs(analytic_grad - fd_grad))),
                "hessian_fd_error_inf": float(np.max(np.abs(analytic_hess - fd_hess))),
            })
            control_grad = analytic_grad @ Gs[k]
            nonzero = np.where(np.abs(control_grad) > 1.0e-10)[0]
            condensed_rows.append({
                "stage": k, "target": target_index,
                "state_jacobian_norm": float(np.linalg.norm(analytic_grad)),
                "control_jacobian_norm": float(np.linalg.norm(control_grad)),
                "control_jacobian_rank": int(np.linalg.matrix_rank(control_grad.reshape(1, -1), tol=1.0e-10)),
                "first_nonzero_control_component": int(nonzero[0]) if nonzero.size else None,
                "nonzero_control_count": int(nonzero.size),
                "both_relative_components_near_zero": bool(abs(pos[0] - tx) < 1.0e-8 and abs(pos[1] - ty) < 1.0e-8),
            })
            sx = sx_collision(pos, prm); mx = mx_collision(pos, prm)
            names = ("deterministic", "p7_sigma")
            stage_cost = {"stage": k, "target": target_index, "sigma_pos_m": sig}
            for j, name in enumerate(names):
                value = float(sx[3 * j]); grad = np.asarray(sx[3 * j + 1], dtype=float).reshape(2); hess = np.asarray(sx[3 * j + 2], dtype=float)
                stage_cost[name] = {"value": value, "gradient": p0.finite(grad), **eig_summary(hess)}
                parity_max["collision_value"] = max(parity_max["collision_value"], abs(value - float(mx[3 * j])))
                parity_max["collision_jacobian"] = max(parity_max["collision_jacobian"], float(np.max(np.abs(grad - np.asarray(mx[3 * j + 1], dtype=float).reshape(2)))))
                parity_max["collision_hessian"] = max(parity_max["collision_hessian"], float(np.max(np.abs(hess - np.asarray(mx[3 * j + 2], dtype=float)))))
            collision_rows.append(stage_cost)
            parity_max["cpa_value"] = max(parity_max["cpa_value"], abs(exact_value - float(mxhv[0])))
            parity_max["cpa_jacobian"] = max(parity_max["cpa_jacobian"], float(np.max(np.abs(exact_grad2 - np.asarray(mxhv[1], dtype=float).reshape(2)))))
            parity_max["cpa_hessian"] = max(parity_max["cpa_hessian"], float(np.max(np.abs(exact_hess2 - np.asarray(mxhv[2], dtype=float)))))
    return {
        "cpa_finite_difference": {
            "max_jacobian_error_inf": max(row["jacobian_fd_error_inf"] for row in cpa_rows),
            "max_hessian_error_inf": max(row["hessian_fd_error_inf"] for row in cpa_rows),
            "rows": cpa_rows,
        },
        "condensed_control_jacobian": {
            "interpretation": "column 2*j=delta_j, 2*j+1=n_j; rank is for scalar h row",
            "rows": condensed_rows,
        },
        "collision_hessian": {
            "first_negative_deterministic": next((row for row in collision_rows if row["deterministic"]["negative_count"] > 0), None),
            "first_negative_p7_sigma": next((row for row in collision_rows if row["p7_sigma"]["negative_count"] > 0), None),
            "most_negative_deterministic": min(collision_rows, key=lambda row: row["deterministic"]["eigen_min"]),
            "most_negative_p7_sigma": min(collision_rows, key=lambda row: row["p7_sigma"]["eigen_min"]),
            "rows": collision_rows,
        },
        "mx_sx_second_order_parity": {"max_abs_errors": parity_max, "pass_tol_1e-10": all(value <= 1.0e-10 for value in parity_max.values())},
    }


def qp_audit(path: pathlib.Path) -> dict[str, Any]:
    data = json.loads(path.read_text(encoding="utf-8"))
    block_rows = []
    for k in range(p0.gen.N):
        Q = np.asarray(data[f"Q_{k:02d}"], dtype=float)
        R = np.asarray(data[f"R_{k:02d}"], dtype=float)
        S = np.asarray(data[f"S_{k:02d}"], dtype=float)
        H = np.block([[R, S], [S.T, Q]])
        stage_gershgorin_lower = float(np.min(np.diag(H) - (np.sum(np.abs(H), axis=1) - np.abs(np.diag(H)))))
        block_rows.append({"stage": k, "Q": eig_summary(Q), "R": eig_summary(R),
                           "RSQ_block": eig_summary(H), "RSQ_gershgorin_lower_bound": stage_gershgorin_lower})
    QN = np.asarray(data[f"Q_{p0.gen.N:02d}"], dtype=float)

    nU = 2 * p0.gen.N
    Gs = [np.zeros((5, nU))]
    for k in range(p0.gen.N):
        A = np.asarray(data[f"A_{k:02d}"], dtype=float)
        B = np.asarray(data[f"B_{k:02d}"], dtype=float)
        G = A @ Gs[-1]
        G[:, 2 * k:2 * k + 2] += B
        Gs.append(G)
    Hc = np.zeros((nU, nU))
    for k in range(p0.gen.N):
        Q = np.asarray(data[f"Q_{k:02d}"], dtype=float)
        R = np.asarray(data[f"R_{k:02d}"], dtype=float)
        S = np.asarray(data[f"S_{k:02d}"], dtype=float)
        E = np.zeros((2, nU)); E[:, 2 * k:2 * k + 2] = np.eye(2)
        G = Gs[k]
        Hc += G.T @ Q @ G + E.T @ R @ E + E.T @ S @ G + G.T @ S.T @ E
    Hc += Gs[-1].T @ QN @ Gs[-1]
    condensed_gershgorin_lower = float(np.min(np.diag(Hc) - (np.sum(np.abs(Hc), axis=1) - np.abs(np.diag(Hc)))))
    stage_gershgorin_lower = min(row["RSQ_gershgorin_lower_bound"] for row in block_rows)
    glm_shift = max(0.0, 1.0e-4 - stage_gershgorin_lower)

    constraint_rows = []
    impossible = []
    for k in range(1, p0.gen.N):
        C = np.asarray(data[f"C_{k:02d}"], dtype=float)
        D = np.asarray(data[f"D_{k:02d}"], dtype=float)
        E = np.zeros((2, nU)); E[:, 2 * k:2 * k + 2] = np.eye(2)
        J = C @ Gs[k] + D @ E
        lower = np.asarray(data[f"lg_{k:02d}"], dtype=float).reshape(-1)
        upper = np.asarray(data[f"ug_{k:02d}"], dtype=float).reshape(-1)
        for row in (2, 18, 19):
            item = {"stage": k, "row": row, "name": "cpa_target_0" if row == 2 else "direction" if row == 18 else "min_alt", "condensed_norm": float(np.linalg.norm(J[row])), "nonzero_count": int(np.sum(np.abs(J[row]) > 1.0e-10)), "linearized_lower": float(lower[row]), "linearized_upper": float(upper[row])}
            constraint_rows.append(item)
            if item["condensed_norm"] <= 1.0e-12 and item["linearized_lower"] > 1.0e-12:
                impossible.append(item)

    numeric_values = []
    nonfinite_keys = []
    for key, value in data.items():
        array = np.asarray(value, dtype=float)
        if not np.all(np.isfinite(array)):
            nonfinite_keys.append(key)
        numeric_values.extend(np.abs(array).reshape(-1).tolist())
    nz = np.asarray([value for value in numeric_values if value > 0.0])
    return {
        "path": str(path),
        "nonfinite_keys": nonfinite_keys,
        "absolute_scale": {"min_nonzero": float(np.min(nz)), "max": float(np.max(nz)), "ratio": float(np.max(nz) / np.min(nz))},
        "stage_hessian_blocks": {
            "first_indefinite_Q": next((row for row in block_rows if row["Q"]["negative_count"] > 0), None),
            "most_negative_Q": min(block_rows, key=lambda row: row["Q"]["eigen_min"]),
            "indefinite_Q_stage_count": sum(row["Q"]["negative_count"] > 0 for row in block_rows),
            "zero_R_stage_count": sum(abs(row["R"]["eigen_max"]) <= 1.0e-12 for row in block_rows),
            "rows": block_rows,
        },
        "reconstructed_full_condensed_control_hessian": eig_summary(Hc),
        "offline_gershgorin_levenberg_marquardt": {
            "named_gershgorin_regularize_method_supported": False,
            "fixed_levenberg_marquardt_option_supported": True,
            "reason": "linked v0.4.4 has no GERSHGORIN_* regularize_method enum, but does expose solver_options.levenberg_marquardt",
            "epsilon": 1.0e-4,
            "stage_rsq_gershgorin_lower_bound_before": stage_gershgorin_lower,
            "condensed_control_gershgorin_lower_bound_sensitivity_only": condensed_gershgorin_lower,
            "diagonal_shift": glm_shift,
            "condensed_hessian_after_same_numeric_shift_sensitivity_only": eig_summary(Hc + glm_shift * np.eye(nU)),
            "mu_scope": "acados applies LM before condensing to stage QP Hessian blocks; the condensed bound is not an equivalent LM value",
            "application": "phase2/gershgorin_derived_lm applies this fixed diagonal shift as the only changed solver option",
        },
        "condensed_constraint_rows": {
            "first_impossible_positive_lower_zero_jacobian": impossible[0] if impossible else None,
            "impossible_rows": impossible,
            "rows": constraint_rows,
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--case", type=pathlib.Path, required=True)
    parser.add_argument("--qp", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    case = json.loads(args.case.read_text(encoding="utf-8"))
    output = {
        "case_id": case["case_id"],
        "input_sha256": p0.sha256(args.case),
        "formula": "h=(x-tx)^2+(y-ty)^2-R_safe^2; dh/du=(dh/dx)(dx/du)",
        **derivative_audit(case),
        "qp_kkt_scaling_audit": qp_audit(args.qp),
    }
    p0.write_json(args.output, output)
    summary = {
        "case_id": case["case_id"],
        "fd_jac_error": output["cpa_finite_difference"]["max_jacobian_error_inf"],
        "fd_hess_error": output["cpa_finite_difference"]["max_hessian_error_inf"],
        "mx_sx_pass": output["mx_sx_second_order_parity"]["pass_tol_1e-10"],
        "first_indefinite_Q": output["qp_kkt_scaling_audit"]["stage_hessian_blocks"]["first_indefinite_Q"],
        "condensed_hessian": output["qp_kkt_scaling_audit"]["reconstructed_full_condensed_control_hessian"],
        "first_impossible_constraint": output["qp_kkt_scaling_audit"]["condensed_constraint_rows"]["first_impossible_positive_lower_zero_jacobian"],
    }
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
