"""Pydantic models for SIL debug HMI endpoints (D1.3b)."""
from __future__ import annotations

from datetime import datetime
from typing import Literal

from pydantic import BaseModel


class SilSAT1Panel(BaseModel):
    threat_count: int
    threats: list[dict] = []


class SilODDPanel(BaseModel):
    zone: str
    envelope_state: str
    conformance_score: float
    confidence: float


class SilNavState(BaseModel):
    lat: float
    lon: float
    sog_kn: float
    cog_deg: float
    heading_deg: float


class SilTargetState(BaseModel):
    mmsi: str
    lat: float
    lon: float
    sog_kn: float
    cog_deg: float
    heading_deg: float
    cpa_nm: float
    tcpa_s: float
    colreg_role: str
    confidence: float


class SilAsdrEvent(BaseModel):
    timestamp: datetime
    event_type: str
    step: int
    rule: str
    cpa_nm: float
    tcpa_s: float
    action: str
    verdict: Literal["PASS", "RISK"]


class SilDebugSchema(BaseModel):
    timestamp: datetime
    scenario_id: str
    sat1: SilSAT1Panel | None = None
    sat2_reasoning: str | None = None
    sat3_tdl_s: float = 0.0
    sat3_tmr_s: float = 0.0
    odd: SilODDPanel | None = None
    job_status: Literal["idle", "running", "done"] = "idle"

    # D1.3b.3 Bridge-less live data
    own_ship: SilNavState | None = None
    targets: list[SilTargetState] = []
    cpa_nm: float = 0.0
    tcpa_s: float = 0.0
    rule_text: str = ""
    decision_text: str = ""
    module_status: list[bool] = [True] * 8
    asdr_events: list[SilAsdrEvent] = []
