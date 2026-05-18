import json
import pytest
from httpx import AsyncClient, ASGITransport
from sil_orchestrator.main import app

@pytest.mark.asyncio
async def test_sse_stream_returns_events():
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        async with client.stream("GET", "/api/v1/selfcheck/stream?scenario_id=test_demo") as resp:
            assert resp.status_code == 200
            assert resp.headers["content-type"] == "text/event-stream"
            events = []
            async for line in resp.aiter_lines():
                if line.startswith("data: "):
                    data = json.loads(line[6:])
                    events.append(data)
                    if data.get("type") == "complete":
                        break
            assert len(events) >= 2
            assert events[-1]["type"] == "complete"
            assert events[-1]["go_no_go"] in ("GO", "NO-GO")

@pytest.mark.asyncio
async def test_sse_stream_missing_scenario():
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        async with client.stream("GET", "/api/v1/selfcheck/stream") as resp:
            assert resp.status_code == 200
