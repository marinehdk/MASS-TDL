from pathlib import Path
import importlib

from ais_twin.config import load_config


def test_safe_route_config_has_expected_bbox_and_capture_window():
    cfg = load_config(Path("src/sim_workbench/ais_twin/config/safe_route_aisstream.yaml"))

    assert cfg.provider == "aisstream"
    assert cfg.capture_duration_hours == 10.0
    assert cfg.route_path == Path("scenarios/集成测试/safe_route.yaml")
    assert cfg.bbox.lat_min == -4.503333
    assert cfg.bbox.lat_max == -1.136667
    assert cfg.bbox.lon_min == 104.786263
    assert cfg.bbox.lon_max == 108.513737
    assert cfg.risk_top_n == 20


def test_console_target_modules_are_importable_with_callable_main():
    for module_name in (
        "ais_twin.capture_cli",
        "ais_twin.replay_node",
        "ais_twin.debug_api",
    ):
        module = importlib.import_module(module_name)

        assert callable(module.main)
