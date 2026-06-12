from importlib import import_module


def test_external_adapter_entrypoint_modules_are_importable_with_callable_main():
    module_names = [
        "external_adapters.tdl_ingress_node",
        "external_adapters.route_out_tdl_node",
        "external_adapters.route_out_external_path_node",
    ]

    for module_name in module_names:
        module = import_module(module_name)
        assert callable(module.main)
