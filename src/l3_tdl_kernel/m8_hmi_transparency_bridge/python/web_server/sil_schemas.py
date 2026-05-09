"""Pydantic models for SIL debug HMI endpoints (D1.3b)."""
from __future__ import annotations

from datetime import datetime
from typing import Literal

from pydantic import BaseModel

from web_server.schemas import Sat1ThreatSchema


class SilSAT1Panel(BaseModel):
    threat_count: int
    threats: list[Sat1ThreatSchema]


class SilODDPanel(BaseModel):
    zone: str
    envelope_state: str
    conformance_score: float
    confidence: float


class SilDebugSchema(BaseModel):
    timestamp: datetime
    scenario_id: str
    sat1: SilSAT1Panel | None = None
    sat2_reasoning: str | None = None
    sat3_tdl_s: float = 0.0
    sat3_tmr_s: float = 0.0
    odd: SilODDPanel | None = None
    job_status: Literal["idle", "running", "done"] = "idle"
