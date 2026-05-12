"""Tests for ScenarioAuthoringNode — YAML CRUD lifecycle."""
import tempfile
from pathlib import Path

from scenario_authoring.node import ScenarioAuthoringNode


def test_list_empty() -> None:
    """Empty directory yields no scenarios."""
    with tempfile.TemporaryDirectory() as d:
        node = ScenarioAuthoringNode(d)
        assert node.list_scenarios() == []


def test_create_get_delete() -> None:
    """Full create -> get -> delete lifecycle."""
    with tempfile.TemporaryDirectory() as d:
        node = ScenarioAuthoringNode(d)

        # Create
        result = node.create_scenario("name: test\n")
        sid = result["scenario_id"]
        assert len(sid) == 12
        assert len(result["hash"]) == 64  # SHA-256 hex

        # Verify file on disk
        assert (Path(d) / f"{sid}.yaml").exists()

        # Get
        detail = node.get_scenario(sid)
        assert detail is not None
        assert detail["yaml_content"] == "name: test\n"

        # List includes it
        scenarios = node.list_scenarios()
        assert len(scenarios) == 1
        assert scenarios[0]["id"] == sid

        # Delete
        assert node.delete_scenario(sid) is True
        assert node.get_scenario(sid) is None
        assert node.list_scenarios() == []


def test_delete_missing() -> None:
    """Deleting a non-existent scenario returns False."""
    with tempfile.TemporaryDirectory() as d:
        node = ScenarioAuthoringNode(d)
        assert node.delete_scenario("nonexistent") is False


def test_get_missing() -> None:
    """Getting a non-existent scenario returns None."""
    with tempfile.TemporaryDirectory() as d:
        node = ScenarioAuthoringNode(d)
        assert node.get_scenario("missing") is None


def test_hash_consistency() -> None:
    """Same content produces same hash regardless of scenario_id."""
    with tempfile.TemporaryDirectory() as d:
        node = ScenarioAuthoringNode(d)
        content = "name: head-on\nencounter: cross\n"
        r1 = node.create_scenario(content)
        r2 = node.create_scenario(content)
        assert r1["hash"] == r2["hash"]
        assert r1["scenario_id"] != r2["scenario_id"]


def test_persists_across_instances() -> None:
    """Scenario survives node re-creation (same dir)."""
    with tempfile.TemporaryDirectory() as d:
        node = ScenarioAuthoringNode(d)
        result = node.create_scenario("name: persist\n")
        sid = result["scenario_id"]

        node2 = ScenarioAuthoringNode(d)
        detail = node2.get_scenario(sid)
        assert detail is not None
        assert detail["yaml_content"] == "name: persist\n"
