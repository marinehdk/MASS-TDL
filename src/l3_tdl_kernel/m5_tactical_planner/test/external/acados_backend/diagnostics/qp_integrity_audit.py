#!/usr/bin/env python3
"""Audit dumped uncondensed OCP-QP inputs without calling the QP solver."""

from __future__ import annotations

import argparse
import json
import math
import pathlib
from typing import Any

import numpy as np


def finite(value: Any) -> Any:
    if isinstance(value, dict):
        return {key: finite(item) for key, item in value.items()}
    if isinstance(value, list):
        return [finite(item) for item in value]
    if isinstance(value, (np.floating, float)):
        value = float(value)
        return value if math.isfinite(value) else str(value)
    if isinstance(value, (np.integer, int)):
        return int(value)
    return value


def stats(array: np.ndarray) -> dict[str, Any]:
    flat = np.abs(array.reshape(-1))
    nz = flat[flat > 0.0]
    return {"shape": list(array.shape), "finite": bool(np.all(np.isfinite(array))),
            "abs_min_nonzero": float(np.min(nz)) if nz.size else None,
            "abs_max": float(np.max(flat)) if flat.size else 0.0,
            "norm_inf": float(np.linalg.norm(array, ord=np.inf)) if array.size else 0.0}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--qp", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    data = json.loads(args.qp.read_text(encoding="utf-8"))
    nonfinite = []
    bound_order_failures = []
    soft_rows = []
    hessian = []
    category_values: dict[str, list[float]] = {key: [] for key in
        ("dynamics", "hessian", "gradient", "general_constraint", "bounds", "slack_penalty")}

    for key, value in data.items():
        array = np.asarray(value, dtype=float)
        if not np.all(np.isfinite(array)):
            nonfinite.append(key)

    for k in range(80):
        A = np.asarray(data[f"A_{k:02d}"], dtype=float)
        B = np.asarray(data[f"B_{k:02d}"], dtype=float)
        Q = np.asarray(data[f"Q_{k:02d}"], dtype=float)
        R = np.asarray(data[f"R_{k:02d}"], dtype=float)
        S = np.asarray(data[f"S_{k:02d}"], dtype=float)
        H = np.block([[R, S], [S.T, Q]])
        eig = np.linalg.eigvalsh(0.5 * (H + H.T))
        hessian.append({"stage": k, "eigen_min": float(eig[0]), "eigen_max": float(eig[-1]),
                        "negative_count": int(np.sum(eig < -1e-10)), "R": stats(R)})
        category_values["dynamics"].extend(np.abs(A).reshape(-1)); category_values["dynamics"].extend(np.abs(B).reshape(-1))
        category_values["hessian"].extend(np.abs(H).reshape(-1))
        category_values["gradient"].extend(np.abs(np.asarray(data[f"r_{k:02d}"], dtype=float)).reshape(-1))
        category_values["gradient"].extend(np.abs(np.asarray(data[f"q_{k:02d}"], dtype=float)).reshape(-1))
        for lower_name, upper_name in (("lbu", "ubu"), ("lbx", "ubx")):
            lower = np.asarray(data[f"{lower_name}_{k:02d}"], dtype=float).reshape(-1)
            upper = np.asarray(data[f"{upper_name}_{k:02d}"], dtype=float).reshape(-1)
            if lower.shape != upper.shape or np.any(lower > upper):
                bound_order_failures.append({"stage": k, "pair": [lower_name, upper_name]})
            category_values["bounds"].extend(np.abs(lower)); category_values["bounds"].extend(np.abs(upper))
        if k > 0:
            C = np.asarray(data[f"C_{k:02d}"], dtype=float)
            D = np.asarray(data[f"D_{k:02d}"], dtype=float)
            lg = np.asarray(data[f"lg_{k:02d}"], dtype=float).reshape(-1)
            ug = np.asarray(data[f"ug_{k:02d}"], dtype=float).reshape(-1)
            category_values["general_constraint"].extend(np.abs(C).reshape(-1)); category_values["general_constraint"].extend(np.abs(D).reshape(-1))
            category_values["bounds"].extend(np.abs(lg)); category_values["bounds"].extend(np.abs(ug))
            idxs = np.asarray(data[f"idxs_{k:02d}"], dtype=int).reshape(-1)
            Zl = np.asarray(data[f"Zl_{k:02d}"], dtype=float).reshape(-1)
            Zu = np.asarray(data[f"Zu_{k:02d}"], dtype=float).reshape(-1)
            zl = np.asarray(data[f"zl_{k:02d}"], dtype=float).reshape(-1)
            zu = np.asarray(data[f"zu_{k:02d}"], dtype=float).reshape(-1)
            soft_rows.append({"stage": k, "idxs": idxs.tolist(), "unique": len(set(idxs.tolist())) == len(idxs),
                              "sizes_match": len(idxs) == len(Zl) == len(Zu) == len(zl) == len(zu),
                              "positive_quadratic_penalties": bool(np.all(Zl > 0.0) and np.all(Zu > 0.0)),
                              "Zl_range": [float(np.min(Zl)), float(np.max(Zl))],
                              "Zu_range": [float(np.min(Zu)), float(np.max(Zu))],
                              "zl_range": [float(np.min(zl)), float(np.max(zl))],
                              "zu_range": [float(np.min(zu)), float(np.max(zu))]})
            for array in (Zl, Zu, zl, zu): category_values["slack_penalty"].extend(np.abs(array))

    QN = np.asarray(data["Q_80"], dtype=float)
    category_values["hessian"].extend(np.abs(QN).reshape(-1))
    scales = {}
    all_nonzero = []
    for category, values in category_values.items():
        array = np.asarray(values, dtype=float)
        nz = array[array > 0.0]
        all_nonzero.extend(nz.tolist())
        scales[category] = {"min_nonzero": float(np.min(nz)) if nz.size else None,
                            "max": float(np.max(array)) if array.size else 0.0,
                            "ratio": float(np.max(nz) / np.min(nz)) if nz.size else None}
    all_nz = np.asarray(all_nonzero)
    output = {
        "qp_dump": str(args.qp), "nonfinite_keys": nonfinite,
        "box_bound_order_failures": bound_order_failures,
        "general_constraint_bound_order": {
            "status": "UPPER_MASK_NOT_EXPORTED_NO_BOUND_ORDER_VERDICT",
            "reason": "get_last_qp exports C/D/lg/ug but not d_mask; disabled +ACADOS_INFTY upper sides can appear as ug=0 after residual masking, so lg<=ug cannot be audited from this dump alone.",
            "primary_source": "/opt/acados/acados/ocp_nlp/ocp_nlp_constraints_bgh.c:1459-1477",
        },
        "soft_constraint_contract": {
            "all_sizes_match": all(row["sizes_match"] for row in soft_rows),
            "all_indices_unique": all(row["unique"] for row in soft_rows),
            "all_quadratic_penalties_positive": all(row["positive_quadratic_penalties"] for row in soft_rows),
            "representative_stage_1": soft_rows[0], "rows": soft_rows,
        },
        "stage_hessian": {"first_indefinite": next((row for row in hessian if row["negative_count"] > 0), None),
                          "most_negative": min(hessian, key=lambda row: row["eigen_min"]),
                          "zero_R_stages": sum(row["R"]["abs_max"] <= 1e-12 for row in hessian), "rows": hessian},
        "input_scaling": {"by_category": scales,
                          "global_min_nonzero": float(np.min(all_nz)), "global_max": float(np.max(all_nz)),
                          "global_ratio": float(np.max(all_nz) / np.min(all_nz))},
        "verdict": {"all_finite": not nonfinite,
                    "box_bounds_and_soft_shapes_consistent": not bound_order_failures and all(row["sizes_match"] and row["unique"] and row["positive_quadratic_penalties"] for row in soft_rows),
                    "general_constraint_mask_verdict": "OPEN_MASK_NOT_EXPORTED",
                    "note": "Finite/shape-consistent inputs do not prove HPIPM numerical solvability; magnitude ranges remain conditioning evidence."},
    }
    args.output.write_text(json.dumps(finite(output), indent=2, sort_keys=True, allow_nan=False) + "\n", encoding="utf-8")
    print(json.dumps(output["verdict"], indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
