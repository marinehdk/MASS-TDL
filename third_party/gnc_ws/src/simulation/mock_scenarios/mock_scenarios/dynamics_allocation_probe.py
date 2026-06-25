import argparse
import math
from pathlib import Path
from typing import Dict, List, Tuple

import yaml


Tau = Tuple[float, float, float]


def load_yaml(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as stream:
        return yaml.safe_load(stream) or {}


def deep_get(data: dict, keys: List[str], default=None):
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


def add_tau(a: Tau, b: Tau) -> Tau:
    return a[0] + b[0], a[1] + b[1], a[2] + b[2]


def scale_tau(a: Tau, scale: float) -> Tau:
    return a[0] * scale, a[1] * scale, a[2] * scale


def tau_norm(a: Tau) -> float:
    return math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])


def tau_diff(a: Tau, b: Tau) -> float:
    return tau_norm((a[0] - b[0], a[1] - b[1], a[2] - b[2]))


def fmt_tau(tau: Tau, unit: str = "N") -> str:
    if unit == "k":
        return f"Fx={tau[0]:+.3f} kN, Fy={tau[1]:+.3f} kN, Mz={tau[2]:+.3f} kNm"
    return f"Fx={tau[0]:+.1f} N, Fy={tau[1]:+.1f} N, Mz={tau[2]:+.1f} Nm"


def get_thruster_configs(config: dict) -> Tuple[List[str], Dict[str, dict], Dict[str, dict]]:
    dyn_thrusters = deep_get(config, ["ship_dynamics_node", "ros__parameters", "thrusters"], {})
    alloc_params = deep_get(config, ["thrust_allocation_node", "ros__parameters"], {})
    names = list(dyn_thrusters.get("thruster_names", []) or [])
    return names, dyn_thrusters, alloc_params


def dynamics_fixed_thruster_tau(cfg: dict, thrust_n: float, angle_rad: float = None) -> Tau:
    x = as_float(cfg, "x")
    y = as_float(cfg, "y")
    efficiency = as_float(cfg, "efficiency", 1.0)
    angle = as_float(cfg, "angle_default") if angle_rad is None else angle_rad
    adjusted_angle = angle
    if thrust_n < 0.0:
        adjusted_angle += math.pi
    abs_thrust = abs(thrust_n)
    fx = abs_thrust * math.cos(adjusted_angle) * efficiency
    fy = abs_thrust * math.sin(adjusted_angle) * efficiency
    mz = x * fy - y * fx
    return fx, fy, mz


def rudder_dynamics_tau(cfg: dict, delta_rad: float, u: float, v: float = 0.0, r: float = 0.0, center_thrust_n: float = 0.0) -> Tau:
    x_r = as_float(cfg, "x")
    y_r = as_float(cfg, "y")
    rho = 1025.0
    prop_diameter = 1.8
    disk_area = math.pi * (prop_diameter / 2.0) ** 2
    k_wash = 0.55
    u_wash = 0.0
    if center_thrust_n > 100.0:
        u_wash = k_wash * math.sqrt(2.0 * center_thrust_n / (rho * disk_area))

    u_eff = max(u + u_wash, 0.3)
    v_local = v + x_r * r
    u_r_sq = u_eff * u_eff + v_local * v_local

    alpha = max(min(delta_rad, math.radians(35.0)), -math.radians(35.0))
    area = 3.5
    aspect_ratio = 2.0
    oswald = 0.85
    cd_0 = 0.025
    cl_alpha = 2.0 * math.pi * aspect_ratio / (aspect_ratio + 2.0)
    cl = cl_alpha * alpha
    cd = cd_0 + cl * cl / (math.pi * aspect_ratio * oswald)
    q_r = 0.5 * rho * area * u_r_sq
    normal_force = q_r * cl_alpha * alpha

    fx = -abs(normal_force * math.sin(delta_rad)) - q_r * cd
    fy = -normal_force * math.cos(delta_rad)
    mz = x_r * fy - y_r * fx
    return fx, fy, mz


