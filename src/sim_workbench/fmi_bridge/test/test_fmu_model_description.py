"""Verify FCBSimulator.export_fmu_interface() returns valid FMI 2.0 spec."""


def test_export_fmu_interface_produces_valid_spec():
    """FCBSimulator.export_fmu_interface() returns valid 22-variable FMI 2.0 spec."""
    try:
        from fcb_mmg_py import FCBSimulator
    except ImportError:
        import pytest
        pytest.skip("fcb_mmg_py not built (requires colcon build with pybind11)")

    sim = FCBSimulator()
    spec = sim.export_fmu_interface()

    assert spec.fmi_version == "2.0"
    assert spec.model_name == "FCBShipDynamics"
    assert spec.default_step_size == 0.02
    assert len(spec.variables) == 22

    var_names = {v.name for v in spec.variables}
    for required in ["u", "v", "r", "x", "y", "psi", "delta_cmd", "n_rps_cmd"]:
        assert required in var_names, f"Missing variable: {required}"

    inputs = [v for v in spec.variables if v.causality == "input"]
    outputs = [v for v in spec.variables if v.causality == "output"]
    params = [v for v in spec.variables if v.causality == "parameter"]
    assert len(inputs) == 6   # delta_cmd + n_rps_cmd + 4 disturbance
    assert len(outputs) == 11
    assert len(params) == 5
