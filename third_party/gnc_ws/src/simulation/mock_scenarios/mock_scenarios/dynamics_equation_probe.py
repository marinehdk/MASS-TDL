import argparse
import math
from pathlib import Path
from typing import List, Tuple

import yaml


Vec4 = Tuple[float, float, float, float]


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


def mat_vec_mul(mat: List[List[float]], vec: Vec4) -> Vec4:
    return tuple(sum(row[j] * vec[j] for j in range(4)) for row in mat)  # type: ignore[return-value]


def dot(a: Vec4, b: Vec4) -> float:
    return sum(x * y for x, y in zip(a, b))


def add(a: Vec4, b: Vec4) -> Vec4:
    return tuple(x + y for x, y in zip(a, b))  # type: ignore[return-value]


def build_mass_matrix(params: dict) -> List[List[float]]:
    vessel = params["vessel"]
    mass = float(vessel["mass"])
    ixx = float(deep_get(vessel, ["moment_of_inertia", "Ixx"], 2.0e7))
    izz = float(deep_get(vessel, ["moment_of_inertia", "Izz"], 3.0e10))
    added = vessel["added_mass"]
    mx = mass + float(added["X_dot_u"])
    my = mass + float(added["Y_dot_v"])
    mp = ixx + float(deep_get(added, ["K_dot_p"], 0.0))
    myaw = izz + float(added["N_dot_r"])
    myr = float(deep_get(added, ["Y_dot_r"], 0.0))
    mnv = float(deep_get(added, ["N_dot_v"], 0.0))
    return [
        [mx, 0.0, 0.0, 0.0],
        [0.0, my, 0.0, myr],
        [0.0, 0.0, mp, 0.0],
        [0.0, mnv, 0.0, myaw],
    ]


def damping_vec(params: dict, nu: Vec4) -> Vec4:
    hydro = params["vessel"]["hydrodynamic"]
    u, v, p, r = nu
    return (
        float(hydro["X_u"]) * u + float(hydro["X_uu"]) * u * abs(u),
        float(hydro["Y_v"]) * v + float(hydro["Y_vv"]) * v * abs(v) + float(hydro["Y_r"]) * r,
        float(deep_get(hydro, ["K_p"], -1000.0)) * p + float(deep_get(hydro, ["K_pp"], -1000.0)) * p * abs(p),
        float(hydro["N_r"]) * r + float(hydro["N_rr"]) * r * abs(r) + float(hydro["N_v"]) * v,
    )


def coriolis_vec(params: dict, nu: Vec4) -> Vec4:
    vessel = params["vessel"]
    mass = float(vessel["mass"])
    added = vessel["added_mass"]
    u, v, _p, r = nu
    mx = mass + float(added["X_dot_u"])
    my = mass + float(added["Y_dot_v"])
    c03 = mx * v
    c13 = -my * u
    c30 = -mx * v
    c31 = my * u
    return (
        c03 * r,
        c13 * r,
        0.0,
        c30 * u + c31 * v,
    )


def fmt_vec(v: Vec4) -> str:
    return f"({v[0]:+.3f}, {v[1]:+.3f}, {v[2]:+.3f}, {v[3]:+.3f})"


def run_case(name: str, params: dict, nu: Vec4) -> Tuple[int, int]:
    mass_matrix = build_mass_matrix(params)
    momentum = mat_vec_mul(mass_matrix, nu)
    d = damping_vec(params, nu)
    c = coriolis_vec(params, nu)
    power_d = dot(nu, d)
    power_c = dot(nu, c)
    power_total_no_tau = dot(nu, add(c, d))

    errors = 0
    warnings = 0
    print(f"{name}: nu={fmt_vec(nu)}")
    print(f"  momentum=M*nu {fmt_vec(momentum)}")
    print(f"  damping D(nu) {fmt_vec(d)} power={power_d:+.3f} W-equivalent")
    print(f"  coriolis C(nu)nu {fmt_vec(c)} power={power_c:+.3f} W-equivalent")
    print(f"  total no-tau power={power_total_no_tau:+.3f} W-equivalent")

    if power_d > 1e-6:
        print("  [ERROR] damping adds energy; sign convention is unsafe")
        errors += 1
    if abs(power_c) > max(1e-3, abs(power_d) * 0.05):
        print("  [WARN] coriolis/centripetal term is not power-neutral under this simplified check")
        warnings += 1
    if power_total_no_tau > 1e-6:
        print("  [ERROR] zero-input dynamics adds energy")
        errors += 1
    return errors, warnings


def main(args=None) -> int:
    parser = argparse.ArgumentParser(description="Probe ship dynamics equation sign and energy behavior.")
    parser.add_argument(
        "--config",
        default="D:/02-dynamics/src/platform/ship_bringup/config/ship_config.yaml",
        help="Path to ship_config.yaml.",
    )
    ns = parser.parse_args(args=args)
    config = load_yaml(Path(ns.config))
    params = deep_get(config, ["ship_dynamics_node", "ros__parameters"], {})

    cases = [
        ("surge", (2.0, 0.0, 0.0, 0.0)),
        ("sway", (0.0, 0.5, 0.0, 0.0)),
        ("yaw", (0.0, 0.0, 0.0, 0.05)),
        ("coupled_turn", (2.0, 0.3, 0.0, 0.05)),
        ("reverse_sway_turn", (-1.0, -0.2, 0.0, -0.04)),
    ]

    errors = 0
    warnings = 0
    print("DYNAMICS EQUATION ENERGY PROBE")
    for name, nu in cases:
        e, w = run_case(name, params, nu)
        errors += e
        warnings += w

    print(f"SUMMARY: {errors} error(s), {warnings} warning(s)")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
