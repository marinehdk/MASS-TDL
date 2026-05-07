"""pydantic model validation tests."""
from __future__ import annotations

import pytest
from datetime import datetime, timezone
from pydantic import ValidationError

from web_server.schemas import (
    Sat1ThreatSchema,
    TorAcknowledgeRequest,
    UiStateSchema,
)


def test_sat1_threat_confidence_bounds() -> None:
    Sat1ThreatSchema(target_id=1, cpa_m=500.0, tcpa_s=120.0,
                     aspect="head_on", confidence=0.0)
    Sat1ThreatSchema(target_id=1, cpa_m=500.0, tcpa_s=120.0,
                     aspect="head_on", confidence=1.0)
    with pytest.raises(ValidationError):
        Sat1ThreatSchema(target_id=1, cpa_m=500.0, tcpa_s=120.0,
                         aspect="head_on", confidence=1.5)


def test_tor_acknowledge_rejects_empty_id() -> None:
    with pytest.raises(ValidationError):
        TorAcknowledgeRequest(operator_id="")


def test_tor_acknowledge_accepts_valid_id() -> None:
    r = TorAcknowledgeRequest(operator_id="ROC-OP-001")
    assert r.operator_id == "ROC-OP-001"


def make_valid_ui_state() -> dict:
    return dict(
        timestamp=datetime.now(tz=timezone.utc),
        role="roc_operator",
        scenario="transit",
        sat1_visible=True,
        sat2_visible=False,
        sat3_visible=True,
        sat3_priority_high=False,
        sat3_alert_color="normal",
        sat1_state_summary="TRANSIT @ D3",
        sat1_threats=[],
        sat3_tdl_s=120.0,
        sat3_tmr_s=150.0,
        tor_active=False,
        tor_remaining_s=0.0,
        tor_button_enabled=False,
        override_active=False,
    )


def test_ui_state_rejects_unknown_scenario() -> None:
    data = make_valid_ui_state()
    data["scenario"] = "unknown_scenario"
    with pytest.raises(ValidationError):
        UiStateSchema(**data)


def test_ui_state_rejects_unknown_role() -> None:
    data = make_valid_ui_state()
    data["role"] = "captain_xo"
    with pytest.raises(ValidationError):
        UiStateSchema(**data)


def test_ui_state_rejects_extra_fields() -> None:
    data = make_valid_ui_state()
    data["unknown_key"] = "value"
    with pytest.raises(ValidationError):
        UiStateSchema(**data)


def test_ui_state_valid_with_optional_fields() -> None:
    data = make_valid_ui_state()
    data["m7_degradation_alert"] = "comm_link_rtt_3s"
    data["sat2_reasoning_chain"] = "Rule 15 crossing situation detected"
    ui = UiStateSchema(**data)
    assert ui.m7_degradation_alert == "comm_link_rtt_3s"


def test_sat1_threat_rejects_unknown_aspect() -> None:
    with pytest.raises(ValidationError):
        Sat1ThreatSchema(target_id=1, cpa_m=100.0, tcpa_s=60.0,
                         aspect="unknown_aspect", confidence=0.8)