def allocator_rudder_column(cfg: dict, u: float) -> Tau:
    x = as_float(cfg, "x")
    u_abs = abs(u)
    u_trans = 1.0
    k_sig = 5.0
    sigma = 1.0 / (1.0 + math.exp(-k_sig * (u_abs - u_trans)))
    rho = 1025.0
    area = 3.5
    cl_alpha = 2.8
    force_per_rad_kn = 0.5 * rho * area * cl_alpha * u_abs * u_abs * 1e-3 * sigma
    return 0.0, -force_per_rad_kn, -x * force_per_rad_kn


def check_single_actuator_directions(names: List[str], dyn_cfg: Dict[str, dict], alloc_cfg: Dict[str, dict]) -> Tuple[int, int]:
    errors = 0
    warnings = 0
    print("SINGLE ACTUATOR DIRECTION PROBE")
    for name in names:
        dcfg = dyn_cfg[name]
        acfg = alloc_cfg.get(name, {})
        is_rudder = as_bool(acfg, "is_rudder") or name in ("rudder", "r1", "r2")
        if is_rudder:
            tau = rudder_dynamics_tau(dcfg, math.radians(10.0), u=2.0)
            print(f"  {name}: +10deg rudder -> {fmt_tau(tau)}")
            if tau[1] >= 0.0:
                print(f"    [ERROR] expected positive rudder angle to produce negative sway force in current convention")
                errors += 1
            continue

        tau = dynamics_fixed_thruster_tau(dcfg, 10000.0)
        print(f"  {name}: +10kN command -> {fmt_tau(tau)}")
        if name.startswith("tb") and abs(tau[1]) < abs(tau[0]):
            print(f"    [ERROR] bow thruster should primarily produce sway force")
            errors += 1
        elif not name.startswith("tb") and abs(tau[0]) < abs(tau[1]):
            print(f"    [WARN] non-bow fixed thruster produces more sway than surge")
            warnings += 1
    return errors, warnings


def check_allocator_dynamics_columns(names: List[str], dyn_cfg: Dict[str, dict], alloc_cfg: Dict[str, dict]) -> Tuple[int, int]:
    errors = 0
    warnings = 0
    print("\nALLOCATION/DYNAMICS COLUMN PROBE")
    for name in names:
        dcfg = dyn_cfg[name]
        acfg = alloc_cfg.get(name, {})
        is_rudder = as_bool(acfg, "is_rudder") or name in ("rudder", "r1", "r2")
        is_azimuth = as_bool(acfg, "is_azimuth")
        x = as_float(acfg, "x")
        y = as_float(acfg, "y")
        angle = as_float(acfg, "angle_default")

        if is_rudder:
            continue

        if is_azimuth:
            cases = [
                ("Fx", (1.0, 0.0, -y), dynamics_fixed_thruster_tau(dcfg, 1000.0, 0.0)),
                ("Fy", (0.0, 1.0, x), dynamics_fixed_thruster_tau(dcfg, 1000.0, math.pi / 2.0)),
            ]
        else:
            alloc_col = (math.cos(angle), math.sin(angle), x * math.sin(angle) - y * math.cos(angle))
            dyn_tau = scale_tau(dynamics_fixed_thruster_tau(dcfg, 1000.0), 1e-3)
            cases = [("fixed", alloc_col, dyn_tau)]

        for label, alloc_col, dyn_col in cases:
            error = tau_diff(alloc_col, dyn_col)
            print(f"  {name}/{label}: allocation={fmt_tau(alloc_col, 'k')} dynamics={fmt_tau(dyn_col, 'k')} diff={error:.6f}")
            if error > 1e-6:
                print("    [ERROR] allocation column does not match dynamics force synthesis")
                errors += 1

    return errors, warnings


