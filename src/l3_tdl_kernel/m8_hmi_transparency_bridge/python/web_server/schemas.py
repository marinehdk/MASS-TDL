"""pydantic models — REST/WebSocket boundary schema."""
from __future__ import annotations

from datetime import datetime
from typing import Literal

from pydantic import BaseModel, ConfigDict, Field


class TorAcknowledgeRequest(BaseModel):
    model_config = ConfigDict(extra="forbid")

    operator_id: str = Field(..., min_length=1, max_length=64)


class TorAcknowledgeResponse(BaseModel):
    accepted: bool
    click_time: datetime
    operator_id: str


class Sat1ThreatSchema(BaseModel):
    target_id: int
    cpa_m: float
    tcpa_s: float
    aspect: Literal["head_on", "crossing", "overtaking", "overtaken", "ambiguous"]
    confidence: float = Field(..., ge=0.0, le=1.0)


class UiStateSchema(BaseModel):
    """JSON projection of UIState for FastAPI ↔ browser boundary."""

    model_config = ConfigDict(extra="forbid")

    timestamp: datetime
    role: Literal["roc_operator", "ship_captain"]
    scenario: Literal[
        "transit",
        "colreg_avoidance",
        "mrc_preparation",
        "mrc_active",
        "override_active",
        "handback_preparation",
    ]
    sat1_visible: bool
    sat2_visible: bool
    sat3_visible: bool
    sat3_priority_high: bool
    sat3_alert_color: Literal["normal", "bold_red"]
    sat1_state_summary: str
    sat1_threats: list[Sat1ThreatSchema]
    sat2_reasoning_chain: str | None = None
    sat2_trigger_reasons: list[str] = []
    sat3_predicted_state: str | None = None
    sat3_tdl_s: float
    sat3_tmr_s: float
    tor_active: bool
    tor_remaining_s: float
    tor_button_enabled: bool
    override_active: bool
    m7_degradation_alert: str | None = None
