#!/usr/bin/env python3
"""P1b-1a T8 -- c(u) yaw-gain + integrator model-class diagnostic (Path B).

NOT production code. test/external staging spike only.

Path B (2026-07-16): the first attempt fit the 1st-order Nomoto
``T*r_dot + r = K*delta`` from this VDM's zigzag and hit a real physics wall:
the simplified 4-DOF MMG ``compute_accelerations()`` has NO ``N_r*r`` yaw-damping
term (``vessel_dynamics_model.cpp:52-59``), so the yaw channel is a pure
double integrator (``r = int delta``, ``psi = int int delta``) -- structurally
unfittable by 1st-order Nomoto (the first-attempt fit gave ``b ~= -9.7e-4`` ->
``T ~= 1027 s``, 35 deg reprojection error, IDENT FAIL).

This script re-scopes honestly to the VDM's real yaw gain

    dr/dt = c(u) * delta ,   c(u) = k_n_rudder * u^2 / izz_e

(no fabricated ``N_r``). It:

  (a) emits a forward INTEGRATOR model-class diagnostic: in the 20/20 maneuver
      it finds the longest run of >= 40 consecutive constant-rudder steps and
      computes the ratio of the second-half r-increment to the first-half
      r-increment. A pure integrator -> ratio ~= 1 (linear ramp); a 1st-order
      Nomoto system -> ratio << 1 (exponential convergence).
  (b) estimates ``c_u`` via a single-variable least-squares ``r_dot = c_u*delta``
      stacking BOTH maneuvers. ``c_u`` is the new primary key T6/T9 read.
  (c) keeps the OLD 2-variable regression ``r_dot = a*delta + b*r`` as a
      DIAGNOSTIC-ONLY side-computation to re-confirm ``b ~= 0`` (i.e. the VDM
      really is an integrator, not 1st-order Nomoto). ``b`` is NOT a gate.

Writes ``nomoto_params.json`` with the EXACT keys T6/T9 consume (``c_u``).
"""

import json
import math
import sys

import numpy as np

L_M = 45.0  # FCB LOA, fixture length_m
U_FALLBACK_MPS = 9.2593  # 18 kn

# Integrator-linearity diagnostic: a run of >= this many consecutive
# constant-rudder steps qualifies as a "constant-rudder hold" for the
# half-ratio test (60 s @ dt=0.5 s = 120 steps).
LINEARITY_MIN_RUN = 40
# Gate window applied downstream in verify_nomoto.py (kept here as a label).
LINEARITY_RATIO_LO = 0.95
LINEARITY_RATIO_HI = 1.05


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
                    if current_tag is not None and current_rows:
                        maneuvers.append((current_tag, current_rows))
                    current_tag = tokens[2] if len(tokens) >= 3 else " ".join(tokens[1:])
                    current_rows = []
                continue
            if line.lower().startswith("t_s,"):
                continue
            parts = line.split(",")
            if len(parts) < 5:
                continue
            t, psi, r, delta, u = (float(p) for p in parts[:5])
            current_rows.append({"t": t, "psi": psi, "r": r, "delta": delta, "u": u})
    if current_tag is not None and current_rows:
        maneuvers.append((current_tag, current_rows))
    return maneuvers


def integrator_linearity_ratio(rows):
    """Integrator model-class diagnostic (forward evidence, primary).

    Find the LONGEST run of >= LINEARITY_MIN_RUN consecutive steps over which
    ``delta`` is constant (the constant-rudder hold phase of the zigzag).
    Split that run in half and compute

        dr_first_half  = r[mid] - r[start]
        dr_second_half = r[end]  - r[mid]
        ratio          = dr_second_half / dr_first_half

    A pure double integrator (``dr/dt = c*u`` const while delta const) ramps
    ``r`` linearly -> ratio ~= 1.0. A 1st-order Nomoto system converges
    exponentially -> ratio << 1.

    Returns (ratio, dr_first_half, dr_second_half, run_len) or
    (None, None, None, 0) if no qualifying run exists.
    """
    deltas = [row["delta"] for row in rows]
    n = len(rows)
    # Find maximal runs of constant delta. A run is [i, j) with equal delta.
    best_len = 0
    best_run = None  # (start, end_exclusive)
    i = 0
    while i < n:
        j = i + 1
        while j < n and abs(deltas[j] - deltas[i]) < 1e-12:
            j += 1
        run_len = j - i
        if run_len >= LINEARITY_MIN_RUN and run_len > best_len:
            best_len = run_len
            best_run = (i, j)
        i = j
    if best_run is None:
        return None, None, None, 0
    start, end = best_run
    mid = (start + end) // 2
    dr_first = rows[mid]["r"] - rows[start]["r"]
    dr_second = rows[end - 1]["r"] - rows[mid]["r"]
    if abs(dr_first) < 1e-15:
        return None, dr_first, dr_second, best_len
    ratio = dr_second / dr_first
    return ratio, dr_first, dr_second, best_len


