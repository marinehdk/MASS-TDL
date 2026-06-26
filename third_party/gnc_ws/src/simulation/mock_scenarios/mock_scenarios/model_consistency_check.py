import argparse
import math
from pathlib import Path
from typing import Dict, Iterable, List, Tuple

import yaml


Issue = Tuple[str, str]


def load_yaml(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as stream:
        return yaml.safe_load(stream) or {}


def deep_get(data: dict, keys: Iterable[str], default=None):
    value = data
    for key in keys:
        if not isinstance(value, dict) or key not in value:
            return default
        value = value[key]
    return value


def as_float(data: dict, key: str, default: float = 0.0) -> float:
    try:
        return float(data.get(key, default))
    except (TypeError, ValueError):
        return default


def as_bool(data: dict, key: str, default: bool = False) -> bool:
    value = data.get(key, default)
    if isinstance(value, bool):
        return value
    if isinstance(value, str):
        return value.lower() in ("1", "true", "yes", "on")
    return bool(value)


def force_vector(x: float, y: float, angle: float, efficiency: float = 1.0) -> Tuple[float, float, float]:
    fx = math.cos(angle) * efficiency
    fy = math.sin(angle) * efficiency
    mz = (x * fy) - (y * fx)
    return fx, fy, mz


def diff_norm(a: Tuple[float, float, float], b: Tuple[float, float, float]) -> float:
    return math.sqrt(sum((ai - bi) ** 2 for ai, bi in zip(a, b)))


def fmt_vec(v: Tuple[float, float, float]) -> str:
    return f"({v[0]:+.4f}, {v[1]:+.4f}, {v[2]:+.4f})"


def add(issues: List[Issue], severity: str, message: str) -> None:
    issues.append((severity, message))


def check_config_consistency(config: dict) -> List[Issue]:
    issues: List[Issue] = []
    dyn_thrusters = deep_get(config, ["ship_dynamics_node", "ros__parameters", "thrusters"], {})
    alloc_params = deep_get(config, ["thrust_allocation_node", "ros__parameters"], {})

    dyn_names = list(dyn_thrusters.get("thruster_names", []) or [])
    alloc_names = list(alloc_params.get("thruster_names", []) or [])
    dyn_num = dyn_thrusters.get("num")

    if dyn_num is not None and int(dyn_num) != len(dyn_names):
        add(
            issues,
            "ERROR",
            f"ship_dynamics thrusters.num={dyn_num}, but thruster_names has {len(dyn_names)} entries: {dyn_names}",
        )

    if dyn_names != alloc_names:
        add(
            issues,
            "ERROR",
            f"ship_dynamics and thrust_allocation thruster_names differ: dynamics={dyn_names}, allocation={alloc_names}",
        )

    for name in dyn_names:
        if name not in dyn_thrusters:
            add(issues, "ERROR", f"ship_dynamics missing config block for thruster '{name}'")
        if name not in alloc_params:
            add(issues, "ERROR", f"thrust_allocation missing config block for thruster '{name}'")

    return issues


def check_thruster_model_consistency(config: dict, tolerance: float) -> List[Issue]:
    issues: List[Issue] = []
    dyn_thrusters = deep_get(config, ["ship_dynamics_node", "ros__parameters", "thrusters"], {})
    alloc_params = deep_get(config, ["thrust_allocation_node", "ros__parameters"], {})
    names = list(dyn_thrusters.get("thruster_names", []) or [])

    for name in names:
        dcfg = dyn_thrusters.get(name, {})
        acfg = alloc_params.get(name, {})
        if not dcfg or not acfg:
            continue

        for key in ("x", "y", "angle_default", "is_azimuth", "max_thrust_kN"):
            dv = dcfg.get(key)
            av = acfg.get(key)
            if dv != av:
                add(issues, "WARN", f"{name}: '{key}' differs between dynamics ({dv}) and allocation ({av})")

        x = as_float(acfg, "x")
        y = as_float(acfg, "y")
        angle = as_float(acfg, "angle_default")
        efficiency = as_float(dcfg, "efficiency", 1.0)
        is_azimuth = as_bool(acfg, "is_azimuth")
        is_rudder = as_bool(acfg, "is_rudder") or name in ("rudder", "r1", "r2")

        if is_rudder:
            add(
                issues,
                "WARN",
                f"{name}: rudder uses a linear force-per-radian allocation model; validate it against the nonlinear dynamics wash/stall model.",
            )
            continue

        if is_azimuth:
            alloc_fx_col = (1.0, 0.0, -y)
            dyn_fx_col = force_vector(x, y, 0.0, efficiency)
            alloc_fy_col = (0.0, 1.0, x)
            dyn_fy_col = force_vector(x, y, math.pi / 2.0, efficiency)
            if diff_norm(alloc_fx_col, dyn_fx_col) > tolerance:
                add(
                    issues,
                    "ERROR",
                    f"{name}: azimuth Fx column mismatch allocation={fmt_vec(alloc_fx_col)} dynamics={fmt_vec(dyn_fx_col)}",
                )
            if diff_norm(alloc_fy_col, dyn_fy_col) > tolerance:
                add(
                    issues,
                    "ERROR",
                    f"{name}: azimuth Fy column mismatch allocation={fmt_vec(alloc_fy_col)} dynamics={fmt_vec(dyn_fy_col)}",
                )
        else:
            alloc_col = force_vector(x, y, angle, 1.0)
            dyn_col = force_vector(x, y, angle, efficiency)
            if diff_norm(alloc_col, dyn_col) > tolerance:
                add(
                    issues,
                    "ERROR",
                    f"{name}: fixed thruster column mismatch allocation={fmt_vec(alloc_col)} dynamics={fmt_vec(dyn_col)}",
                )

        if "max_power_kW" in acfg:
            add(
                issues,
                "INFO",
                f"{name}: max_power_kW is configured; allocator source should enforce thrust clipping from brake-power limit.",
            )

    return issues


def check_dynamics_source(source_path: Path) -> List[Issue]:
    issues: List[Issue] = []
    if not source_path.exists():
        add(issues, "WARN", f"source file not found: {source_path}")
        return issues

    text = source_path.read_text(encoding="utf-8", errors="ignore")
    if "m_total_x = v_config_.phys.mass + v_config_.am.Y_dot_v" in text:
        add(
            issues,
            "ERROR",
            "compute_nu_dot appears to use Y_dot_v in m_total_x; surge axis should be checked against initialize_mass_matrix.",
        )
    if "m_total_y = v_config_.phys.mass + v_config_.am.X_dot_u" in text:
        add(
            issues,
            "ERROR",
            "compute_nu_dot appears to use X_dot_u in m_total_y; sway axis should be checked against initialize_mass_matrix.",
        )
    if "tau_net = tau_total + C_vec + D" in text:
        add(
            issues,
            "WARN",
            "dynamics equation uses tau_total + C_vec + D; sign convention needs a documented derivation and regression test.",
        )
    return issues


def check_allocator_source(source_path: Path) -> List[Issue]:
    issues: List[Issue] = []
    if not source_path.exists():
        add(issues, "WARN", f"source file not found: {source_path}")
        return issues

    text = source_path.read_text(encoding="utf-8", errors="ignore")
    if "B_(1, r_col) = -1.0" in text or "cons.max_bound = +rf_max" in text:
        add(
            issues,
            "ERROR",
            "rudder allocation still appears to optimize rudder force while later emitting the variable as rudder angle.",
        )
    if "max_force_by_power_N" not in text or "std::copysign(max_force_by_power_N" not in text:
        add(
            issues,
            "WARN",
            "allocator does not appear to clip final thrust from max_power_kW.",
        )
    if "std::copysign(cfg.max_thrust_N" not in text:
        add(
            issues,
            "WARN",
            "allocator does not appear to clip final thrust from max_thrust_N.",
        )
    return issues


def print_report(issues: List[Issue]) -> None:
    if not issues:
        print("MODEL CONSISTENCY CHECK: PASS")
        return

    print("MODEL CONSISTENCY CHECK: FINDINGS")
    for severity, message in issues:
        print(f"[{severity}] {message}")

    errors = sum(1 for severity, _ in issues if severity == "ERROR")
    warnings = sum(1 for severity, _ in issues if severity == "WARN")
    infos = sum(1 for severity, _ in issues if severity == "INFO")
    print(f"SUMMARY: {errors} error(s), {warnings} warning(s), {infos} info(s)")


def main(args=None) -> int:
    parser = argparse.ArgumentParser(description="Check ship dynamics and thrust allocation consistency.")
    parser.add_argument(
        "--config",
        default="D:/02-dynamics/src/platform/ship_bringup/config/ship_config.yaml",
        help="Path to ship_config.yaml.",
    )
    parser.add_argument(
        "--dynamics-source",
        default="D:/02-dynamics/src/simulation/ship_dynamics/src/ship_dynamics_node.cpp",
        help="Path to ship_dynamics_node.cpp for static source checks.",
    )
    parser.add_argument(
        "--allocator-source",
        default="D:/02-dynamics/src/gnc/thrust_allocation/src/thrust_allocation_node.cpp",
        help="Path to thrust_allocation_node.cpp for static source checks.",
    )
    parser.add_argument(
        "--tolerance",
        type=float,
        default=1e-6,
        help="Allowed norm mismatch for unit generalized-force columns.",
    )
    parser.add_argument("--strict", action="store_true", help="Return non-zero when warnings exist.")
    ns = parser.parse_args(args=args)

    config = load_yaml(Path(ns.config))
    issues: List[Issue] = []
    issues.extend(check_config_consistency(config))
    issues.extend(check_thruster_model_consistency(config, ns.tolerance))
    issues.extend(check_dynamics_source(Path(ns.dynamics_source)))
    issues.extend(check_allocator_source(Path(ns.allocator_source)))
    print_report(issues)

    has_error = any(severity == "ERROR" for severity, _ in issues)
    has_warn = any(severity == "WARN" for severity, _ in issues)
    return 1 if has_error or (ns.strict and has_warn) else 0


if __name__ == "__main__":
    raise SystemExit(main())
