"""Test PyFmiAdapter: FMU loading + doStep + variable get/set."""
from __future__ import annotations
from pathlib import Path


def test_pyfmi_adapter_loads_real_fmu():
    """PyFmiAdapter loads fcb_mmg_4dof.fmu and enumerates variables."""
    from fmi_bridge.python.pyfmi_adapter import PyFmiAdapter

    fmu_path = Path("fcb_mmg_4dof.fmu")
    if not fmu_path.exists():
        from fmi_bridge.python.build_fmu import build
        fmu_path = build(str(fmu_path))

    adapter = PyFmiAdapter(str(fmu_path), instance_name="own_ship")
    adapter.load()

    assert adapter.instance_name == "own_ship"
    var_names = adapter.get_variable_names()
    for required in ["u", "v", "r", "x", "y", "psi", "delta_cmd", "n_rps_cmd"]:
        assert required in var_names, f"Missing FMU variable: {required}"
    adapter.terminate()


def test_pyfmi_adapter_dostep_1000_replay_deterministic():
    """1000 doStep reruns produce identical trajectory."""
    from fmi_bridge.python.pyfmi_adapter import PyFmiAdapter

    fmu_path = Path("fcb_mmg_4dof.fmu")
    if not fmu_path.exists():
        from fmi_bridge.python.build_fmu import build
        fmu_path = build(str(fmu_path))

    def run_1000_steps() -> list[float]:
        adapter = PyFmiAdapter(str(fmu_path), instance_name="own_ship")
        adapter.load()
        adapter.setup_experiment(start_time=0.0, stop_time=20.0)
        adapter.enter_initialization_mode()
        adapter.exit_initialization_mode()
        traj = []
        for i in range(1000):
            t = i * 0.02
            adapter.do_step(t, 0.02)
            traj.append(adapter.get_real("u"))
        adapter.terminate()
        return traj

    traj1 = run_1000_steps()
    traj2 = run_1000_steps()

    assert len(traj1) == 1000
    assert len(traj2) == 1000
    for i in range(1000):
        assert abs(traj1[i] - traj2[i]) < 1e-9, \
            f"Non-deterministic output at step {i}: {traj1[i]} != {traj2[i]}"
