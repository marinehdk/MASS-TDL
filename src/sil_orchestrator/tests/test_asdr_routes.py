"""TDD tests for W10A: asdr_routes refactor — cache-based event generation.

Tests verify:
1. _generate_asdr_events uses a cache dict, not _avoidance_state / demo globals.
2. Source contains no _avoidance_state or 'demo' references.
3. /api/v1/asdr/events endpoint works with empty cache (returns empty lists).
4. Events are correctly derived from synthetic cache entries.
"""
import inspect

import pytest


# ---------------------------------------------------------------------------
# 10A-1: module imports no demo at module level
# ---------------------------------------------------------------------------

def test_asdr_routes_no_demo_module_import():
    """asdr_routes must not import demo_avoidance or AvoidanceState."""
    import sil_orchestrator.asdr_routes as mod
    import ast, textwrap

    source = inspect.getsource(mod)

    # Parse import statements only — do not flag docstring/comment mentions
    tree = ast.parse(textwrap.dedent(source))
    import_names = []
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            import_names.extend(alias.name for alias in node.names)
        elif isinstance(node, ast.ImportFrom):
            if node.module:
                import_names.append(node.module)
            import_names.extend(alias.name for alias in node.names)

    assert not any("demo_avoidance" in n for n in import_names), (
        f"asdr_routes.py must not import demo_avoidance; found: {import_names}"
    )
    assert not any("AvoidanceState" in n for n in import_names), (
        f"asdr_routes.py must not import AvoidanceState; found: {import_names}"
    )


def test_asdr_routes_source_no_avoidance_state_or_demo():
    """_generate_asdr_events source must not contain _avoidance_state or 'demo'."""
    from sil_orchestrator.asdr_routes import _generate_asdr_events

    src = inspect.getsource(_generate_asdr_events)
    assert "_avoidance_state" not in src, (
        "_generate_asdr_events must not reference _avoidance_state"
    )
    assert "demo" not in src.lower(), (
        "_generate_asdr_events must not contain any 'demo' reference"
    )


# ---------------------------------------------------------------------------
# 10A-2/3: _generate_asdr_events accepts (sim_time) and sources from cache
# ---------------------------------------------------------------------------

def _make_cache_snapshot(
    *,
    threat_mmsi: int = 123456789,
    threat_cpa_nm: float = 0.25,
    rule: str = "Rule 14",
    rationale: str = "mpc starboard",
) -> dict:
    """Build a synthetic cache snapshot matching MessageCache.get_snapshot() output."""
    return {
        "threat_state": [
            {
                "stamp": 25.0,
                "target_mmsi": threat_mmsi,
                "cpa_nm": threat_cpa_nm,
                "tcpa_s": 80.0,
            }
        ],
        "rule_assessment": [
            {
                "stamp": 49.0,
                "applicable_rule": rule,
                "give_way": "OWN",
            }
        ],
        "behavior_plan": [
            {
                "stamp": 52.0,
                "rationale": rationale,
                "action": "STARBOARD_TURN",
                "delta_heading_deg": 35.0,
            }
        ],
        "actuator_cmd": [],
    }


def test_generate_asdr_events_signature():
    """_generate_asdr_events must accept (sim_time: float) — no state/min_cpa args."""
    from sil_orchestrator.asdr_routes import _generate_asdr_events

    sig = inspect.signature(_generate_asdr_events)
    params = list(sig.parameters.keys())
    assert "sim_time" in params, f"Expected 'sim_time' param, got {params}"
    # Must NOT have the old positional params
    assert "state" not in params, "Old 'state' param must be removed"
    assert "min_cpa_nm" not in params, "Old 'min_cpa_nm' param must be removed"


def test_generate_asdr_events_from_synthetic_cache_basic():
    """With sim_time=600 and a populated cache, returns expected event types."""
    from sil_orchestrator.asdr_routes import _generate_asdr_events, _msg_cache

    # Populate the module-level cache with synthetic data
    snap = _make_cache_snapshot()
    for msg in snap["threat_state"]:
        _msg_cache.append("threat_state", msg)
    for msg in snap["rule_assessment"]:
        _msg_cache.append("rule_assessment", msg)
    for msg in snap["behavior_plan"]:
        _msg_cache.append("behavior_plan", msg)

    try:
        events = _generate_asdr_events(sim_time=600.0)
    finally:
        # Reset cache between tests
        from sil_orchestrator.asdr_routes import MessageCache
        import sil_orchestrator.asdr_routes as mod
        mod._msg_cache = MessageCache()

    event_types = {e["type"] for e in events}
    assert "T01_DET" in event_types, f"Expected T01_DET, got {event_types}"
    assert "CPA_PROJ" in event_types, f"Expected CPA_PROJ, got {event_types}"
    assert "COLREG_R14" in event_types, f"Expected COLREG_R14, got {event_types}"
    assert "MPC_BRANCH" in event_types, f"Expected MPC_BRANCH, got {event_types}"
    assert "CPA_MIN" in event_types, f"Expected CPA_MIN, got {event_types}"


