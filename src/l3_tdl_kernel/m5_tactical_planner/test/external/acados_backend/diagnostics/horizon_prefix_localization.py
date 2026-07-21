#!/usr/bin/env python3
"""Debug-only first failure-inducing horizon prefix and target dropout probe."""

from __future__ import annotations

import argparse
import copy
import json
import pathlib
import shutil
import sys
from typing import Any

import numpy as np
from acados_template import AcadosOcpSolver

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import run_phase0_case as p0  # noqa: E402


def run_prefix(case: dict[str, Any], horizon: int, output: pathlib.Path,
               execution_head: str) -> dict[str, Any]:
    output.mkdir(parents=True, exist_ok=True)
    generated = output / "generated_code"
    if generated.exists():
        shutil.rmtree(generated)
    generated.mkdir()
    ocp = p0.gen.build_ocp()
    ocp.dims.N = horizon
    ocp.solver_options.N_horizon = horizon
    ocp.solver_options.tf = horizon * p0.gen.DT
    ocp.solver_options.cost_scaling = np.ones(horizon + 1)
    ocp.solver_options.nlp_solver_ext_qp_res = 1
    ocp.code_export_directory = str(generated)
    json_path = output / f"acados_ocp_{p0.gen.SOLVER_NAME}.json"
    solver = AcadosOcpSolver(ocp, json_file=str(json_path), generate=True, build=True, verbose=False)
    seed_x, seed_u = p0.build_seed(case)
    params = p0.pack(case, seed_x)
    for k in range(horizon + 1):
        solver.set(k, "p", params[k]); solver.set(k, "x", seed_x[k])
        if k < horizon:
            solver.set(k, "u", seed_u[k])
    solver.constraints_set(0, "lbx", seed_x[0]); solver.constraints_set(0, "ubx", seed_x[0])
    for k in range(1, horizon):
        lo, hi = p0.bounds(case, k, "production_bounds")
        solver.constraints_set(k, "lh", lo); solver.constraints_set(k, "uh", hi)
    raw = int(solver.solve())
    stat = np.asarray(solver.get_stats("statistics"), dtype=float)
    qp_status = stat[5, :].astype(int).tolist() if stat.shape[0] > 5 else []
    try:
        solver.dump_last_qp_to_json(filename=str(output / "last_qp.json"), overwrite=True)
    except Exception:
        pass
    result = {"horizon": horizon, "time_horizon_s": horizon * p0.gen.DT,
              "target_count": len(case["targets"]), "raw_status": raw,
              "raw_semantic": {0: "SUCCESS", 1: "NAN_DETECTED", 2: "MAXITER", 3: "MINSTEP", 4: "QP_FAILURE",
                               5: "READY", 6: "UNBOUNDED", 7: "TIMEOUT"}.get(raw, "UNKNOWN"),
              "sqp_iter": int(solver.get_stats("sqp_iter")), "qp_status_history": qp_status,
              "first_failed_qp_column": next((i for i, status in enumerate(qp_status) if status not in (0, 2)), None),
              "input_sha256": p0.sha256(output / "input.json") if (output / "input.json").exists() else None,
              "execution_head": execution_head, "production_changed": False}
    p0.write_json(output / "result.json", result)
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--case", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--execution-head", required=True)
    parser.add_argument("--reuse-existing", action="store_true")
    args = parser.parse_args(); args.output.mkdir(parents=True, exist_ok=True)
    case = json.loads(args.case.read_text(encoding="utf-8"))
    results = []
    first_raw4 = None
    for horizon in range(1, p0.gen.N + 1):
        out = args.output / "active_target" / f"N_{horizon:02d}"
        out.mkdir(parents=True, exist_ok=True)
        if args.reuse_existing and (out / "result.json").exists():
            row = json.loads((out / "result.json").read_text(encoding="utf-8"))
        else:
            shutil.copyfile(args.case, out / "input.json")
            row = run_prefix(case, horizon, out, args.execution_head)
        results.append(row)
        if row["raw_status"] == 4:
            first_raw4 = horizon
            break

    dropout = None
    if first_raw4 is not None and case["targets"]:
        no_target = copy.deepcopy(case)
        no_target["case_id"] = case["case_id"] + "__target_dropout"
        no_target["targets"] = []
        dropout_dir = args.output / "target_dropout" / f"N_{first_raw4:02d}"
        dropout_dir.mkdir(parents=True, exist_ok=True)
        if args.reuse_existing and (dropout_dir / "result.json").exists():
            dropout = json.loads((dropout_dir / "result.json").read_text(encoding="utf-8"))
        else:
            p0.write_json(dropout_dir / "input.json", no_target)
            dropout = run_prefix(no_target, first_raw4, dropout_dir, args.execution_head)

    if dropout is None:
        target_localization = "OPEN"
    elif dropout["raw_status"] != 4:
        target_localization = "TARGET_0_CONFIRMED_FOR_QP_NAN_FAILURE_MODE"
    else:
        target_localization = "SHARED_NON_TARGET_QP_NAN_AT_SAME_HORIZON"

    verdict = {"case_id": case["case_id"], "scan": results,
               "first_failure_inducing_included_horizon": first_raw4,
               "first_failure_inducing_added_path_stage": None if first_raw4 is None else first_raw4 - 1,
               "changed_terminal_stage": first_raw4,
               "target_dropout_at_same_horizon": dropout,
               "terminology": "This is a failure-inducing included horizon/stage bound, not an HPIPM internal failing stage status.",
               "target_localization": target_localization}
    p0.write_json(args.output / "prefix_localization.json", verdict)
    print(json.dumps(verdict, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
