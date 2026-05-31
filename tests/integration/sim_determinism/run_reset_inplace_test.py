#!/usr/bin/env python3
import os
import sys
import time
import rclpy

# Add paths to make sure we can import ship_dynamics and shell_b_harness
for p in [
    "/opt/ws/src/sim_workbench/sil_nodes/ship_dynamics",
    "/opt/ws/install/shell_b_harness/local/lib/python3.10/dist-packages",
    "./src/sim_workbench/sil_nodes/ship_dynamics"
]:
    abs_p = os.path.abspath(p)
    if abs_p not in sys.path:
        sys.path.insert(0, abs_p)

from shell_b_harness.simulator import ShellBSimulator

def record_episode(sim, steps=50) -> list[dict]:
    trajectory = []
    for _ in range(steps):
        state = sim.step()
        trajectory.append({
            "sim_t": state["sim_t"],
            "own_ship": dict(state["own_ship"]),
            "target_vessels": [dict(t) for t in state["target_vessels"]],
            "autopilot_enabled": state["autopilot_enabled"],
            "avoidance_active": state["avoidance_active"],
        })
    return trajectory

def assert_identical(traj1, traj2):
    assert len(traj1) == len(traj2), f"Lengths differ: {len(traj1)} vs {len(traj2)}"
    for i in range(len(traj1)):
        r = traj1[i]
        c = traj2[i]
        assert abs(r["sim_t"] - c["sim_t"]) < 1e-12, f"Step {i} sim_t differs: {r['sim_t']} vs {c['sim_t']}"
        for key in ["x", "y", "psi", "u", "v", "r"]:
            rv = r["own_ship"][key]
            cv = c["own_ship"][key]
            assert rv == cv, f"Step {i} own_ship.{key} differs: {rv} vs {cv}"
        for key in ["lat", "lon", "heading", "sog"]:
            rv = r["target_vessels"][0][key]
            cv = c["target_vessels"][0][key]
            assert rv == cv, f"Step {i} target_vessels[0].{key} differs: {rv} vs {cv}"

def assert_different(traj1, traj2):
    for i in range(len(traj1)):
        r = traj1[i]
        c = traj2[i]
        for key in ["x", "y", "psi"]:
            if r["own_ship"][key] != c["own_ship"][key]:
                return
    raise AssertionError("Trajectories are identical, but they should be different due to different seeds!")

def main():
    print("Initializing ShellBSimulator...", flush=True)
    sim = ShellBSimulator(port=9093, use_m7=True, verbose=True, ros_domain_id=42)
    try:
        # Run 1: Seed 42, cold start
        print("Starting Run 1 (Seed 42)...", flush=True)
        sim.reset(seed=42, in_place=False)
        traj1 = record_episode(sim, 50)
        
        # Run 2: Seed 42, in_place reset
        print("Resetting in_place with Seed 42...", flush=True)
        t_start = time.perf_counter()
        sim.reset(seed=42, in_place=True)
        t_end = time.perf_counter()
        elapsed_ms = (t_end - t_start) * 1000.0
        print(f"In-place reset took {elapsed_ms:.3f} ms", flush=True)
        assert elapsed_ms < 500.0, f"In-place reset took {elapsed_ms:.3f} ms, which exceeds the 500 ms budget!"
        
        traj2 = record_episode(sim, 50)
        
        # Run 3: Seed 100, in_place reset
        print("Resetting in_place with Seed 100...", flush=True)
        sim.reset(seed=100, in_place=True)
        traj3 = record_episode(sim, 50)
        
        # Assertions
        print("Asserting Run 1 and Run 2 are identical...", flush=True)
        assert_identical(traj1, traj2)
        print("Identical trajectory assertions passed!", flush=True)
        
        print("Asserting Run 1 and Run 3 are different...", flush=True)
        assert_different(traj1, traj3)
        print("Different trajectory assertions passed!", flush=True)
        
        print("In-place reset integration test PASSED successfully!", flush=True)
        sys.exit(0)
    except Exception as e:
        import traceback
        traceback.print_exc()
        sys.exit(1)
    finally:
        sim.close()
        if rclpy.ok():
            rclpy.shutdown()

if __name__ == '__main__':
    main()
