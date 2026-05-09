"""Pydantic model for YAML scenario spec files (D1.3b schema_version 1.0)."""
from __future__ import annotations

import math
from typing import Literal

from pydantic import BaseModel, Field, field_validator


class OwnShip(BaseModel):
    x_m: float
    y_m: float
    heading_nav_deg: float
    speed_kn: float
    n_rps: float

    @property
    def speed_mps(self) -> float:
        return self.speed_kn * 0.5144

    @property
    def psi_math_rad(self) -> float:
        """Convert nautical heading (CW from North, deg) → math convention (CCW from East, rad)."""
        return math.pi / 2.0 - math.radians(self.heading_nav_deg)


class Target(BaseModel):
    target_id: int
    x_m: float
    y_m: float
    cog_nav_deg: float
    sog_mps: float


class InitialConditions(BaseModel):
    own_ship: OwnShip
    targets: list[Target]


class Encounter(BaseModel):
    rule: str
    give_way_vessel: Literal["own", "target", "none"]
    expected_own_action: Literal["turn_starboard", "turn_port", "maintain", "slow_down"]
    avoidance_time_s: float
    avoidance_delta_rad: float
    avoidance_duration_s: float


class DisturbanceModel(BaseModel):
    wind_kn: float
    wind_dir_nav_deg: float
    current_kn: float
    current_dir_nav_deg: float
    vis_m: float
    wave_height_m: float


class PassCriteria(BaseModel):
    max_dcpa_no_action_m: float
    min_dcpa_with_action_m: float
    bearing_sector_deg: list[float] = Field(..., min_length=2, max_length=2)


class SimulationConfig(BaseModel):
    duration_s: float
    dt_s: float

    @field_validator("dt_s")
    @classmethod
    def dt_must_be_0_02(cls, v: float) -> float:
        if abs(v - 0.02) > 1e-9:
            raise ValueError(f"dt_s must be 0.02 (D1.3.1 baseline), got {v}")
        return v

    @field_validator("duration_s")
    @classmethod
    def duration_must_be_gte_600(cls, v: float) -> float:
        if v < 600.0:
            raise ValueError(f"duration_s must be >= 600.0, got {v}")
        return v


class ScenarioSpec(BaseModel):
    schema_version: str
    scenario_id: str
    description: str
    rule_branch_covered: list[str]
    vessel_class: str
    odd_zone: Literal["A", "B", "C", "D"]
    initial_conditions: InitialConditions
    encounter: Encounter
    disturbance_model: DisturbanceModel
    prng_seed: int
    pass_criteria: PassCriteria
    simulation: SimulationConfig
