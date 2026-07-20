#!/usr/bin/env python3
import csv
import glob
import json
import math
import os
import statistics

import matplotlib.pyplot as plt
import numpy as np

BASE = os.path.dirname(os.path.abspath(__file__))
BACKENDS = ("ipopt", "acados")


def load_runs(backend):
    return [json.load(open(path, encoding="utf-8"))
            for path in sorted(glob.glob(os.path.join(BASE, f"{backend}_rep*.json")))]


def stats(values):
    a = np.asarray(values, dtype=float)
    return {
        "mean": float(np.mean(a)),
        "std": float(np.std(a, ddof=1)) if len(a) > 1 else 0.0,
        "min": float(np.min(a)),
        "p50": float(np.percentile(a, 50)),
        "p95": float(np.percentile(a, 95)),
        "max": float(np.max(a)),
    }


runs = {backend: load_runs(backend) for backend in BACKENDS}
summary = {}
rows = []
for backend in BACKENDS:
    data = runs[backend]
    warm_samples = [sample for run in data for sample in run["warm_wall_ms"]]
    process_wall_ms = [
        float(open(os.path.join(BASE, f"{backend}_rep{i}.process_wall_s"), encoding="utf-8").read()) * 1000.0
        for i in range(1, len(data) + 1)
    ]
    summary[backend] = {
        "process_repetitions": len(data),
        "warm_solves": len(warm_samples),
        "startup_total_ms": stats([r["timing"]["startup_total_ms"] for r in data]),
        "cold_solve_ms": stats([r["timing"]["cold_solve_wall_ms"] for r in data]),
        "warm_solve_ms": stats(warm_samples),
        "warm_per_step_ms": stats([v / 80.0 for v in warm_samples]),
        "benchmark_total_ms": stats([r["timing"]["benchmark_total_ms"] for r in data]),
        "process_wall_ms": stats(process_wall_ms),
        "cold_converged": sum(r["cold"]["status"] == "Converged" for r in data),
        "warm_converged": sum(status == 0 for r in data for status in r["warm_statuses"]),
        "cold_min_cpa_m": stats([r["cold"]["min_cpa_m"] for r in data]),
        "cold_iterations": stats([r["cold"]["iterations"] for r in data]),
        "dispatch": data[0]["dispatch"],
        "cold_status": data[0]["cold"]["status"],
        "cold_trajectory_size": data[0]["cold"]["trajectory_size"],
        "max_abs_rot_rad_s": data[0]["cold"]["max_abs_rot_rad_s"],
        "max_decel_mps2": data[0]["cold"]["max_decel_mps2"],
        "max_starboard_rad": data[0]["cold"]["max_starboard_rad"],
        "terminal_y_m": data[0]["cold"]["terminal_y_m"],
        "terminal_psi_rad": data[0]["cold"]["terminal_psi_rad"],
    }
    for rep, run in enumerate(data, 1):
        rows.append({
            "backend": backend,
            "rep": rep,
            "startup_total_ms": run["timing"]["startup_total_ms"],
            "cold_solve_ms": run["timing"]["cold_solve_wall_ms"],
            "warm_mean_ms": run["timing"]["warm_mean_ms"],
            "warm_per_step_ms": run["timing"]["warm_mean_per_step_ms"],
            "benchmark_total_ms": run["timing"]["benchmark_total_ms"],
            "cold_status": run["cold"]["status"],
            "cold_iterations": run["cold"]["iterations"],
            "min_cpa_m": run["cold"]["min_cpa_m"],
            "trajectory_size": run["cold"]["trajectory_size"],
            "dispatch_reason": run["dispatch"]["reason"],
        })

with open(os.path.join(BASE, "summary.json"), "w", encoding="utf-8") as fh:
    json.dump(summary, fh, indent=2, ensure_ascii=False)
with open(os.path.join(BASE, "runs_summary.csv"), "w", newline="", encoding="utf-8") as fh:
    writer = csv.DictWriter(fh, fieldnames=list(rows[0].keys()))
    writer.writeheader()
    writer.writerows(rows)

colors = {"ipopt": "#3366cc", "acados": "#dc3912"}

# Figure 1: timing comparison.
fig, axes = plt.subplots(2, 2, figsize=(12, 8))
metrics = [
    ("startup_total_ms", "Runtime startup", "ms", True),
    ("cold_solve_ms", "Cold target solve", "ms", True),
    ("warm_solve_ms", "Warm target solve", "ms", True),
    ("warm_per_step_ms", "Normalized warm time / 80", "ms/stage", True),
]
for ax, (key, title, ylabel, log_scale) in zip(axes.flat, metrics):
    means = [summary[b][key]["mean"] for b in BACKENDS]
    errors = [summary[b][key]["std"] for b in BACKENDS]
    bars = ax.bar(BACKENDS, means, yerr=errors, capsize=5,
                  color=[colors[b] for b in BACKENDS])
    if log_scale:
        ax.set_yscale("log")
    ax.set_title(title)
    ax.set_ylabel(ylabel + " (log scale)")
    ax.grid(axis="y", alpha=0.3)
    for bar, value in zip(bars, means):
        ax.text(bar.get_x() + bar.get_width()/2, value * 1.12,
                f"{value:.3g}", ha="center", va="bottom", fontsize=9)
