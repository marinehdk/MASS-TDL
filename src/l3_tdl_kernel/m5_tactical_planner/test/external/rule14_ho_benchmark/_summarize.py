#!/usr/bin/env python3
"""Print a one-line summary of a runner JSON. Helper for the benchmark log."""
import json
import sys


def main():
    d = json.load(open(sys.argv[1]))
    print(f"backend={d['backend']} status={d['status']['name']} "
          f"usable={d['usable']} cost_total={d['cost_total']:.3f} "
          f"traj_cpa_m={d['trajectory_cpa_m']:.1f} "
          f"solve_ms={d['solve_duration_ms']} iter={d['iterations']}")
    # also dump the psi sequence so we can eyeball stbd/port direction
    psis = [round(p['psi_rad'], 4) for p in d['trajectory']]
    print("psi_seq_rad=", psis)


if __name__ == "__main__":
    main()
