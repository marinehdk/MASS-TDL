#!/usr/bin/env python3
"""P1b-1a T8 -- double-integrator forward-match + integrator-linearity gate.

NOT production code. test/external staging spike only.

Path B (2026-07-16): the first attempt gated a 1st-order Nomoto reprojection at
2 deg. That gate is structurally impossible for this VDM (its yaw channel has
no ``N_r*r`` damping -> it is a pure double integrator, not 1st-order Nomoto),
so the first attempt correctly reported IDENT FAIL. This script re-scopes the
verification to the honest double-integrator model class:

  1. FORWARD-MATCH: re-simulate EACH maneuver with the discrete double
     integrator fed the SAME delta sequence the VDM used,
         r[k+1] = r[k] + dt * c_u * delta[k]
         psi[k+1] = psi[k] + dt * r[k]
     and compare the r and psi series to the VDM-truth series. Because the VDM
     literally computes ``dr/dt = k_n_rudder*u^2*delta/izz_e`` (no r term), the
     r channel should match almost exactly and psi should accumulate only small
     error.
     GATE: max|delta_psi| < 5 deg (0.087 rad) AND r forward-match tight.
  2. INTEGRATOR-LINEARITY GATE: the 20/20 constant-rudder half-ratio must lie
     in [0.95, 1.05] (pure integrator -> ratio ~= 1).
  3. IMO MSC.137(76) first-overshoot is reported as REFERENCE ONLY -- a pure
     integrator overshoots by design; that is the model class, not a fit-quality
     problem.

Honesty: if the ratio is outside [0.95, 1.05] (the VDM is NOT a clean
integrator) or the r forward-match is poor (c(u) is not constant, e.g. u
varies), report IDENT FAIL with the real finding. Do NOT widen the gate.
"""

import json
import math
import sys

import numpy as np

# Forward-match gate (psi) and a sanity ceiling on the r forward-match.
GATE_PSI_RAD = 0.087  # 5 deg
GATE_PSI_DEG = 5.0
# r forward-match ceiling: the r channel is the direct integration of c_u*delta,
# so it should match the VDM to numerical precision. A loose ceiling flags
# either a non-constant c(u) (u varying) or a parsing/seed mismatch.
GATE_R_RAD = 1e-3  # 0.001 rad/s

# Integrator-linearity gate window (must match ident_nomoto.py).
LINEARITY_RATIO_LO = 0.95
LINEARITY_RATIO_HI = 1.05
LINEARITY_MIN_RUN = 40

L_M = 45.0


def _parse_maneuvers(path):
    """Same parser shape as ident_nomoto.parse_zigzag_csv (kept local so this
    script has no import dependency on the fit script)."""
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


def _threshold_deg_for_tag(tag):
    try:
        return float(tag.split("/")[0])
    except (ValueError, IndexError):
        return None


def integrator_linearity_ratio(rows):
    """Same diagnostic as ident_nomoto.integrator_linearity_ratio."""
    deltas = [row["delta"] for row in rows]
    n = len(rows)
    best_len = 0
    best_run = None
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


def forward_match_maneuver(rows, c_u, dt):
    """Re-simulate r and psi with the discrete double integrator fed the VDM
    delta sequence. Seeds r[0], psi[0] from the VDM initial state so the match
    is on the rudder->yaw response, not on initial conditions.

    PAIRING NOTE: ident_runner.cpp emits each row's ``delta`` BEFORE the zigzag
    flip check, then steps the VDM with the (possibly flipped) post-update
    delta. So the rudder ACTUALLY applied over [t_k, t_{k+1}] is rows[k+1].delta
    (see ident_nomoto.build_single_var_regressor for the full justification).
    Using rows[k].delta mis-aligns every flip step and inflates the forward-
    match error; using rows[k+1].delta makes the r channel match the VDM to
    ~1e-9 (Euler vs RK4) and psi to within a few degrees.

    Returns (r_series, psi_series) aligned 1:1 with rows.
    """
    r = rows[0]["r"]
    psi = rows[0]["psi"]
    r_series = [r]
    psi_series = [psi]
    for k in range(len(rows) - 1):
        delta_applied = rows[k + 1]["delta"]
        r = r + dt * c_u * delta_applied
        psi = psi + dt * r
        r_series.append(r)
        psi_series.append(psi)
    return r_series, psi_series


