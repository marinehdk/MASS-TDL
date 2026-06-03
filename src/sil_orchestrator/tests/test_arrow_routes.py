import pytest
from httpx import AsyncClient, ASGITransport
from sil_orchestrator.main import app
from sil_orchestrator import arrow_routes
from pathlib import Path

@pytest.mark.asyncio
async def test_export_arrow_not_found():
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.post("/api/v1/export/arrow", json={"run_id": "nonexistent-run-id"})
        assert resp.status_code == 404
        assert resp.json()["detail"] == "Run not found"

@pytest.mark.asyncio
async def test_export_arrow_success(tmp_path, monkeypatch):
    # Setup test run directory
    run_id = "test-run-id"
    run_dir = tmp_path / run_id
    run_dir.mkdir()
    
    # Mock RUN_DIR inside arrow_routes
    monkeypatch.setattr(arrow_routes, "RUN_DIR", tmp_path)
    
    # Mock subprocess.run in _build_arrow to do nothing and succeed
    import subprocess
    class MockCompletedProcess:
        returncode = 0
        stderr = ""
        stdout = ""
    
    def mock_run(*args, **kwargs):
        # Write dummy arrow file
        (run_dir / "replay.arrow").write_text("dummy")
        return MockCompletedProcess()
        
    monkeypatch.setattr(subprocess, "run", mock_run)
    
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        # Request export
        resp = await client.post("/api/v1/export/arrow", json={"run_id": run_id})
        assert resp.status_code == 200
        assert resp.json() == {"status": "processing", "run_id": run_id}
        
        # Manually run the background task logic to simulate background_tasks.add_task
        arrow_routes._build_arrow(run_id)
        
        # Check status
        resp_status = await client.get(f"/api/v1/export/arrow/status/{run_id}")
        assert resp_status.status_code == 200
        status_data = resp_status.json()
        assert status_data["status"] == "ready"
        assert "replay.arrow" in status_data["path"]