def build_single_var_regressor(maneuvers, dt):
    """Stack delta_k -> r_dot_k = (r_{k+1} - r_k)/dt for k=0..n-2 of every
    maneuver into the single-variable system  r_dot = c_u * delta.

    PAIRING NOTE: ident_runner.cpp emits each row's ``delta`` BEFORE it checks
    the zigzag flip and steps the VDM with the (possibly flipped) post-update
    delta. So the rudder ACTUALLY applied over [t_k, t_{k+1}] is the value
    recorded in row k+1, not row k. Pairing r_dot[k] with rows[k+1].delta
    (instead of rows[k].delta) yields a clean fit: the VDM literally computes
    dr/dt = k_n_rudder*u^2*delta/izz_e (no N_r*r), and the fit recovers c_u to
    ~1e-9 (ratio_to_analytical = 1.0000). Mis-pairing with rows[k].delta
    contaminates only the flip steps and biases c_u ~1.5% low.
    """
    A_rows = []
    y_rows = []
    for _tag, rows in maneuvers:
        n = len(rows)
        for k in range(n - 1):
            delta_applied = rows[k + 1]["delta"]
            r_next = rows[k + 1]["r"]
            r_k = rows[k]["r"]
            r_dot = (r_next - r_k) / dt
            A_rows.append([delta_applied])
            y_rows.append(r_dot)
    if not A_rows:
        raise RuntimeError("no samples parsed from zigzag CSV")
    return np.array(A_rows), np.array(y_rows)


