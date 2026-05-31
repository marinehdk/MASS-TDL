#!/usr/bin/env python3
import os
import sys
import math
import time

# Ensure we can import shell_b_harness and tools.sil modules
for p in [
    "/tmp",
    "/opt/ws",
    "/opt/ws/src/sim_workbench/shell_b_harness",
    "/opt/ws/install/shell_b_harness/local/lib/python3.10/dist-packages",
    "/opt/ws/install/shell_b_harness/lib/python3.10/site-packages",
    "./src/sim_workbench/shell_b_harness",
]:
    abs_p = os.path.abspath(p)
    if abs_p not in sys.path:
        sys.path.insert(0, abs_p)

from tools.sil.scenario_spec import ScenarioSpec
from tools.sil.simulate import simulate

def main():
    print("Starting real-stack batch simulation test...", flush=True)
    
    # Enable the real stack simulator
    os.environ["USE_REAL_STACK"] = "1"
    os.environ["REAL_STACK_PORT"] = "9110"
    os.environ["REAL_STACK_USE_M7"] = "1"
    
    # 2 scenarios to run
    scenario_files = [
        "/var/sil/scenarios/COLREGs测试/colreg-rule14-ho.yaml",
        "/var/sil/scenarios/COLREGs测试/colreg-rule13-ot.yaml"
    ]
    
    for yaml_path in scenario_files:
        print(f"\nLoading scenario: {yaml_path}", flush=True)
        spec = ScenarioSpec.from_file(yaml_path)
        
        # Pass 1: no-action
        print(f"Running pass 1: apply_avoidance=False...", flush=True)
        t_start = time.perf_counter()
        res_na = simulate(spec, apply_avoidance=False)
        t_na = time.perf_counter() - t_start
        print(f"  Stable: {res_na.stable}, DCPA: {res_na.dcpa_m:.2f} m, Wall time: {t_na:.2f} s", flush=True)
        
        # Pass 2: with-action
        print(f"Running pass 2: apply_avoidance=True...", flush=True)
        t_start = time.perf_counter()
        res_wa = simulate(spec, apply_avoidance=True)
        t_wa = time.perf_counter() - t_start
        print(f"  Stable: {res_wa.stable}, DCPA: {res_wa.dcpa_m:.2f} m, Wall time: {t_wa:.2f} s", flush=True)
        
        # Assertions
        assert res_na.stable, f"Scenario {yaml_path} no-avoidance pass was unstable!"
        assert res_wa.stable, f"Scenario {yaml_path} avoidance pass was unstable!"
        
        # DCPA validation
        assert res_na.dcpa_m < spec.pass_criteria.max_dcpa_no_action_m, (
            f"No-action DCPA {res_na.dcpa_m:.2f} m should be less than threshold "
            f"{spec.pass_criteria.max_dcpa_no_action_m} m"
        )
        assert res_wa.dcpa_m >= spec.pass_criteria.min_dcpa_with_action_m, (
            f"With-action DCPA {res_wa.dcpa_m:.2f} m should be at least threshold "
            f"{spec.pass_criteria.min_dcpa_with_action_m} m"
        )
        
        # Trajectories validation
        assert len(res_na.own_trajectory_sampled) > 0, "No trajectory points sampled for no-action pass!"
        assert len(res_wa.own_trajectory_sampled) > 0, "No trajectory points sampled for with-action pass!"
        
        print(f"Scenario {os.path.basename(yaml_path)} passed successfully!", flush=True)
        
    print("\nAll integration tests passed successfully!", flush=True)

if __name__ == "__main__":
    main()
