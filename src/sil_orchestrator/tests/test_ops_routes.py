import pytest
from httpx import AsyncClient, ASGITransport
from sil_orchestrator.main import app

@pytest.mark.asyncio
async def test_restart_node_rejects_invalid_name():
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.post("/api/v1/ops/restart_node?name=;%20rm%20-rf")
        assert resp.status_code == 422

@pytest.mark.asyncio
async def test_clear_hash_cache_accepts_valid_id():
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.post("/api/v1/ops/clear_hash_cache?scenario_id=test_valid")
        assert resp.status_code == 200
        data = resp.json()
        assert data["success"] is True

@pytest.mark.asyncio
async def test_ensure_asdr_dir():
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.post("/api/v1/ops/ensure_asdr_dir?run_id=rx-test-001")
        assert resp.status_code == 200
        data = resp.json()
        assert data["success"] is True