def main():
    if len(sys.argv) < 3:
        print("usage: verify_nomoto.py <nomoto_params.json> <zigzag.csv>",
              file=sys.stderr)
        return 2
    params_path = sys.argv[1]
    csv_path = sys.argv[2]
    dt = 0.5

    with open(params_path, "r") as fh:
        params = json.load(fh)
    c_u = float(params["c_u"])
    fit_residual = float(params.get("fit_residual", float("nan")))
    r2 = float(params.get("r_squared", float("nan")))
    ratio_json = params.get("integrator_ratio", None)
    b_diag = float(params.get("diagnostic_b_coef", float("nan")))

    maneuvers = _parse_maneuvers(csv_path)
    if not maneuvers:
        print("IDENT FAIL: no maneuvers parsed from %s" % csv_path, file=sys.stderr)
        return 1

    print("=== double-integrator forward-match vs VDM truth (Path B) ===")
    print("c_u = %.6e rad/s^2 per rad  (fit_residual=%.4e, r2=%.4f)" %
          (c_u, fit_residual, r2))
    print("diagnostic b_coef = %.6e (expect ~= 0: integrator, not 1st-order)" % b_diag)

    # ---- (1) forward-match each maneuver ----------------------------------
    max_psi_err_rad = 0.0
    max_r_err_rad = 0.0
    per_maneuver_imo = []
    for tag, rows in maneuvers:
        r_truth = np.array([row["r"] for row in rows])
        psi_truth = np.array([row["psi"] for row in rows])
        r_sim, psi_sim = forward_match_maneuver(rows, c_u, dt)
        r_sim = np.array(r_sim)
        psi_sim = np.array(psi_sim)
        n = min(len(psi_truth), len(psi_sim))
        dpsi = np.abs(psi_truth[:n] - psi_sim[:n])
        dr = np.abs(r_truth[:n] - r_sim[:n])
        m_psi_rad = float(np.max(dpsi)) if n > 0 else 0.0
        m_r_rad = float(np.max(dr)) if n > 0 else 0.0
        max_psi_err_rad = max(max_psi_err_rad, m_psi_rad)
        max_r_err_rad = max(max_r_err_rad, m_r_rad)
        print("  %-6s n=%4d  max|dr|=%.6e rad/s  max|dpsi|=%.6f rad (%.4f deg)" %
              (tag, n, m_r_rad, m_psi_rad, math.degrees(m_psi_rad)))

        # IMO MSC.137(76) reference overshoot from VDM truth psi (not the gate).
        thr_deg = _threshold_deg_for_tag(tag)
        abs_psi_deg = np.abs(np.degrees(psi_truth))
        peak_deg = float(np.max(abs_psi_deg)) if abs_psi_deg.size else 0.0
        overshoot_deg = peak_deg - thr_deg if thr_deg is not None else float("nan")
        per_maneuver_imo.append((tag, peak_deg, overshoot_deg, thr_deg))

    print("--- IMO MSC.137(76) REFERENCE (VDM truth, NOT gated) ---")
    for tag, peak_deg, overshoot_deg, thr_deg in per_maneuver_imo:
        print("  %-6s peak|psi| = %.3f deg, overshoot above %.0f deg cmd = %.3f deg"
              "  (pure integrator overshoots by design -- model class)" %
              (tag, peak_deg, thr_deg if thr_deg is not None else float("nan"),
               overshoot_deg))

    # ---- (2) integrator-linearity gate (re-compute for self-containment) --
    diag_tag = "20/20"
    diag_rows = None
    for tag, rows in maneuvers:
        if tag == diag_tag:
            diag_rows = rows
            break
    if diag_rows is None:
        diag_tag, diag_rows = max(maneuvers, key=lambda tr: len(tr[1]))
    ratio, dr_first, dr_second, run_len = integrator_linearity_ratio(diag_rows)
    if ratio is None:
        print(">> no constant-rudder run >= %d steps in %s maneuver" %
              (LINEARITY_MIN_RUN, diag_tag))
        ratio_ok = False
        ratio_val = float("nan")
    else:
        ratio_val = ratio
        ratio_ok = LINEARITY_RATIO_LO <= ratio <= LINEARITY_RATIO_HI
        print("integrator-linearity: ratio=%.6f (run=%d steps in %s) "
              "gate [%.2f, %.2f] -> %s" %
              (ratio_val, run_len, diag_tag,
               LINEARITY_RATIO_LO, LINEARITY_RATIO_HI,
               "PASS" if ratio_ok else "FAIL"))

    # If the JSON carries the ratio too, cross-check (informational only).
    if ratio_json is not None:
        try:
            rj = float(ratio_json)
            if math.isfinite(rj) and math.isfinite(ratio_val):
                print("  (json integrator_ratio=%.6f, recomputed=%.6f)" % (rj, ratio_val))
        except (TypeError, ValueError):
            pass

    # ---- gates ------------------------------------------------------------
    psi_ok = max_psi_err_rad < GATE_PSI_RAD
    r_ok = max_r_err_rad < GATE_R_RAD
    print("--- gates ---")
    print("  psi forward-match: max|dpsi|=%.4f deg  gate < %.1f deg -> %s" %
          (math.degrees(max_psi_err_rad), GATE_PSI_DEG, "PASS" if psi_ok else "FAIL"))
    print("  r   forward-match: max|dr|=%.6e rad/s  gate < %.1e rad/s -> %s" %
          (max_r_err_rad, GATE_R_RAD, "PASS" if r_ok else "FAIL"))
    print("  integrator ratio: %.6f              gate [%.2f,%.2f] -> %s" %
          (ratio_val, LINEARITY_RATIO_LO, LINEARITY_RATIO_HI,
           "PASS" if ratio_ok else "FAIL"))

    if psi_ok and r_ok and ratio_ok:
        print("IDENT PASS: c_u=%.6e rad/s^2/rad (integrator ratio=%.4f, "
              "psi forward-match %.4f deg, r forward-match %.3e rad)" %
              (c_u, ratio_val, math.degrees(max_psi_err_rad), max_r_err_rad))
        return 0

    reasons = []
    if not psi_ok:
        reasons.append("psi forward-match %.4f deg >= %.1f deg" %
                       (math.degrees(max_psi_err_rad), GATE_PSI_DEG))
    if not r_ok:
        reasons.append("r forward-match %.3e rad/s >= %.1e rad/s (c(u) not constant?)"
                       % (max_r_err_rad, GATE_R_RAD))
    if not ratio_ok:
        reasons.append("integrator ratio %.4f outside [%.2f, %.2f]" %
                       (ratio_val, LINEARITY_RATIO_LO, LINEARITY_RATIO_HI))
    print("IDENT FAIL: %s" % "; ".join(reasons))
    return 1


if __name__ == "__main__":
    sys.exit(main())
