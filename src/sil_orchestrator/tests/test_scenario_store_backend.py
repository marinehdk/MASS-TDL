"""W10D TDD: scenario_store.py default backend is 'ros2'."""
import textwrap
import tempfile
from pathlib import Path

import pytest

from sil_orchestrator.scenario_store import ScenarioStore


def _make_store(tmp_path: Path) -> ScenarioStore:
    return ScenarioStore(base_dir=tmp_path)


def _write_yaml(tmp_path: Path, name: str, content: str) -> None:
    (tmp_path / f"{name}.yaml").write_text(content)


# ---------------------------------------------------------------------------
# Test: YAML with no backend field defaults to "ros2"
# ---------------------------------------------------------------------------

def test_no_backend_field_defaults_to_ros2(tmp_path):
    """A scenario YAML without metadata.simulation_settings.backend must return 'ros2'."""
    yaml_content = textwrap.dedent("""\
        title: Test scenario
        ownShip:
          static: {id: 1}
          initial:
            position: {latitude: 63.0, longitude: 10.0}
    """)
    _write_yaml(tmp_path, "test-scenario", yaml_content)
    store = _make_store(tmp_path)
    detail = store.get("test-scenario")
    assert detail is not None
    assert detail["backend"] == "ros2", (
        f"Expected default backend 'ros2', got '{detail['backend']}'"
    )


# ---------------------------------------------------------------------------
# Test: YAML with explicit backend preserves the value
# ---------------------------------------------------------------------------

def test_explicit_backend_is_preserved(tmp_path):
    """A scenario YAML with backend: custom must preserve that value."""
    yaml_content = textwrap.dedent("""\
        title: Test scenario custom
        metadata:
          simulation_settings:
            backend: custom
        ownShip:
          static: {id: 1}
          initial:
            position: {latitude: 63.0, longitude: 10.0}
    """)
    _write_yaml(tmp_path, "test-custom", yaml_content)
    store = _make_store(tmp_path)
    detail = store.get("test-custom")
    assert detail is not None
    assert detail["backend"] == "custom", (
        f"Expected backend 'custom', got '{detail['backend']}'"
    )


# ---------------------------------------------------------------------------
# Test: YAML with explicit backend: ros2 returns ros2
# ---------------------------------------------------------------------------

def test_explicit_ros2_backend(tmp_path):
    """A scenario YAML with backend: ros2 must return 'ros2'."""
    yaml_content = textwrap.dedent("""\
        title: Test scenario ros2
        metadata:
          simulation_settings:
            backend: ros2
        ownShip:
          static: {id: 1}
          initial:
            position: {latitude: 63.0, longitude: 10.0}
    """)
    _write_yaml(tmp_path, "test-ros2", yaml_content)
    store = _make_store(tmp_path)
    detail = store.get("test-ros2")
    assert detail is not None
    assert detail["backend"] == "ros2", (
        f"Expected backend 'ros2', got '{detail['backend']}'"
    )


# ---------------------------------------------------------------------------
# Test: YAML scenario list extracts coordinates and domain
# ---------------------------------------------------------------------------

def test_scenario_list_extracts_coordinates_and_domain(tmp_path):
    yaml_content = textwrap.dedent("""\
        title: Test scenario coordinate
        ownShip:
          initial:
            position: {latitude: -2.5, longitude: 106.4}
        metadata:
          odd_cell:
            domain: coastal_archipelago
    """)
    _write_yaml(tmp_path, "test-malacca", yaml_content)
    store = _make_store(tmp_path)
    scenarios = store.list()
    assert len(scenarios) == 1
    s = scenarios[0]
    assert s["latitude"] == -2.5
    assert s["longitude"] == 106.4
    assert s["odd_domain"] == "coastal_archipelago"