def test_generate_asdr_events_empty_cache():
    """With empty cache and sim_time=600, INIT/SCENE_CHG/END still present; no T01_DET."""
    from sil_orchestrator.asdr_routes import _generate_asdr_events, MessageCache
    import sil_orchestrator.asdr_routes as mod

    mod._msg_cache = MessageCache()  # ensure empty

    events = _generate_asdr_events(sim_time=600.0)
    event_types = {e["type"] for e in events}

    # With empty cache, T01_DET requires a threat_state entry → absent
    assert "T01_DET" not in event_types, (
        "T01_DET must not appear when cache has no threat_state"
    )
    # INIT should always appear at sim_time >= 0
    assert "INIT" in event_types, f"INIT missing from {event_types}"


def test_generate_asdr_events_t01_uses_mmsi_from_cache():
    """T01_DET payload must carry the MMSI from cache, not a hardcoded 0."""
    from sil_orchestrator.asdr_routes import _generate_asdr_events, MessageCache
    import sil_orchestrator.asdr_routes as mod

    mod._msg_cache = MessageCache()
    mod._msg_cache.append("threat_state", {"stamp": 25.0, "target_mmsi": 987654321, "cpa_nm": 0.3, "tcpa_s": 50.0})

    try:
        events = _generate_asdr_events(sim_time=600.0)
    finally:
        mod._msg_cache = MessageCache()

    t01 = next((e for e in events if e["type"] == "T01_DET"), None)
    assert t01 is not None, "T01_DET not found"
    assert t01["payload"]["target_mmsi"] == 987654321, (
        f"MMSI should be 987654321 from cache, got {t01['payload'].get('target_mmsi')}"
    )


# ---------------------------------------------------------------------------
# 10A-4: MessageCache API
# ---------------------------------------------------------------------------

def test_message_cache_append_and_snapshot():
    """MessageCache.append + get_snapshot round-trip."""
    from sil_orchestrator.asdr_routes import MessageCache

    cache = MessageCache()
    cache.append("threat_state", {"stamp": 1.0, "cpa_nm": 0.2})
    cache.append("behavior_plan", {"stamp": 2.0, "rationale": "mpc"})

    snap = cache.get_snapshot()
    assert len(snap["threat_state"]) == 1
    assert snap["threat_state"][0]["cpa_nm"] == 0.2
    assert len(snap["behavior_plan"]) == 1
    assert snap["behavior_plan"][0]["rationale"] == "mpc"
    assert len(snap["rule_assessment"]) == 0
    assert len(snap["actuator_cmd"]) == 0


def test_message_cache_unknown_topic_ignored():
    """append() with unknown topic must not raise."""
    from sil_orchestrator.asdr_routes import MessageCache

    cache = MessageCache()
    # Should not raise
    cache.append("unknown_topic", {"stamp": 1.0})


# ---------------------------------------------------------------------------
# 10A-4: /api/v1/asdr/events endpoint — no _avoidance_state dependency
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_asdr_events_endpoint_empty_cache():
    """GET /api/v1/asdr/events with empty cache returns valid JSON (no 500)."""
    from httpx import AsyncClient, ASGITransport
    from sil_orchestrator.asdr_routes import MessageCache
    import sil_orchestrator.asdr_routes as mod

    mod._msg_cache = MessageCache()  # ensure empty

    # Import app WITHOUT triggering _avoidance_state
    from sil_orchestrator.main import app

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.get("/api/v1/asdr/events")

    assert resp.status_code == 200
    body = resp.json()
    assert "events" in body
    assert "ledger" in body
    # Empty cache → sim_time falls back to 0.0 → only INIT event at t=0
    assert isinstance(body["events"], list)
    assert isinstance(body["ledger"], list)


# ---------------------------------------------------------------------------
# Issue #3 fix: sim_time derivation from ROS2 dict-typed stamps
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_sim_time_from_ros2_dict_stamp():
    """Cache entry with stamp={"sec":42,"nanosec":500_000_000} → sim_time=42.5.

    Verifies that _extract_sec correctly converts the ROS2 dict stamp so
    sim_time >= 25 and the T01_DET event is present in the response.
    """
    from httpx import AsyncClient, ASGITransport
    from sil_orchestrator.asdr_routes import MessageCache
    import sil_orchestrator.asdr_routes as mod

    mod._msg_cache = MessageCache()
    mod._msg_cache.append(
        "threat_state",
        {
            "stamp": {"sec": 42, "nanosec": 500_000_000},
            "target_mmsi": 111222333,
            "cpa_nm": 0.30,
            "tcpa_s": 60.0,
        },
    )

    try:
        from sil_orchestrator.main import app
        transport = ASGITransport(app=app)
        async with AsyncClient(transport=transport, base_url="http://test") as client:
            resp = await client.get("/api/v1/asdr/events")
    finally:
        mod._msg_cache = MessageCache()

    assert resp.status_code == 200
    body = resp.json()
    event_types = {e["type"] for e in body["events"]}

    # sim_time = 42.5 → >= 25 → T01_DET must be present
    assert "T01_DET" in event_types, (
        f"Expected T01_DET (sim_time=42.5 >= 25), got event types: {event_types}"
    )
    # sim_time = 42.5 < 47 → SCENE_CHG must NOT be present
    assert "SCENE_CHG" not in event_types, (
        f"SCENE_CHG should not appear at sim_time=42.5, got: {event_types}"
    )
