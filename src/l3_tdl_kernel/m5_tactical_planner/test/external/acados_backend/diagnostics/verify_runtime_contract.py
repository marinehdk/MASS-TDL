#!/usr/bin/env python3
"""Fail-closed acados status/dimension/artifact contract diagnostic.

This is test-only tooling.  It reads the exact header and shared objects used by
the diagnostic container and compares their status enum with the production
wrapper's switch.  A mismatch is an expected test failure until production is
changed under a separately approved decision gate.
"""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import pathlib
import re
import subprocess
import sys
from typing import Any


EXPECTED_ENUM = {
    "ACADOS_SUCCESS": 0,
    "ACADOS_NAN_DETECTED": 1,
    "ACADOS_MAXITER": 2,
    "ACADOS_MINSTEP": 3,
    "ACADOS_QP_FAILURE": 4,
    "ACADOS_READY": 5,
    "ACADOS_UNBOUNDED": 6,
    "ACADOS_TIMEOUT": 7,
}

# MidMpcSolution::Status integer values, from common/types.hpp.
# Diagnostic policy proposal for every raw status in the linked enum.
# Only EXPECTED_ENUM above is an upstream semantic fact. This policy preserves
# MidMpcSolution's available categories without claiming an official mapping.
PROPOSED_WRAPPER_MAP = {
    0: "Converged",
    1: "NumericalFailure",  # NaN detected
    2: "Timeout",           # maximum iterations
    3: "NumericalFailure",  # minimum step
    4: "NumericalFailure",  # QP failure
    5: "NumericalFailure",  # READY is not a solved trajectory
    6: "NumericalFailure",  # unbounded has no dedicated MidMpc status
    7: "Timeout",           # explicit linked timeout status
}


