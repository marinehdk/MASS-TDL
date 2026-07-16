#!/usr/bin/env python3
"""P1b-1a T8 -- Nomoto first-order T,K identification from VDM zigzag CSV.

NOT production code. test/external staging spike only.

Reads the zigzag CSV produced by ident_runner.cpp (two maneuvers split by
"# maneuver <tag>" comment lines), stacks BOTH maneuvers' samples into one
least-squares fit of the Nomoto first-order model

    T * r_dot + r = K * delta        (continuous)
    r_dot = (K/T) * delta - (1/T) * r

Regress  r_dot = a * delta + b * r   ->   T = -1/b ,  K = a / (-b).

Writes nomoto_params.json with the EXACT keys T6/T9 consume (T_s, K_inv_s).

Honesty: if |b| < 1e-9 the VDM has essentially no explicit yaw-rate damping
in the regression (a real property of the simplified 4-DOF MMG here -- its
dr/dt has no N_r*r term). T -> +inf/-inf is reported as-is and the JSON stores
the sign-bounded value; the fit is NOT forced to a finite T.
"""

import json
import math
import os
import sys

import numpy as np

# Indicative ranges from the brief ([R22]) -- for human reporting only, NOT a
# tolerance to force-pass against.
T_INDICATIVE_LO_S = 2.0
T_INDICATIVE_HI_S = 10.0
K_INDICATIVE_LO_INV_S = 0.1
K_INDICATIVE_HI_INV_S = 0.6
L_M = 45.0  # FCB LOA, fixture length_m
U_FALLBACK_MPS = 9.2593  # 18 kn
B_NEAR_ZERO = 1e-9  # below this |b| the first-order T is effectively unbounded


def parse_zigzag_csv(path):
    """Parse the runner CSV. Returns list of (tag, rows) where each row is a
    dict {t, psi, r, delta, u}. Maneuvers are split on "# maneuver <tag>"."""
    maneuvers = []
    current_tag = None
    current_rows = []
    with open(path, "r") as fh:
        for raw in fh:
            line = raw.strip()
            if not line:
                continue
            if line.startswith("#"):
                tokens = line.split()
                if len(tokens) >= 2 and tokens[0] == "#":
                    # A new maneuver marker: flush the previous one.
                    if current_tag is not None and current_rows:
                        maneuvers.append((current_tag, current_rows))
                    # tokens e.g. ['#', 'maneuver', '10/10'] -> tag '10/10'
                    current_tag = tokens[2] if len(tokens) >= 3 else " ".join(tokens[1:])
                    current_rows = []
                continue
            if line.lower().startswith("t_s,"):
                continue  # CSV header within the stream
            parts = line.split(",")
            if len(parts) < 5:
                continue
            t, psi, r, delta, u = (float(p) for p in parts[:5])
            current_rows.append({"t": t, "psi": psi, "r": r, "delta": delta, "u": u})
    if current_tag is not None and current_rows:
        maneuvers.append((current_tag, current_rows))
    return maneuvers


def build_regressor(maneuvers, dt):
    """Stack (delta_k, r_k) -> r_dot_k = (r_{k+1} - r_k)/dt for k=0..n-2 of
    every maneuver into one least-squares system."""
    A_rows = []
    y_rows = []
    for _tag, rows in maneuvers:
        n = len(rows)
        for k in range(n - 1):
            delta_k = rows[k]["delta"]
            r_k = rows[k]["r"]
            r_next = rows[k + 1]["r"]
            r_dot = (r_next - r_k) / dt
            # Regressor order [delta, r] -> coeffs [a, b].
            A_rows.append([delta_k, r_k])
            y_rows.append(r_dot)
    if not A_rows:
        raise RuntimeError("no samples parsed from zigzag CSV")
    return np.array(A_rows), np.array(y_rows)


def r_squared(y_actual, y_pred):
    ss_res = float(np.sum((y_actual - y_pred) ** 2))
    ss_tot = float(np.sum((y_actual - np.mean(y_actual)) ** 2))
    if ss_tot < 1e-18:
        return 0.0
    return 1.0 - ss_res / ss_tot