def build_two_var_regressor(maneuvers, dt):
    """Diagnostic-only: stack (delta_applied, r_k) -> r_dot_k for the OLD
    2-variable regression r_dot = a*delta + b*r (to re-confirm b ~= 0). Uses
    the same applied-delta pairing as build_single_var_regressor."""
    A_rows = []
    y_rows = []
    for _tag, rows in maneuvers:
        n = len(rows)
        for k in range(n - 1):
            delta_applied = rows[k + 1]["delta"]
            r_k = rows[k]["r"]
            r_next = rows[k + 1]["r"]
            r_dot = (r_next - r_k) / dt
            A_rows.append([delta_applied, r_k])
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
        print("usage: ident_nomoto.py <zigzag.csv> [out.json]", file=sys.stderr)
        return 2
    csv_path = sys.argv[1]
    out_path = sys.argv[2] if len(sys.argv) >= 3 else "nomoto_params.json"
    dt = 0.5

    maneuvers = parse_zigzag_csv(csv_path)
    if not maneuvers:
        raise SystemExit("IDENT FAIL: no maneuvers parsed from %s" % csv_path)

    # --- (a) Integrator model-class diagnostic on the 20/20 maneuver --------
    diag_tag = "20/20"
    diag_rows = None
    for tag, rows in maneuvers:
        if tag == diag_tag:
            diag_rows = rows
            break
    if diag_rows is None:
        # Fall back to the longest maneuver available.
        diag_tag, diag_rows = max(maneuvers, key=lambda tr: len(tr[1]))
    ratio, dr_first, dr_second, run_len = integrator_linearity_ratio(diag_rows)

    # --- (b) c(u) single-variable least-squares across BOTH maneuvers -------
    A1, y1 = build_single_var_regressor(maneuvers, dt)
    n_samples = int(A1.shape[0])
    coef1, *_ = np.linalg.lstsq(A1, y1, rcond=None)
    c_u = float(coef1[0])
    y_pred1 = A1 @ coef1
    fit_residual = float(math.sqrt(np.mean((y_pred1 - y1) ** 2)))  # RMS
    r2 = r_squared(y1, y_pred1)

    # --- (c) DIAGNOSTIC-ONLY 2-variable regression (re-confirm b ~= 0) ------
    A2, y2 = build_two_var_regressor(maneuvers, dt)
    coef2, *_ = np.linalg.lstsq(A2, y2, rcond=None)
    a_diag, b_diag = float(coef2[0]), float(coef2[1])
    if abs(b_diag) < 1e-9:
        T_diag = float("inf") if b_diag >= 0.0 else float("-inf")
        K_diag = float("nan")
    else:
        T_diag = -1.0 / b_diag
        K_diag = a_diag / (-b_diag)

    # Mean cruise speed U across all samples.
    all_u = np.array([row["u"] for _tag, rows in maneuvers for row in rows])
    U = float(np.mean(all_u)) if all_u.size else U_FALLBACK_MPS
    if not math.isfinite(U) or U <= 0.0:
        U = U_FALLBACK_MPS

    # Dimensionless prime: c_prime = c_u * L / U.
    c_prime = c_u * L_M / U

    params = {
        "c_u": c_u,
        "c_prime": c_prime,
        "integrator_ratio": float(ratio) if ratio is not None else float("nan"),
        "delta_half1": float(dr_first) if dr_first is not None else float("nan"),
        "delta_half2": float(dr_second) if dr_second is not None else float("nan"),
        "fit_residual": fit_residual,
        "r_squared": r2,
        "n_samples": n_samples,
        "u_cruise": U,
        "source": ("VDM direct yaw gain (no fabricated N_r); double-integrator "
                   "model-class; real yaw damping TBD-5 sea-trial"),
        "diagnostic_b_coef": b_diag,
        # Historical diagnostic fields from the first attempt (NOT consumed by
        # T6/T9; kept for auditability of the Path B re-scope).
        "diagnostic_a_coef": a_diag,
        "diagnostic_T_s": T_diag,
        "diagnostic_K_inv_s": K_diag,
        "linearity_run_len": int(run_len),
        "linearity_run_tag": diag_tag,
    }
    with open(out_path, "w") as fh:
        json.dump(params, fh, indent=2)

    # Human-readable summary.
    print("=== c(u) yaw-gain + integrator model-class diagnostic (Path B) ===")
    print("maneuvers: %d, samples: %d" % (len(maneuvers), n_samples))
    print("--- (a) integrator model-class diagnostic (%s maneuver) ---" % diag_tag)
    if ratio is not None:
        print("  longest constant-rudder run: %d steps (>= %d required)" %
              (run_len, LINEARITY_MIN_RUN))
        print("  dr_first_half  = %.6e rad/s" % dr_first)
        print("  dr_second_half = %.6e rad/s" % dr_second)
        print("  integrator_ratio = %.6f  (gate [%.2f, %.2f])" %
              (ratio, LINEARITY_RATIO_LO, LINEARITY_RATIO_HI))
    else:
        print("  no constant-rudder run >= %d steps found" % LINEARITY_MIN_RUN)
    print("--- (b) c(u) single-variable fit r_dot = c_u*delta ---")
    print("  c_u = %.6e rad/s^2 per rad" % c_u)
    print("  fit_residual (RMS r_dot) = %.6e rad/s^2" % fit_residual)
    print("  r_squared = %.6f" % r2)
    print("  U = %.6f m/s, L = %.1f m" % (U, L_M))
    print("  c_prime = c_u*L/U = %.6f" % c_prime)
    print("--- (c) DIAGNOSTIC-ONLY 2-var fit r_dot = a*delta + b*r ---")
    print("  a = %.6e, b = %.6e (expect b ~= 0)" % (a_diag, b_diag))
    print("  T = %.6f s, K = %.6f 1/s (meaningless for an integrator)" %
          (T_diag, K_diag))
    print("wrote %s" % out_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
