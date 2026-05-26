"""Tests for scenario store and REST endpoints."""
import tempfile
from pathlib import Path
from fastapi.testclient import TestClient
from sil_orchestrator.scenario_store import ScenarioStore
from sil_orchestrator.main import app
from sil_orchestrator.scenario_routes import store as api_store


class TestScenarioStore:
    def test_list_empty(self):
        with tempfile.TemporaryDirectory() as d:
            store = ScenarioStore(Path(d))
            assert store.list() == []

    def test_create_get_list_delete(self):
        with tempfile.TemporaryDirectory() as d:
            store = ScenarioStore(Path(d))
            result = store.create("name: test\nversion: 1\n")
            sid = result["scenario_id"]
            assert len(sid) == 12

            detail = store.get(sid)
            assert detail is not None
            assert detail["yaml_content"] == "name: test\nversion: 1\n"
            assert len(detail["hash"]) == 64

            assert len(store.list()) == 1

            assert store.delete(sid) is True
            assert store.get(sid) is None

    def test_get_nonexistent(self):
        with tempfile.TemporaryDirectory() as d:
            store = ScenarioStore(Path(d))
            assert store.get("nonexistent") is None

    def test_validate_empty_yaml(self):
        store = ScenarioStore()
        result = store.validate("")
        assert result["valid"] is False
        assert len(result["errors"]) > 0

    def test_validate_nonempty_yaml(self):
        store = ScenarioStore()
        from sil_orchestrator.config import SCENARIO_DIR
        yaml_file = SCENARIO_DIR / "IMAZU标准测试" / "imazu-01-ho.yaml"
        if yaml_file.exists():
            content = yaml_file.read_text()
        else:
            content = "title: test\nstartTime: '2026-01-01T00:00:00Z'\nownShip:\n  static:\n    id: 1\n    shipType: Cargo\n    name: test\n    mmsi: 123456789\n  initial:\n    position:\n      latitude: 0\n      longitude: 0\n    cog: 0\n    sog: 0\n    heading: 0\n    navStatus: Under way\n  model: test\n  controller: test\ntargetShips: []\nenvironment:\n  wind:\n    dir_deg: 0\n    speed_mps: 0\n  current:\n    dir_deg: 0\n    speed_mps: 0\n  visibility_nm: 5\n"
        result = store.validate(content)
        assert result["valid"] is True, f"Validation errors: {result['errors']}"


class TestScenarioAPI:
    @classmethod
    def setup_class(cls):
        cls._tmpdir = tempfile.TemporaryDirectory()
        api_store._dir = Path(cls._tmpdir.name)

    @classmethod
    def teardown_class(cls):
        cls._tmpdir.cleanup()

    def test_list_returns_array(self):
        client = TestClient(app)
        resp = client.get("/api/v1/scenarios")
        assert resp.status_code == 200
        assert isinstance(resp.json(), list)

    def test_post_and_get(self):
        client = TestClient(app)
        resp = client.post("/api/v1/scenarios", json={"yaml_content": "name: api-test\n"})
        assert resp.status_code == 200
        sid = resp.json()["scenario_id"]

        resp = client.get(f"/api/v1/scenarios/{sid}")
        assert resp.status_code == 200
        assert resp.json()["yaml_content"] == "name: api-test\n"

    def test_get_nonexistent_404(self):
        client = TestClient(app)
        resp = client.get("/api/v1/scenarios/nonexistent-id")
        assert resp.status_code == 404
