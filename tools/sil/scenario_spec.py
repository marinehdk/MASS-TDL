"""Pydantic model for YAML scenario spec files (D1.3b schema_version 1.0)."""
from __future__ import annotations

import math
import re
from pathlib import Path
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
    bearing_sector_deg: list[float] | None = Field(default=None, min_length=2, max_length=2)


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

    @classmethod
    def from_file(cls, yaml_path: str | Path) -> "ScenarioSpec":
        """Load a scenario YAML (v1.0 or v2.0 maritime-schema).

        v2.0 files use geo_origin + lat/lon and are converted to ENU (m) relative
        to the declared geo_origin. v1.0 files use x_m/y_m directly.
        """
        import yaml

        raw = yaml.safe_load(Path(yaml_path).read_text())
        meta = raw.get("metadata", raw)

        schema_version = meta.get("schema_version", "1.0")

        # v2.0: convert lat/lon → ENU using geo_origin
        if schema_version == "2.0":
            geo = meta.get("geo_origin", {})
            lat_origin = float(geo.get("latitude", 5.0))
            lon_origin = float(geo.get("longitude", 63.0))

            from .geo_utils import latlon_to_enu

            # Own ship
            own_init = raw["own_ship"]["initial"]
            lat_own = float(own_init["position"]["latitude"])
            lon_own = float(own_init["position"]["longitude"])
            north_own, east_own = latlon_to_enu(lat_origin, lon_origin, lat_own, lon_own)

            # heading: v2.0 "heading" is COG/nautical convention, convert to psi_math_rad
            heading_nav = float(own_init.get("heading", 0.0))
            # psi_math_rad: nautical heading CW from North → math CCW from East
            psi_math = math.pi / 2.0 - math.radians(heading_nav)

            # Cog to ENU velocity: vx = SOG*sin(COG), vy = SOG*cos(COG) in ENU
            sog_kn = float(own_init["sog"])
            cog_nav = float(own_init.get("cog", heading_nav))
            sog_mps = sog_kn * 0.5144
            vx_own = sog_mps * math.sin(math.radians(cog_nav))
            vy_own = sog_mps * math.cos(math.radians(cog_nav))

            n_rps_initial = float(meta.get("simulation", {}).get("n_rps_initial", 3.0))

            own_ship = OwnShip(
                x_m=east_own,
                y_m=north_own,
                heading_nav_deg=heading_nav,
                speed_kn=sog_kn,
                n_rps=n_rps_initial,
            )

            # Targets
            targets_list: list[Target] = []
            for i, ts in enumerate(raw.get("target_ships", [])):
                ts_init = ts["initial"]
                lat_t = float(ts_init["position"]["latitude"])
                lon_t = float(ts_init["position"]["longitude"])
                north_t, east_t = latlon_to_enu(lat_origin, lon_origin, lat_t, lon_t)
                cog_t = float(ts_init.get("cog", ts_init.get("heading", 0.0)))
                sog_t_kn = float(ts_init["sog"])
                sog_t_mps = sog_t_kn * 0.5144
                targets_list.append(
                    Target(
                        target_id=i + 1,
                        x_m=east_t,
                        y_m=north_t,
                        cog_nav_deg=cog_t,
                        sog_mps=sog_t_mps,
                    )
                )

            # Encounter
            enc = meta["encounter"]
            encounter = Encounter(
                rule=enc["rule"],
                give_way_vessel=enc["give_way_vessel"],
                expected_own_action=enc["expected_own_action"],
                avoidance_time_s=float(enc["avoidance_time_s"]),
                avoidance_delta_rad=float(enc["avoidance_delta_rad"]),
                avoidance_duration_s=float(enc["avoidance_duration_s"]),
            )

            # Disturbance
            dm = meta["disturbance_model"]
            disturbance_model = DisturbanceModel(
                wind_kn=float(dm["wind_kn"]),
                wind_dir_nav_deg=float(dm.get("wind_dir_nav_deg", 0.0)),
                current_kn=float(dm["current_kn"]),
                current_dir_nav_deg=float(dm.get("current_dir_nav_deg", 0.0)),
                vis_m=float(dm["vis_m"]),
                wave_height_m=float(dm.get("wave_height_m", 0.0)),
            )

            # Pass criteria (v2.0 may omit bearing_sector_deg)
            pc = meta["pass_criteria"]
            pass_criteria = PassCriteria(
                max_dcpa_no_action_m=float(pc["max_dcpa_no_action_m"]),
                min_dcpa_with_action_m=float(pc["min_dcpa_with_action_m"]),
                bearing_sector_deg=pc.get("bearing_sector_deg"),
            )

            # Simulation
            sim = meta["simulation"]
            simulation = SimulationConfig(
                duration_s=float(sim["duration_s"]),
                dt_s=float(sim["dt_s"]),
            )

            return cls(
                schema_version="2.0",
                scenario_id=meta["scenario_id"],
                description=raw.get("description", ""),
                rule_branch_covered=[enc["rule"]],
                vessel_class=meta.get("vessel_class", "FCB"),
                odd_zone=meta.get("odd_zone", "A"),
                initial_conditions=InitialConditions(own_ship=own_ship, targets=targets_list),
                encounter=encounter,
                disturbance_model=disturbance_model,
                prng_seed=0,
                pass_criteria=pass_criteria,
                simulation=simulation,
            )

        # v1.0: direct own_ship / target_ships structure (already ENU m)
        return cls.model_validate(raw)
