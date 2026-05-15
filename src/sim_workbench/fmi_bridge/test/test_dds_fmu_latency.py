"""Measure FMI co-sim single exchange latency: p95 < 10ms (per decision record §2.4 KPI)."""
from __future__ import annotations
import time
import numpy as np
from pathlib import Path


def test_fmu_exchange_latency_p95_under_10ms():
    from fmi_bridge.python.pyfmi_adapter import PyFmiAdapter

    fmu_path = Path("fcb_mmg_4dof.fmu")
    if not fmu_path.exists():
        from fmi_bridge.python.build_fmu import build
        fmu_path = build(str(fmu_path))

    adapter = PyFmiAdapter(str(fmu_path), instance_name="own_ship")
    adapter.load()
    adapter.setup_experiment(start_time=0.0, stop_time=20.0)
    adapter.enter_initialization_mode()
    adapter.exit_initialization_mode()

    latencies = []
    for i in range(1000):
        t = i * 0.02
        t0 = time.perf_counter()
        adapter.set_real("delta_cmd", 0.0)
        adapter.set_real("n_rps_cmd", 3.5)
        adapter.do_step(t, 0.02)
        _ = adapter.get_real("u")
        _ = adapter.get_real("v")
        _ = adapter.get_real("r")
        _ = adapter.get_real("x")
        _ = adapter.get_real("y")
        _ = adapter.get_real("psi")
        t1 = time.perf_counter()
        latencies.append((t1 - t0) * 1000.0)

    adapter.terminate()

    latencies = np.array(latencies)
    p50 = float(np.percentile(latencies, 50))
    p95 = float(np.percentile(latencies, 95))
    p99 = float(np.percentile(latencies, 99))
    avg = float(np.mean(latencies))

    print(f"\n  Latency profile (1000 exchanges):")
    print(f"    p50={p50:.3f}ms  p95={p95:.3f}ms  p99={p99:.3f}ms  avg={avg:.3f}ms")

    assert p95 < 10.0, \
        f"p95 latency {p95:.3f}ms exceeds 10ms KPI (decision record §2.4)"
    assert p99 < 20.0, \
        f"p99 latency {p99:.3f}ms exceeds reasonable worst-case bound"


def test_fmu_exchange_latency_p95_deterministic_across_runs():
    from fmi_bridge.python.pyfmi_adapter import PyFmiAdapter
    import numpy as np

    fmu_path = Path("fcb_mmg_4dof.fmu")
    if not fmu_path.exists():
        from fmi_bridge.python.build_fmu import build
        fmu_path = build(str(fmu_path))

    p95s = []
    for run_idx in range(3):
        adapter = PyFmiAdapter(str(fmu_path), instance_name=f"run{run_idx}")
        adapter.load()
        adapter.setup_experiment()
        adapter.enter_initialization_mode()
        adapter.exit_initialization_mode()

        lats = []
        for i in range(200):
            t = i * 0.02
            t0 = time.perf_counter()
            adapter.do_step(t, 0.02)
            _ = adapter.get_real("u")
            t1 = time.perf_counter()
            lats.append((t1 - t0) * 1000.0)
        adapter.terminate()

        p95 = float(np.percentile(lats, 95))
        p95s.append(p95)
        print(f"  Run {run_idx+1}: p95={p95:.3f}ms")

    p95s = np.array(p95s)
    assert np.std(p95s) < 2.0, \
        f"p95 latency unstable across runs: std={np.std(p95s):.3f}ms, values={p95s}"
