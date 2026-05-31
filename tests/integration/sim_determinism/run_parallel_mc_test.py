#!/usr/bin/env python3
import os
import sys

# Path injections for workspace modules
for p in [
    "/opt/ws/src/sim_workbench/sil_nodes/ship_dynamics",
    "/opt/ws/install/shell_b_harness/local/lib/python3.10/dist-packages",
    "./src/sim_workbench/sil_nodes/ship_dynamics",
    "/opt/ws/src/sim_workbench/shell_b_harness",
    "./src/rl_workbench",
    "/opt/ws/install/rl_workbench/local/lib/python3.10/dist-packages"
]:
    abs_p = os.path.abspath(p)
    if abs_p not in sys.path:
        sys.path.insert(0, abs_p)

import rclpy
from rl_workbench.parallel_mc import run_parallel_mc, aggregate_mc_results


def test_parallel_mc_execution():
    print("\n--- Running Parallel Monte Carlo Campaign (4 episodes, 2 workers) ---", flush=True)
    seeds = [101, 102, 103, 104]
    
    # We use num_workers=2, and use_m7=False to run extremely fast and cleanly
    results = run_parallel_mc(
        seeds=seeds,
        num_workers=2,
        base_port=9200,
        base_domain=50,
        use_m7=False,
        max_steps=50  # Keep it short (50 steps = 1 second) for fast integration check
    )
    
    print(f"\nCollected {len(results)} results:")
    for r in results:
        print(f"  Result: {r}", flush=True)
        
    assert len(results) == len(seeds), f"Expected {len(seeds)} results, but got {len(results)}"
    
    for r in results:
        assert r["status"] == "success", f"Episode failed with error: {r.get('error')}\n{r.get('traceback')}"
        assert r["steps"] == 50, f"Expected 50 steps, but got {r['steps']}"
        assert r["min_separation_m"] > 0, "Invalid min_separation_m"
        assert "terminated" in r
        assert "truncated" in r

    # Perform result aggregation (Task D4)
    print("\nAggregating metrics...", flush=True)
    metrics = aggregate_mc_results(results)
    print(f"Aggregated Metrics: {metrics}", flush=True)

    assert metrics["total_episodes"] == 4
    assert metrics["success_rate"] == 1.0
    assert metrics["collision_rate"] == 0.0
    assert metrics["error_rate"] == 0.0
    assert metrics["avg_steps"] == 50.0
    assert metrics["min_separation_m"] > 0.0
    assert metrics["avg_min_separation_m"] > 0.0
        
    print("\nPARALLEL MONTE CARLO INTEGRATION CHECKS PASSED!", flush=True)
    sys.exit(0)


def main():
    try:
        test_parallel_mc_execution()
    except Exception as e:
        import traceback
        traceback.print_exc()
        sys.exit(1)
    finally:
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == "__main__":
    main()