def check_rudder_linearization(names: List[str], dyn_cfg: Dict[str, dict], alloc_cfg: Dict[str, dict]) -> Tuple[int, int]:
    errors = 0
    warnings = 0
    print("\nRUDDER LINEARIZATION PROBE")
    speeds = [0.5, 1.0, 2.0, 3.0]
    delta = math.radians(5.0)
    for name in names:
        acfg = alloc_cfg.get(name, {})
        if not (as_bool(acfg, "is_rudder") or name in ("rudder", "r1", "r2")):
            continue
        dcfg = dyn_cfg[name]
        for u in speeds:
            alloc_tau = scale_tau(allocator_rudder_column(acfg, u), delta)
            dyn_tau = scale_tau(rudder_dynamics_tau(dcfg, delta, u), 1e-3)
            denominator = max(tau_norm(dyn_tau), 1e-6)
            rel_error = tau_diff(alloc_tau, dyn_tau) / denominator
            print(
                f"  {name} u={u:.1f}m/s delta=5deg: "
                f"allocation={fmt_tau(alloc_tau, 'k')} dynamics={fmt_tau(dyn_tau, 'k')} rel_error={rel_error:.2f}"
            )
            if u >= 1.0 and rel_error > 0.60:
                print("    [WARN] rudder linear model differs materially from nonlinear dynamics")
                warnings += 1
    return errors, warnings


def brake_power_limited_force(cfg: dict) -> float:
    beta_t = 0.9
    eta_product = as_float(cfg, "eta1", 800.0) * as_float(cfg, "eta2", 1.0)
    eta_m = as_float(cfg, "eta_m", 0.93)
    diameter = as_float(cfg, "diameter", 1.0)
    max_power_kw = as_float(cfg, "max_power_kW", 0.0)
    if eta_product <= 1e-9 or eta_m <= 1e-9 or diameter <= 1e-9 or max_power_kw <= 0.0:
        return math.inf
    p_shaft_limit_kw = max_power_kw * eta_m
    t_nominal_limit_kn = eta_product * (p_shaft_limit_kw * diameter) ** (2.0 / 3.0)
    return beta_t * t_nominal_limit_kn * 1000.0


def check_power_limits(names: List[str], dyn_cfg: Dict[str, dict], alloc_cfg: Dict[str, dict]) -> Tuple[int, int]:
    errors = 0
    warnings = 0
    print("\nPOWER LIMIT PROBE")
    for name in names:
        acfg = alloc_cfg.get(name, {})
        if as_bool(acfg, "is_rudder") or name in ("rudder", "r1", "r2"):
            continue
        max_thrust_n = as_float(acfg, "max_thrust_kN", 0.0) * 1000.0
        power_force_n = brake_power_limited_force(acfg)
        effective_limit_n = min(max_thrust_n, power_force_n)
        print(
            f"  {name}: max_thrust={max_thrust_n:.0f}N "
            f"power_limited={power_force_n:.0f}N effective_limit={effective_limit_n:.0f}N"
        )
        if not math.isfinite(power_force_n):
            print("    [WARN] no finite power-based thrust limit could be computed")
            warnings += 1
        elif power_force_n <= 0.0:
            print("    [ERROR] power-based thrust limit is non-positive")
            errors += 1
        elif power_force_n > max_thrust_n * 1.5:
            print("    [WARN] power limit is much looser than max_thrust; verify eta coefficients")
            warnings += 1
    return errors, warnings


def main(args=None) -> int:
    parser = argparse.ArgumentParser(description="Probe ship dynamics and thrust allocation numerical consistency.")
    parser.add_argument(
        "--config",
        default="D:/02-dynamics/src/platform/ship_bringup/config/ship_config.yaml",
        help="Path to ship_config.yaml.",
    )
    ns = parser.parse_args(args=args)

    config = load_yaml(Path(ns.config))
    names, dyn_cfg, alloc_cfg = get_thruster_configs(config)

    errors = 0
    warnings = 0
    for check in (
        check_single_actuator_directions,
        check_allocator_dynamics_columns,
        check_rudder_linearization,
        check_power_limits,
    ):
        e, w = check(names, dyn_cfg, alloc_cfg)
        errors += e
        warnings += w

    print(f"\nSUMMARY: {errors} error(s), {warnings} warning(s)")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