def sha256(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def read_enum(path: pathlib.Path) -> dict[str, int]:
    text = path.read_text(encoding="utf-8")
    return {
        name: int(value)
        for name, value in re.findall(r"\b(ACADOS_[A-Z_]+)\s*=\s*(-?\d+)", text)
    }


def read_wrapper_map(path: pathlib.Path) -> dict[int, str]:
    text = path.read_text(encoding="utf-8")
    match = re.search(
        r"MidMpcSolution::Status\s+map_acados_status\s*\([^)]*\)\s*\{(.*?)\n\}",
        text,
        flags=re.DOTALL,
    )
    if match is None:
        raise RuntimeError("map_acados_status body not found")
    result: dict[int, str] = {}
    for raw, mapped in re.findall(
        r"case\s+(-?\d+)\s*:\s*return\s+MidMpcSolution::Status::(\w+)",
        match.group(1),
    ):
        result[int(raw)] = mapped
    default_match = re.search(
        r"default\s*:\s*return\s+MidMpcSolution::Status::(\w+)", match.group(1)
    )
    if default_match is not None:
        for raw in range(8):
            result.setdefault(raw, default_match.group(1))
    return result


def read_macros(path: pathlib.Path) -> dict[str, int]:
    text = path.read_text(encoding="utf-8")
    wanted = {
        "M5_MID_MPC_ACADOS_N",
        "M5_MID_MPC_ACADOS_NX",
        "M5_MID_MPC_ACADOS_NU",
        "M5_MID_MPC_ACADOS_NP",
        "M5_MID_MPC_ACADOS_NH",
        "M5_MID_MPC_ACADOS_NSH",
    }
    values = {
        name: int(value)
        for name, value in re.findall(r"^#define\s+(\w+)\s+(\d+)\s*$", text, re.MULTILINE)
        if name in wanted
    }
    return values


def run(command: list[str]) -> str:
    return subprocess.run(command, check=True, text=True, capture_output=True).stdout.strip()


def casadi_parameter_sparsity(shared_library: pathlib.Path) -> dict[str, dict[str, Any]]:
    library = ctypes.CDLL(str(shared_library))
    probes = {
        "dynamics": ("m5_mid_mpc_acados_dyn_disc_phi_fun", 2),
        "constraint": ("m5_mid_mpc_acados_constr_h_fun", 3),
        "stage_cost": ("m5_mid_mpc_acados_cost_ext_cost_fun", 3),
    }
    result: dict[str, dict[str, Any]] = {}
    for label, (stem, parameter_index) in probes.items():
        n_in = getattr(library, f"{stem}_n_in")
        n_in.restype = ctypes.c_int
        sparsity = getattr(library, f"{stem}_sparsity_in")
        sparsity.argtypes = [ctypes.c_int]
        sparsity.restype = ctypes.POINTER(ctypes.c_int)
        shape = sparsity(parameter_index)
        result[label] = {
            "symbol": f"{stem}_sparsity_in",
            "n_in": n_in(),
            "parameter_input_index": parameter_index,
            "parameter_shape": [shape[0], shape[1]],
        }
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--acados-root", type=pathlib.Path, default=pathlib.Path("/opt/acados"))
    parser.add_argument("--acados-install", type=pathlib.Path, default=pathlib.Path("/usr/local"))
    args = parser.parse_args()

    backend = args.repo / "src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_backend"
    generated = backend / "c_generated_code"
    enum_header = args.acados_install / "include/acados/utils/types.h"
    hpipm_header = args.acados_install / "include/hpipm/include/hpipm_common.h"
    hpipm_adapter = args.acados_root / "acados/dense_qp/dense_qp_hpipm.c"
    acados_lib = args.acados_install / "lib/libacados.so"
    generated_header = generated / "acados_solver_m5_mid_mpc_acados.h"
    generated_lib = generated / "libacados_ocp_solver_m5_mid_mpc_acados.so"
    generated_json = generated / "acados_ocp_m5_mid_mpc_acados.json"
    wrapper = args.repo / "src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_acados_solver.cpp"

    runtime_enum = read_enum(enum_header)
    wrapper_map = read_wrapper_map(wrapper)
    enum_ok = all(runtime_enum.get(name) == value for name, value in EXPECTED_ENUM.items())
    map_mismatches = {
        str(raw): {"expected": expected, "actual": wrapper_map.get(raw)}
        for raw, expected in PROPOSED_WRAPPER_MAP.items()
        if wrapper_map.get(raw) != expected
    }

    ocp_json: dict[str, Any] = json.loads(generated_json.read_text(encoding="utf-8"))
    dims = ocp_json["dims"]
    generated_macros = read_macros(generated_header)
    # Step5 方案 B (VR-01 final): expected dims reflect the post-codegen layout.
    # nh stays 20 (target count Nt=16 unchanged); nsh drops 16 -> 0 (no slack;
    # CPA row is a TRUE hard floor, residual reads cpa_hard_m from G_CPA_HARD);
    # np grows 210 -> 211 (155 global = 26 head + 128 target + 1 cpa_hard,
    # plus 56 per-stage). The in-tree generated header is STALE until codegen
    # is re-run; this test will fail against the stale header (NSH=16, NP=210)
    # and pass after `python3 gen_mid_mpc_acados.py` is re-run.
    expected_generated_dims = {
        "N": 80,
        "nx": 5,
        "nu": 2,
        "np_global": 0,
        "np": 211,
        "nh": 20,
        "nsh": 0,
    }
    actual_generated_dims = {
        "N": dims["N"],
        "nx": dims["nx"],
        "nu": dims["nu"],
        "np_global": dims.get("np_global", 0),
        "np": dims["np"],
        "nh": dims["nh"],
        "nsh": dims["nsh"],
    }
    logical_partition = {"application_np_global": 155, "application_np_stage": 56,
                         "generated_flat_np": dims["np"]}
    runtime_parameter_sparsity = casadi_parameter_sparsity(generated_lib)
    sparsity_ok = all(item["parameter_shape"] == [211, 1]
                      for item in runtime_parameter_sparsity.values())
    dimension_ok = (actual_generated_dims == expected_generated_dims and
                    logical_partition["application_np_global"] + logical_partition["application_np_stage"] == dims["np"] and
                    sparsity_ok)

    acados_commit = run(["git", "-C", str(args.acados_root), "rev-parse", "HEAD"])
    acados_describe = run(["git", "-C", str(args.acados_root), "describe", "--tags", "--always", "--dirty"])
    linked = run(["ldd", str(generated_lib)])
    qpscaling_hits = run([
        "bash", "-lc",
        f"grep -R -l -i qpscaling {args.acados_root}/acados {args.acados_root}/interfaces "
        "2>/dev/null || true",
    ]).splitlines()
    selected_backend_qpscaling_hits: list[str] = []

    report: dict[str, Any] = {
        "test": "acados_runtime_status_dimension_artifact_contract",
        "pass": enum_ok and dimension_ok and not map_mismatches,
        "runtime": {
            "enum_header": str(enum_header),
            "enum_header_sha256": sha256(enum_header),
            "enum": runtime_enum,
            "hpipm_status": {
                "header": str(hpipm_header),
                "header_sha256": sha256(hpipm_header),
                "enum_order": ["SUCCESS=0", "MAX_ITER=1", "MIN_STEP=2", "NAN_SOL=3", "INCONS_EQ=4"],
                "adapter_source": str(hpipm_adapter),
                "adapter_source_sha256": sha256(hpipm_adapter),
                "adapter_semantic": "Only 0/1/2 remapped; HPIPM NAN_SOL=3 propagates as QP status 3",
            },
            "acados_commit": acados_commit,
            "acados_describe": acados_describe,
            "libacados": str(acados_lib),
            "libacados_sha256": sha256(acados_lib),
            "generated_solver_ldd": linked.splitlines(),
            "qpscaling_status_api": {
                "linked_source_any": bool(qpscaling_hits),
                "linked_source_hits": qpscaling_hits,
                "selected_backend": "FULL_CONDENSING_HPIPM",
                "selected_backend_supported": False,
                "selected_backend_hits": selected_backend_qpscaling_hits,
                "selected_backend_value": None,
                "selected_backend_status": "UNAVAILABLE_IN_LINKED_VERSION",
                "note": "Source-text hits outside the selected backend, including OSQP, do not expose a FULL_CONDENSING_HPIPM qpscaling status API.",
            },
        },
        "production_wrapper": {
            "source": str(wrapper),
            "source_sha256": sha256(wrapper),
            "actual_raw_map": {str(k): v for k, v in sorted(wrapper_map.items())},
            "proposed_raw_map_policy": {str(k): v for k, v in sorted(PROPOSED_WRAPPER_MAP.items())},
            "mismatches": map_mismatches,
        },
        "codegen": {
            "json": str(generated_json),
            "json_sha256": sha256(generated_json),
            "header": str(generated_header),
            "header_sha256": sha256(generated_header),
            "shared_library": str(generated_lib),
            "shared_library_sha256": sha256(generated_lib),
            "header_macros": generated_macros,
            "expected_generated_dimensions": expected_generated_dims,
            "actual_generated_dimensions": actual_generated_dims,
            "application_logical_parameter_partition": logical_partition,
            "runtime_casadi_parameter_sparsity": runtime_parameter_sparsity,
            "runtime_casadi_parameter_sparsity_pass": sparsity_ok,
            "dimension_contract_pass": dimension_ok,
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    sys.exit(main())
