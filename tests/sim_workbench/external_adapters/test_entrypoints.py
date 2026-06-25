import importlib.util
import sys
from pathlib import Path
from importlib import import_module
from types import ModuleType, SimpleNamespace


def test_external_adapter_entrypoint_modules_are_importable_with_callable_main():
    module_names = [
        "external_adapters.tdl_ingress_node",
        "external_adapters.l2_route_plan_adaptor",
        "external_adapters.l2_route_seed",
        "external_adapters.route_out_tdl_node",
        "external_adapters.route_out_external_path_node",
    ]

    for module_name in module_names:
        module = import_module(module_name)
        assert callable(module.main)


def test_l2_route_plan_adaptor_resolves_gnc_route_plan(monkeypatch):
    # Track A: GNC ship_interfaces/RoutePlan is now the single route contract,
    # so l2_route_plan_adaptor resolves RoutePlan directly (no GncRoutePlan fallback).
    package = ModuleType("ship_interfaces")
    msg_module = ModuleType("ship_interfaces.msg")
    msg_module.RoutePlan = SimpleNamespace
    monkeypatch.setitem(sys.modules, "ship_interfaces", package)
    monkeypatch.setitem(sys.modules, "ship_interfaces.msg", msg_module)

    module_path = (
        Path(__file__).parents[3]
        / "src"
        / "sim_workbench"
        / "external_adapters"
        / "external_adapters"
        / "l2_route_plan_adaptor.py"
    )
    spec = importlib.util.spec_from_file_location(
        "external_adapters._l2_route_plan_adaptor_import_guard",
        module_path,
    )
    assert spec is not None
    assert spec.loader is not None
    module = importlib.util.module_from_spec(spec)

    spec.loader.exec_module(module)

    assert module.RoutePlan is SimpleNamespace
