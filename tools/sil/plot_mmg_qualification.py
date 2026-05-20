#!/usr/bin/env python3
# tools/sil/plot_mmg_qualification.py
"""D1.3.1' Matplotlib chart generator for qualification report evidence.

Generates 3 PNG plots:
  1. straight_deceleration.png — u(t) curve, dt=0.02s vs dt=0.001s
  2. turning_circle.png — XY trajectory + tactical diameter annotation
  3. zigzag_10_10.png — Heading psi(t) with overshoot annotations

Usage: python3 tools/sil/plot_mmg_qualification.py --evidence-dir evidence/
"""
import argparse
import math
import sys
import os
from pathlib import Path

import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_REPO_ROOT = os.path.normpath(os.path.join(_SCRIPT_DIR, "../.."))
sys.path.insert(0, os.path.join(_REPO_ROOT,
    "src/sim_workbench/sil_nodes/ship_dynamics"))
from ship_dynamics.mmg_coefficients import MMGCoefficients
from ship_dynamics.mmg_model import MMGModel, ShipState


def plot_straight_deceleration(evidence_dir: Path):
    """T1 chart: u(t) for dt=0.02s vs dt=0.001s."""
    model = MMGModel(MMGCoefficients())
    duration = 600.0

    # dt=0.02s trajectory
    state = ShipState(u=model.c.u0, psi=model.c.psi0)
    t_002, u_002 = [0.0], [state.u]
    for i in range(int(duration / 0.02)):
        state = model.rk4_step(state, 0.0, 0.0)
        t_002.append((i+1)*0.02)
        u_002.append(state.u)

    # dt=0.001s trajectory
    model_ref = MMGModel(MMGCoefficients(dt=0.001))
    state = ShipState(u=model_ref.c.u0, psi=model_ref.c.psi0)
    u_0001 = [state.u]
    for _ in range(int(duration / 0.001)):
        state = model_ref.rk4_step(state, 0.0, 0.0)
        u_0001.append(state.u)

    fig, ax = plt.subplots(figsize=(10, 5))
    ax.plot(t_002, u_002, 'b-', linewidth=1.5, label='dt=0.02s (standard)')
    ax.plot(np.linspace(0, duration, len(u_0001)), u_0001, 'r--', linewidth=1.0, label='dt=0.001s (reference)')
    ax.set_xlabel('Time [s]')
    ax.set_ylabel('Surge velocity u [m/s]')
    ax.set_title('Straight Deceleration — MMG 4-DOF (FCB 45m)')
    ax.legend()
    ax.grid(True, alpha=0.3)
    ax.annotate('WARNING: No X_u term in Abkowitz model\n(ship does not decelerate — HAZID-UNVERIFIED)',
                xy=(0.5, 0.5), xycoords='axes fraction',
                ha='center', va='center', fontsize=11,
                bbox=dict(boxstyle='round', facecolor='yellow', alpha=0.7))
    fig.tight_layout()
    fig.savefig(evidence_dir / 'straight_deceleration.png', dpi=150)
    plt.close(fig)
    print(f"  OK straight_deceleration.png")


def plot_turning_circle(evidence_dir: Path):
    """T2 chart: XY trajectory with tactical diameter."""
    model = MMGModel(MMGCoefficients())
    delta_35 = math.radians(35.0)
    duration = 600.0
    dt = model.c.dt

    state = ShipState(u=model.c.u0, psi=model.c.psi0)
    x_vals, y_vals = [state.x], [state.y]
    for _ in range(int(duration / dt)):
        state = model.rk4_step(state, delta_35, 5.0)
        x_vals.append(state.x)
        y_vals.append(state.y)

    x_arr = np.array(x_vals)
    y_arr = np.array(y_vals)
    D_T = float(2.0 * np.max(np.abs(y_arr)))

    fig, ax = plt.subplots(figsize=(8, 8))
    ax.plot(x_arr, y_arr, 'b-', linewidth=1.0, label='MMG trajectory')
    ax.axhline(y=D_T/2, color='r', linestyle='--', linewidth=1.0,
               label=f'Tactical Diameter = {D_T:.0f} m')
    ax.axhline(y=-D_T/2, color='r', linestyle='--', linewidth=1.0)
    ax.scatter([0], [0], color='green', s=80, zorder=5, label='Start')
    ax.set_xlabel('East [m]')
    ax.set_ylabel('North [m]')
    ax.set_title(f'35 deg Rudder Turning Circle — DT = {D_T:.0f} m')
    ax.legend()
    ax.grid(True, alpha=0.3)
    ax.set_aspect('equal')
    fig.tight_layout()
    fig.savefig(evidence_dir / 'turning_circle.png', dpi=150)
    plt.close(fig)
    print(f"  OK turning_circle.png (DT={D_T:.0f} m)")


def plot_zigzag_10_10(evidence_dir: Path):
    """T3 chart: psi(t) with overshoot annotations."""
    model = MMGModel(MMGCoefficients())
    delta_10 = math.radians(10.0)
    duration = 300.0
    dt = model.c.dt

    state = ShipState(u=model.c.u0, psi=model.c.psi0)
    psi0 = state.psi
    current_delta = delta_10
    last_switch_psi = psi0
    target_dev = math.radians(10.0)

    t_vals, psi_vals, switch_marks = [], [], []
    for i in range(int(duration / dt)):
        t = i * dt
        state = model.rk4_step(state, current_delta, 5.0)
        t_vals.append(t)
        psi_vals.append(math.degrees(state.psi))

        psi_dev = state.psi - last_switch_psi
        while psi_dev > math.pi: psi_dev -= 2*math.pi
        while psi_dev < -math.pi: psi_dev += 2*math.pi

        if abs(psi_dev) >= target_dev:
            current_delta = -current_delta
            last_switch_psi = state.psi
            switch_marks.append((t, math.degrees(state.psi)))
            if len(switch_marks) >= 6:
                break

    fig, ax = plt.subplots(figsize=(10, 5))
    ax.plot(t_vals, psi_vals, 'b-', linewidth=1.0, label='Heading psi(t)')
    ax.axhline(y=math.degrees(psi0), color='gray', linestyle=':', linewidth=0.8)
    for j, (t_s, psi_s) in enumerate(switch_marks):
        ax.axvline(x=t_s, color='r', linestyle='--', linewidth=0.8, alpha=0.5)
        ax.annotate(f'S{j+1}', xy=(t_s, psi_s),
                    xytext=(5, 10), textcoords='offset points',
                    fontsize=8, color='red')
    ax.set_xlabel('Time [s]')
    ax.set_ylabel('Heading psi [deg]')
    ax.set_title('Zigzag 10deg/10deg — Heading Response')
    ax.legend()
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig(evidence_dir / 'zigzag_10_10.png', dpi=150)
    plt.close(fig)
    print(f"  OK zigzag_10_10.png ({len(switch_marks)} rudder switches)")


def main():
    parser = argparse.ArgumentParser(description='D1.3.1 MMG qualification plotter')
    parser.add_argument('--evidence-dir', default='evidence', help='Output directory for PNGs')
    args = parser.parse_args()

    evidence_dir = Path(args.evidence_dir)
    evidence_dir.mkdir(parents=True, exist_ok=True)

    print(f"Generating D1.3.1' qualification charts -> {evidence_dir}/")
    plot_straight_deceleration(evidence_dir)
    plot_turning_circle(evidence_dir)
    plot_zigzag_10_10(evidence_dir)
    print("Done — 3 charts generated.")


if __name__ == '__main__':
    main()
