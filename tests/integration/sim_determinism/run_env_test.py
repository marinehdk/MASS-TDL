#!/usr/bin/env python3
import os
import sys
import numpy as np

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
from gymnasium.utils.env_checker import check_env
from rl_workbench.envs.massl3_env import MASSL3Env


def test_gym_compliance():
    print("\n--- 1. Testing Gym Compliance ---", flush=True)
    env = MASSL3Env(port=9095, use_m7=False, verbose=False)
    try:
        check_env(env)
        print("MASSL3Env is standard Gymnasium compliant!", flush=True)
    finally:
        env.close()


def test_random_rollout():
    print("\n--- 2. Testing 10-step Random Rollout ---", flush=True)
    env = MASSL3Env(port=9095, use_m7=False, verbose=False)
    try:
        obs, info = env.reset(seed=42)
        print(f"Initial Obs: {obs}")
        for step in range(10):
            action = env.action_space.sample()
            obs, reward, terminated, truncated, info = env.step(action)
            print(f"Step {step + 1}: Action={action}, Reward={reward:.4f}, Terminated={terminated}, Truncated={truncated}", flush=True)
    finally:
        env.close()


def test_reproducibility():
    print("\n--- 3. Testing Reset Reproducibility ---", flush=True)
    env = MASSL3Env(port=9095, use_m7=False, verbose=False)
    try:
        # Run 1: seed 42
        obs_seq_1 = []
        obs, info = env.reset(seed=42)
        obs_seq_1.append(obs.copy())
        
        # Apply 5 steps of constant actions
        action = np.array([10.0, 12.0], dtype=np.float32)
        for _ in range(5):
            obs, reward, terminated, truncated, info = env.step(action)
            obs_seq_1.append(obs.copy())
            
        # Run 2: seed 42 (in-place reset)
        obs_seq_2 = []
        obs, info = env.reset(seed=42)
        obs_seq_2.append(obs.copy())
        for _ in range(5):
            obs, reward, terminated, truncated, info = env.step(action)
            obs_seq_2.append(obs.copy())
            
        # Verify identity
        for idx, (o1, o2) in enumerate(zip(obs_seq_1, obs_seq_2)):
            np.testing.assert_array_equal(o1, o2, err_msg=f"Observation mismatch at step {idx}")
            
        print("Reset reproducibility verified successfully (seeded determinism matches exactly)!", flush=True)
    finally:
        env.close()


def main():
    try:
        test_gym_compliance()
        test_random_rollout()
        test_reproducibility()
        print("\nALL INTEGRATION CHECKS PASSED!", flush=True)
        sys.exit(0)
    except Exception as e:
        import traceback
        traceback.print_exc()
        sys.exit(1)
    finally:
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == "__main__":
    main()
