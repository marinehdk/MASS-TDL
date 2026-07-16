#!/usr/bin/env python3
"""P1b-1a T8 -- Nomoto identification quality verification.

NOT production code. test/external staging spike only.

Loads T,K from nomoto_params.json and the zigzag CSV, then:
  1. Re-simulates EACH maneuver with the discrete first-order Nomoto eqn
        r[k+1] = r[k] + dt*(K*delta[k] - r[k])/T
        psi[k+1] = psi[k] + dt*r[k]
     fed the SAME delta sequence the VDM run used, and compares the psi series
     to the VDM-truth psi series.
  2. Computes the IMO MSC.137(76) first-overshoot metric per maneuver (Nomoto
     reprojection): max |psi| peak above the command threshold. This is a
     REFERENCE metric, labelled clearly -- not the gate.
  3. GATE: reprojection max|delta_psi| < 2 deg (0.035 rad).

Honesty: if the fit found b~=0 (no yaw-rate damping, T unbounded), the Nomoto
reprojection diverges (psi grows unbounded). That is a REAL finding -- report
IDENT FAIL with the reason and exit non-zero. Do NOT widen the tolerance.
"""

import json
import math
import sys

import numpy as np

GATE_REPROJ_RAD = 0.035  # 2 deg
GATE_REPROJ_DEG = 2.0


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
    """Parse the command-threshold in degrees from a maneuver tag like '10/10'."""
    try:
        return float(tag.split("/")[0])
    except (ValueError, IndexError):
        return None


def reproject_maneuver(rows, T, K, dt):
    """Re-simulate psi with discrete Nomoto fed the VDM delta sequence.

    psi[0] = rows[0].psi, r[0] = rows[0].r (seed from VDM initial state so the
    comparison is on the rudder->heading response, not on initial conditions).
    Returns list of reprojected psi aligned 1:1 with rows.
    """
    psi = rows[0]["psi"]
    r = rows[0]["r"]
    psi_series = [psi]
    for k in range(len(rows) - 1):
        delta_k = rows[k]["delta"]
        if not math.isfinite(T) or abs(T) > 1e12:
            # T unbounded: Nomoto reduces to r_dot = K*delta (pure integrator
            # with no yaw damping). Reproject with that degenerate form so the
            # divergence is observable in the psi series.
            r = r + dt * K * delta_k
        else:
            r = r + dt * (K * delta_k - r) / T
        psi = psi + dt * r
        psi_series.append(psi)
    return psi_series


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
    T = float(params["T_s"])
    K = float(params["K_inv_s"])
    fit_residual = float(params.get("fit_residual", float("nan")))
    r2 = float(params.get("r_squared", float("nan")))
    b_coef = float(params.get("b_coef", 0.0))

    maneuvers = _parse_maneuvers(csv_path)
    if not maneuvers:
        print("IDENT FAIL: no maneuvers parsed from %s" % csv_path, file=sys.stderr)
        return 1

    print("=== Nomoto reprojection vs VDM truth ===")
    print("T = %.6f s, K = %.6f 1/s, b_coef = %.6e" % (T, K, b_coef))

    # Honest b~=0 short-circuit: first-order Nomoto is insufficient.
    if not math.isfinite(T) or abs(b_coef) < 1e-9 or not math.isfinite(K):
        print(">> b_coef ~= 0 (no explicit yaw-rate damping): first-order Nomoto")
        print("   reprojection diverges (psi grows unbounded). Real finding.")
        print("IDENT FAIL: first-order Nomoto insufficient fit for simplified "
              "MMG (b~=0, no explicit yaw damping); recommend second-order "
              "Nomoto or larger tolerance")
        return 1

    max_reproj_rad = 0.0
    per_maneuver_imo = []
    for tag, rows in maneuvers:
        psi_truth = np.array([row["psi"] for row in rows])
        psi_reproj = np.array(reproject_maneuver(rows, T, K, dt))
        n = min(len(psi_truth), len(psi_reproj))
        dpsi = np.abs(psi_truth[:n] - psi_reproj[:n])
        m_rad = float(np.max(dpsi)) if n > 0 else 0.0
        m_deg = math.degrees(m_rad)
        max_reproj_rad = max(max_reproj_rad, m_rad)

        # IMO MSC.137(76) reference overshoot: max |psi| peak above the command
        # threshold, from the Nomoto reprojection. Labelled as a REFERENCE.
        thr_deg = _threshold_deg_for_tag(tag)
        abs_psi_deg = np.abs(np.degrees(psi_reproj))
        peak_deg = float(np.max(abs_psi_deg)) if abs_psi_deg.size else 0.0
        overshoot_deg = peak_deg - thr_deg if thr_deg is not None else float("nan")
        per_maneuver_imo.append((tag, peak_deg, overshoot_deg, thr_deg))
        print("  %-6s n=%4d  reprojection max|dpsi| = %.5f rad (%.4f deg)"
              % (tag, n, m_rad, m_deg))

    print("--- IMO MSC.137(76) REFERENCE (Nomoto reprojection, not the gate) ---")
    for tag, peak_deg, overshoot_deg, thr_deg in per_maneuver_imo:
        note = ""
        if thr_deg is not None:
            # Indicative first-overshoot ceilings from the brief.
            indicative = 20.0 if thr_deg <= 10.0 + 1e-6 else 25.0
            if overshoot_deg > indicative:
                note = "  (exceeds indicative ~%.0f deg; reported, not gated)" % indicative
        print("  %-6s peak|psi| = %.3f deg, overshoot above %.0f deg cmd = %.3f deg%s"
              % (tag, peak_deg, thr_deg if thr_deg is not None else float("nan"),
                 overshoot_deg, note))

    reproj_deg = math.degrees(max_reproj_rad)
    imo_max = max((ov for _t, _p, ov, _th in per_maneuver_imo
                   if ov is not None and math.isfinite(ov)), default=float("nan"))
    gate_pass = max_reproj_rad < GATE_REPROJ_RAD

    # Honest root-cause note (does NOT widen the gate). When the fit's T is huge
    # the fitted b_coef is near zero: the VDM's yaw acceleration has no N_r*r
    # term, so first-order Nomoto (which needs b = -1/T < 0) is structurally
    # inapplicable to this simplified MMG. That is the real finding.
    if not gate_pass and abs(T) > 100.0:
        print(">> note: fitted |T| >> indicative range -- b_coef is effectively 0.")
        print("   The simplified-MMG VDM has no hydrodynamic yaw-rate damping")
        print("   (dr/dt = k_n_rudder*u^2*delta / izz_e, no N_r*r term), so a")
        print("   first-order Nomoto model cannot reproduce its yaw response.")
        print("   Recommend second-order Nomoto or revisit VDM yaw damping; do")
        print("   NOT widen this gate to force a pass.")

    if gate_pass:
        print("IDENT PASS: T=%.4fs K=%.4f/s fit_res=%.4e "
              "(psi reprojection err %.4f deg, IMO overshoot %.3f deg)" %
              (T, K, fit_residual, reproj_deg, imo_max))
        return 0
    print("IDENT FAIL: reprojection err %.4f deg >= %.1f deg gate "
          "(T=%.4fs K=%.4f/s fit_res=%.4e r2=%.4f IMO_overshoot=%.3f deg)" %
          (reproj_deg, GATE_REPROJ_DEG, T, K, fit_residual, r2, imo_max))
    return 1


if __name__ == "__main__":
    sys.exit(main())
