import os
import sys
import time
import multiprocessing
import queue
from math import radians, cos, sqrt

# Inject paths to import rl_workbench and shell_b_harness if not already importable
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

# rl-isolation-ok: parallel MC imports env
from rl_workbench.envs.massl3_env import MASSL3Env


def mc_worker_loop(
    worker_id: int,
    task_queue: multiprocessing.Queue,
    result_queue: multiprocessing.Queue,
    base_port: int,
    base_domain: int,
    use_m7: bool,
    max_steps: int
):
    """
    Persistent worker process loop. Instantiates the Gym env once
    and runs rollouts for multiple episodes using in-place resets.
    """
    port = base_port + worker_id
    domain = base_domain + worker_id

    # Create Gym environment instance once for this worker
    env = MASSL3Env(
        port=port,
        use_m7=use_m7,
        verbose=False,
        ros_domain_id=domain,
        headless=True
    )

    try:
        while True:
            try:
                task = task_queue.get(timeout=3.0)
            except queue.Empty:
                break

            if task is None:
                break

            seed = task["seed"]

            try:
                # Reset environment (leveraging in-place reset optimization)
                obs, info = env.reset(seed=seed)

                step_count = 0
                min_distance = float("inf")
                total_reward = 0.0

                own_x, own_y = obs[0], obs[1]
                tgt_lat, tgt_lon = obs[6], obs[7]

                y_t = (tgt_lat - 63.44) * 111120.0
                x_t = (tgt_lon - 10.38) * 111120.0 * cos(radians(63.44))
                d0 = sqrt((own_x - x_t) ** 2 + (own_y - y_t) ** 2)
                min_distance = min(min_distance, d0)

                terminated = False
                truncated = False

                action = env.action_space.sample()

                while not (terminated or truncated) and step_count < max_steps:
                    # Periodically sample a new action (stochastic policy)
                    if step_count % 100 == 0:
                        action = env.action_space.sample()

                    obs, reward, terminated, truncated, state = env.step(action)

                    own_x, own_y = obs[0], obs[1]
                    tgt_lat, tgt_lon = obs[6], obs[7]

                    y_t = (tgt_lat - 63.44) * 111120.0
                    x_t = (tgt_lon - 10.38) * 111120.0 * cos(radians(63.44))
                    dist = sqrt((own_x - x_t) ** 2 + (own_y - y_t) ** 2)
                    min_distance = min(min_distance, dist)

                    total_reward += reward
                    step_count += 1

                result_queue.put({
                    "seed": seed,
                    "worker_id": worker_id,
                    "steps": step_count,
                    "min_separation_m": min_distance,
                    "total_reward": total_reward,
                    "terminated": terminated,
                    "truncated": truncated,
                    "success": not terminated,
                    "status": "success"
                })
            except Exception as e:
                import traceback
                result_queue.put({
                    "seed": seed,
                    "worker_id": worker_id,
                    "error": str(e),
                    "traceback": traceback.format_exc(),
                    "status": "error"
                })
    finally:
        env.close()


def run_parallel_mc(
    seeds: list[int],
    num_workers: int = 4,
    base_port: int = 9200,
    base_domain: int = 50,
    use_m7: bool = False,
    max_steps: int = 200
) -> list[dict]:
    """
    Spawns multiple workers in parallel to run Monte Carlo simulations.
    """
    task_queue = multiprocessing.Queue()
    result_queue = multiprocessing.Queue()

    # Enqueue tasks
    for seed in seeds:
        task_queue.put({"seed": seed})

    # Enqueue stop sentinels
    for _ in range(num_workers):
        task_queue.put(None)

    processes = []
    for w_id in range(num_workers):
        p = multiprocessing.Process(
            target=mc_worker_loop,
            args=(w_id, task_queue, result_queue, base_port, base_domain, use_m7, max_steps)
        )
        p.start()
        processes.append(p)

    results = []
    # Collect results
    total_tasks = len(seeds)
    while len(results) < total_tasks:
        try:
            res = result_queue.get(timeout=5.0)
            results.append(res)
        except queue.Empty:
            # Check if all processes are dead
            if all(not p.is_alive() for p in processes):
                break

    for p in processes:
        p.join()

    return results


def aggregate_mc_results(results: list[dict]) -> dict:
    """
    Aggregates Monte Carlo simulation results to produce key metrics.
    """
    total = len(results)
    if total == 0:
        return {
            "total_episodes": 0,
            "success_rate": 0.0,
            "collision_rate": 0.0,
            "error_rate": 0.0,
            "avg_steps": 0.0,
            "avg_reward": 0.0,
            "min_separation_m": 0.0,
            "avg_min_separation_m": 0.0
        }

    successes = 0
    collisions = 0
    errors = 0
    total_steps = 0
    total_reward = 0.0
    min_sep = float("inf")
    total_sep = 0.0

    for r in results:
        if r.get("status") == "error":
            errors += 1
            continue

        if r.get("terminated"):
            collisions += 1
        else:
            successes += 1

        total_steps += r.get("steps", 0)
        total_reward += r.get("total_reward", 0.0)
        sep = r.get("min_separation_m", 0.0)
        min_sep = min(min_sep, sep)
        total_sep += sep

    valid_runs = total - errors
    return {
        "total_episodes": total,
        "success_rate": successes / valid_runs if valid_runs > 0 else 0.0,
        "collision_rate": collisions / valid_runs if valid_runs > 0 else 0.0,
        "error_rate": errors / total,
        "avg_steps": total_steps / valid_runs if valid_runs > 0 else 0.0,
        "avg_reward": total_reward / valid_runs if valid_runs > 0 else 0.0,
        "min_separation_m": min_sep if min_sep != float("inf") else 0.0,
        "avg_min_separation_m": total_sep / valid_runs if valid_runs > 0 else 0.0
    }
