#!/usr/bin/env python3
"""Print a one-line summary of a runner JSON. Helper for the benchmark log."""
import json
import sys


def main():
    d = json.load(open(sys.argv[1]))
    sc = d.get("scenario", {})
    role = sc.get("role", "primary")
    tgt_d = sc.get("target_distance_m", "?")
    tcpa = sc.get("tcpa_s", "?")
    print(f"backend={d['backend']} role={role} target_d={tgt_d}m tcpa={tcpa}s "
          f"status={d['status']['name']} usable={d['usable']} "
          f"cost_total={d['cost_total']:.3f} "
          f"traj_cpa_m={d['trajectory_cpa_m']:.1f} "
          f"solve_ms={d['solve_duration_ms']} iter={d['iterations']}")
    # also dump the psi sequence so we can eyeball stbd/port direction
    psis = [round(p['psi_rad'], 4) for p in d['trajectory']]
    print("psi_seq_rad=", psis)


if __name__ == "__main__":
    main()
