"""W10B TDD: verify demo endpoints are gone and demo imports removed from main.py."""
import ast
import inspect
import textwrap

import httpx
import pytest
from fastapi.testclient import TestClient


# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------

def _get_main_module():
    import importlib
    import sil_orchestrator.main as mod
    return mod


# ---------------------------------------------------------------------------
# 10B-1a: /api/v1/demo/telemetry returns 404
# ---------------------------------------------------------------------------

@pytest.mark.anyio
async def test_demo_telemetry_endpoint_404():
    from sil_orchestrator.main import app
    transport = httpx.ASGITransport(app=app)
    async with httpx.AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.get("/api/v1/demo/telemetry")
    assert resp.status_code == 404, (
        f"Expected 404, got {resp.status_code}; demo endpoint must be removed"
    )


# ---------------------------------------------------------------------------
# 10B-1b: /api/v1/demo/reset returns 404
# ---------------------------------------------------------------------------

@pytest.mark.anyio
async def test_demo_reset_endpoint_404():
    from sil_orchestrator.main import app
    transport = httpx.ASGITransport(app=app)
    async with httpx.AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.post("/api/v1/demo/reset")
    assert resp.status_code == 404, (
        f"Expected 404, got {resp.status_code}; demo endpoint must be removed"
    )


# ---------------------------------------------------------------------------
# 10B-1c: main.py has no demo_avoidance / demo_scorer imports
# ---------------------------------------------------------------------------

def test_demo_modules_not_imported():
    """main.py must not import demo_avoidance or demo_scorer at AST level."""
    import sil_orchestrator.main as mod

    source = inspect.getsource(mod)
    tree = ast.parse(textwrap.dedent(source))

    import_names: list[str] = []
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            import_names.extend(alias.name for alias in node.names)
        elif isinstance(node, ast.ImportFrom):
            if node.module:
                import_names.append(node.module)
            import_names.extend(alias.name for alias in node.names)

    assert not any("demo_avoidance" in n for n in import_names), (
        f"main.py must not import demo_avoidance; found in imports: {import_names}"
    )
    assert not any("demo_scorer" in n for n in import_names), (
        f"main.py must not import demo_scorer; found in imports: {import_names}"
    )