def main():
    if len(sys.argv) < 2:
        print("usage: ident_nomoto.py <zigzag.csv>", file=sys.stderr)
        return 2
    csv_path = sys.argv[1]
    out_path = sys.argv[2] if len(sys.argv) >= 3 else "nomoto_params.json"
    dt = 0.5

    maneuvers = parse_zigzag_csv(csv_path)
    if not maneuvers:
        raise SystemExit("IDENT FAIL: no maneuvers parsed from %s" % csv_path)

    A, y = build_regressor(maneuvers, dt)
    n_samples = int(A.shape[0])

    # Least-squares fit r_dot = a*delta + b*r.
    coef, residuals_, rank_, sv_ = np.linalg.lstsq(A, y, rcond=None)
    a, b = float(coef[0]), float(coef[1])
    y_pred = A @ coef
    fit_residual = float(math.sqrt(np.mean((y_pred - y) ** 2)))  # RMS
    r2 = r_squared(y, y_pred)

    b_near_zero = abs(b) < B_NEAR_ZERO
    if b_near_zero:
        # First-order Nomoto has no finite T. Keep the bounded sign of b for
        # reporting, but mark T as effectively unbounded.
        T_s = float("inf") if b >= 0.0 else float("-inf")
        # K = a/(-b) is undefined; store NaN to signal downstream.
        K_inv_s = float("nan")
    else:
        T_s = -1.0 / b
        K_inv_s = a / (-b)

    # Mean cruise speed U across all samples (sway-coupled maneuvers vary u
    # only via the prop/drag balance; this is the realized mean).
    all_u = np.array([row["u"] for _tag, rows in maneuvers for row in rows])
    U = float(np.mean(all_u)) if all_u.size else U_FALLBACK_MPS
    if not math.isfinite(U) or U <= 0.0:
        U = U_FALLBACK_MPS

    # Dimensionless primes (only meaningful for finite T, K).
    if math.isfinite(T_s):
        T_prime = T_s * U / L_M
    else:
        T_prime = float("inf")
    if math.isfinite(K_inv_s):
        K_prime = K_inv_s * L_M / U
    else:
        K_prime = float("nan")

    params = {
        "T_s": T_s,
        "K_inv_s": K_inv_s,
        "T_prime": T_prime,
        "K_prime": K_prime,
        "fit_residual": fit_residual,
        "r_squared": r2,
        "n_samples": n_samples,
        "source": "simplified-MMG 4-DOF VDM zigzag fit, sea-trial calibration TBD-5",
        "b_coef": b,
        "a_coef": a,
    }
    with open(out_path, "w") as fh:
        json.dump(params, fh, indent=2)

    # Human-readable summary to stdout (the JSON goes to the file).
    print("=== Nomoto first-order fit (T*r_dot + r = K*delta) ===")
    print("maneuvers: %d, samples: %d" % (len(maneuvers), n_samples))
    print("regression r_dot = a*delta + b*r:")
    print("  a = %.6e  (K = a/(-b))" % a)
    print("  b = %.6e  (T = -1/b)" % b)
    if b_near_zero:
        print("  >> |b| < 1e-9: VDM has ~no explicit yaw-rate damping in the")
        print("     regression (simplified MMG dr/dt has no N_r*r term).")
        print("     First-order T is effectively unbounded -- real finding.")
    print("fit_residual (RMS of r_dot) = %.6e rad/s^2" % fit_residual)
    print("r_squared = %.6f" % r2)
    print("T   = %.6f s   (indicative range %.1f-%.1f s)" %
          (T_s, T_INDICATIVE_LO_S, T_INDICATIVE_HI_S))
    print("K   = %.6f 1/s (indicative range %.2f-%.2f 1/s)" %
          (K_inv_s, K_INDICATIVE_LO_INV_S, K_INDICATIVE_HI_INV_S))
    print("U   = %.6f m/s, L = %.1f m" % (U, L_M))
    print("T'  = %.6f" % T_prime)
    print("K'  = %.6f" % K_prime)
    print("wrote %s" % out_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
