"""Unit tests for sil_schemas pydantic models."""
from datetime import datetime, timezone

from web_server.sil_schemas import SilDebugSchema, SilODDPanel, SilSAT1Panel
from web_server.schemas import Sat1ThreatSchema


def test_sil_debug_schema_defaults():
    s = SilDebugSchema(timestamp=datetime.now(tz=timezone.utc), scenario_id="idle")
    assert s.job_status == "idle"
    assert s.sat1 is None
    assert s.odd is None
    assert s.sat3_tdl_s == 0.0


def test_sil_debug_schema_with_sat1():
    threat = Sat1ThreatSchema(
        target_id=1, cpa_m=400.0, tcpa_s=120.0, aspect="head_on", confidence=0.9
    )
    panel = SilSAT1Panel(threat_count=1, threats=[threat])
    s = SilDebugSchema(
        timestamp=datetime.now(tz=timezone.utc),
        scenario_id="colreg-rule14-ho-001-v1.0",
        sat1=panel,
        job_status="running",
    )
    assert s.sat1.threat_count == 1
    assert s.job_status == "running"


def test_sil_odd_panel():
    panel = SilODDPanel(
        zone="A",
        envelope_state="IN",
        conformance_score=0.95,
        confidence=0.9,
    )
    assert panel.zone == "A"
    assert panel.conformance_score == 0.95


def test_sil_debug_schema_full_roundtrip():
    threat = Sat1ThreatSchema(
        target_id=2, cpa_m=200.0, tcpa_s=80.0, aspect="crossing", confidence=0.85
    )
    schema = SilDebugSchema(
        timestamp=datetime.now(tz=timezone.utc),
        scenario_id="cs-001",
        sat1=SilSAT1Panel(threat_count=1, threats=[threat]),
        sat2_reasoning="Rule 15: own ship is give-way",
        sat3_tdl_s=45.0,
        sat3_tmr_s=60.0,
        odd=SilODDPanel(zone="A", envelope_state="IN", conformance_score=0.9, confidence=0.85),
        job_status="done",
    )
    d = schema.model_dump()
    assert d["job_status"] == "done"
    assert d["sat3_tdl_s"] == 45.0
