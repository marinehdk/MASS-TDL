import pytest
import subprocess
import time
from httpx import AsyncClient, ASGITransport
from sil_orchestrator.main import app
from sil_orchestrator import arrow_routes
from pathlib import Path

@pytest.fixture(autouse=True)
def clean_status():
    # Clear the global status dict before/after each test
    with arrow_routes._status_lock:
        arrow_routes._arrow_status.clear()
    yield
    with arrow_routes._status_lock:
        arrow_routes._arrow_status.clear()

@pytest.mark.asyncio
async def test_export_arrow_not_found():
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.post("/api/v1/export/arrow", json={"run_id": "nonexistent-run-id"})
        assert resp.status_code == 404
        assert resp.json()["detail"] == "Run not found"

@pytest.mark.asyncio
async def test_export_arrow_success(tmp_path, monkeypatch):
    run_id = "test-run-id"
    run_dir = tmp_path / run_id
    run_dir.mkdir()
    
    monkeypatch.setattr(arrow_routes, "RUN_DIR", tmp_path)
    
    class MockCompletedProcess:
        returncode = 0
        stderr = ""
        stdout = ""
    
    def mock_run(*args, **kwargs):
        (run_dir / "replay.arrow").write_text("dummy")
        return MockCompletedProcess()
        
    monkeypatch.setattr(subprocess, "run", mock_run)
    
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.post("/api/v1/export/arrow", json={"run_id": run_id})
        assert resp.status_code == 200
        assert resp.json() == {"status": "processing", "run_id": run_id}
        
        arrow_routes._build_arrow(run_id)
        
        resp_status = await client.get(f"/api/v1/export/arrow/status/{run_id}")
        assert resp_status.status_code == 200
        status_data = resp_status.json()
        assert status_data["status"] == "ready"
        assert "replay.arrow" in status_data["path"]

@pytest.mark.asyncio
async def test_export_arrow_path_traversal(tmp_path, monkeypatch):
    monkeypatch.setattr(arrow_routes, "RUN_DIR", tmp_path)
    
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        # Test traversing up via POST
        resp = await client.post("/api/v1/export/arrow", json={"run_id": "../etc/passwd"})
        assert resp.status_code == 400
        assert resp.json()["detail"] == "Invalid run_id"

        # Test absolute path outside RUN_DIR via POST
        resp = await client.post("/api/v1/export/arrow", json={"run_id": "/etc/passwd"})
        assert resp.status_code == 400
        assert resp.json()["detail"] == "Invalid run_id"

        # Test path traversal via GET status
        # Note: we use query-like params or url-encoded traversal to hit the endpoint if routing allows.
        # It may be rejected as 404 by the router or client normalization before hitting the handler.
        resp = await client.get("/api/v1/export/arrow/status/..%2Fetc%2Fpasswd")
        assert resp.status_code in (400, 404)

@pytest.mark.asyncio
async def test_export_arrow_deduplication_ready(tmp_path, monkeypatch):
    run_id = "test-run-id-dedup"
    run_dir = tmp_path / run_id
    run_dir.mkdir()
    
    monkeypatch.setattr(arrow_routes, "RUN_DIR", tmp_path)
    
    # 1. Existing file on disk check
    arrow_file = run_dir / "replay.arrow"
    arrow_file.write_text("existing-replay-data")
    
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        # Request should immediately return ready
        resp = await client.post("/api/v1/export/arrow", json={"run_id": run_id})
        assert resp.status_code == 200
        data = resp.json()
        assert data["status"] == "ready"
        assert data["path"] == str(arrow_file)
        
        # Verify status dict was populated
        status_resp = await client.get(f"/api/v1/export/arrow/status/{run_id}")
        assert status_resp.status_code == 200
        assert status_resp.json()["status"] == "ready"

@pytest.mark.asyncio
async def test_export_arrow_deduplication_processing(tmp_path, monkeypatch):
    run_id = "test-run-id-processing"
    run_dir = tmp_path / run_id
    run_dir.mkdir()
    
    monkeypatch.setattr(arrow_routes, "RUN_DIR", tmp_path)
    
    # Pre-populate status to processing
    with arrow_routes._status_lock:
        arrow_routes._arrow_status[run_id] = {"status": "processing", "_created": time.time()}
        
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        # Calling post/arrow should return processing immediately
        resp = await client.post("/api/v1/export/arrow", json={"run_id": run_id})
        assert resp.status_code == 200
        assert resp.json() == {"status": "processing", "run_id": run_id}

@pytest.mark.asyncio
async def test_export_arrow_subprocess_failure(tmp_path, monkeypatch):
    run_id = "test-run-id-failure"
    run_dir = tmp_path / run_id
    run_dir.mkdir()
    
    monkeypatch.setattr(arrow_routes, "RUN_DIR", tmp_path)
    
    # Mock subprocess.run to raise an exception
    def mock_run_raise(*args, **kwargs):
        raise RuntimeError("Subprocess command failed to launch")
        
    monkeypatch.setattr(subprocess, "run", mock_run_raise)
    
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.post("/api/v1/export/arrow", json={"run_id": run_id})
        assert resp.status_code == 200
        
        # Manually invoke builder to trigger the error path
        arrow_routes._build_arrow(run_id)
        
        # Check status
        resp_status = await client.get(f"/api/v1/export/arrow/status/{run_id}")
        assert resp_status.status_code == 200
        status_data = resp_status.json()
        assert status_data["status"] == "error"
        assert "Subprocess command failed to launch" in status_data["detail"]