fig.suptitle("M5 1200 s / 80-stage Rule 14 benchmark timing (mean ± sample SD)")
fig.tight_layout()
fig.savefig(os.path.join(BASE, "timing_comparison.png"), dpi=180)
fig.savefig(os.path.join(BASE, "timing_comparison.svg"))
plt.close(fig)

# Figure 2: warm solve samples.
fig, ax = plt.subplots(figsize=(10, 5))
for backend in BACKENDS:
    samples = [sample for run in runs[backend] for sample in run["warm_wall_ms"]]
    ax.plot(range(1, len(samples) + 1), samples, marker="o", markersize=3,
            linewidth=1.2, label=backend, color=colors[backend])
ax.set_yscale("log")
ax.axhline(2000.0, color="black", linestyle="--", linewidth=1, label="2 s gate")
ax.set_xlabel("Warm solve sample (5 processes × 10)")
ax.set_ylabel("Wall time (ms, log scale)")
ax.set_title("Warm solve latency and 2 s production gate")
ax.grid(alpha=0.3)
ax.legend()
fig.tight_layout()
fig.savefig(os.path.join(BASE, "warm_latency.png"), dpi=180)
fig.savefig(os.path.join(BASE, "warm_latency.svg"))
plt.close(fig)

# Figure 3: cold trajectories and target track.
fig, ax = plt.subplots(figsize=(9, 7))
for backend in BACKENDS:
    traj = runs[backend][0]["cold_trajectory"]
    north = [p["x_m"] for p in traj]
    east = [p["y_m"] for p in traj]
    ax.plot(east, north, linewidth=2.0, label=f"{backend} own ship",
            color=colors[backend])
target_t = np.arange(80) * 15.0
target_north = runs["ipopt"][0]["scenario"]["range_m"]
target_north = math.sqrt(target_north**2 - 60.0**2) - 3.0 * target_t
target_east = np.full_like(target_t, -60.0)
ax.plot(target_east, target_north, color="#109618", linestyle="--",
        linewidth=2.0, label="target")
ax.scatter([0.0, -60.0], [0.0, target_north[0]], s=55,
           color=[colors["ipopt"], "#109618"], zorder=4)
ax.set_xlabel("East y (m)")
ax.set_ylabel("North x (m)")
ax.set_title("Cold-solve trajectories: near head-on at 5000 m")
ax.axis("equal")
ax.grid(alpha=0.3)
ax.legend()
fig.tight_layout()
fig.savefig(os.path.join(BASE, "trajectory_comparison.png"), dpi=180)
fig.savefig(os.path.join(BASE, "trajectory_comparison.svg"))
plt.close(fig)

# Figure 4: acceptance matrix.
checks = ["Converged", "CPA ≥ 1852 m", "Valid 80-stage solution", "ROT ≤ 4.7°/s",
          "Decel ≤ 0.08 m/s²", "Converged warm p95 ≤ 2 s", "Dispatch truthful"]
matrix = np.zeros((len(checks), 2), dtype=int)
for col, backend in enumerate(BACKENDS):
    s = summary[backend]
    all_converged = s["cold_converged"] == 5 and s["warm_converged"] == 50
    matrix[:, col] = [
        int(all_converged),
        int(all_converged and s["cold_min_cpa_m"]["min"] >= 1852.0),
        int(all_converged and s["cold_trajectory_size"] == 80),
        int(all_converged and s["max_abs_rot_rad_s"] <= 4.7 * math.pi / 180.0 + 1e-6),
        int(all_converged and s["max_decel_mps2"] <= 0.08 + 1e-6),
        int(all_converged and s["warm_solve_ms"]["p95"] <= 2000.0),
        int((backend == "ipopt") or not s["dispatch"]["acados_dispatched"] or
            (s["cold_converged"] == 5 and s["cold_min_cpa_m"]["min"] >= 1852.0)),
    ]
fig, ax = plt.subplots(figsize=(8, 5.8))
ax.imshow(matrix, cmap=plt.matplotlib.colors.ListedColormap(["#d9534f", "#5cb85c"]),
          vmin=0, vmax=1, aspect="auto")
ax.set_xticks([0, 1], BACKENDS)
ax.set_yticks(range(len(checks)), checks)
for i in range(len(checks)):
    for j in range(2):
        ax.text(j, i, "PASS" if matrix[i, j] else "FAIL",
                ha="center", va="center", color="white", fontweight="bold")
ax.set_title("Acceptance matrix (independent checks)")
fig.tight_layout()
fig.savefig(os.path.join(BASE, "acceptance_matrix.png"), dpi=180)
fig.savefig(os.path.join(BASE, "acceptance_matrix.svg"))
plt.close(fig)

print(json.dumps(summary, indent=2, ensure_ascii=False))
