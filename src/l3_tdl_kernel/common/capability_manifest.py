from __future__ import annotations

from enum import StrEnum

from pydantic import BaseModel, field_validator


class HullClass(StrEnum):
    DISPLACEMENT = "DISPLACEMENT"
    SEMI_PLANING = "SEMI_PLANING"
    PLANING = "PLANING"
    CATAMARAN = "CATAMARAN"


class CapabilityManifest(BaseModel):
    max_speed_kn: float
    rot_max_curve: list[tuple[float, float]]  # [(speed_kn, rot_deg_per_s), ...] monotone-decreasing
    length_m: float
    hull_class: HullClass
    primary_role: str = "PRIMARY_ON_BOARD"

    @field_validator("max_speed_kn")
    @classmethod
    def validate_positive_speed(cls, v: float) -> float:
        if v <= 0:
            raise ValueError(f"max_speed_kn must be > 0, got {v}")
        return v

    @field_validator("rot_max_curve")
    @classmethod
    def validate_rot_curve_monotone(
        cls, v: list[tuple[float, float]]
    ) -> list[tuple[float, float]]:
        if len(v) < 2:
            raise ValueError("rot_max_curve must have at least 2 control points")
        for i in range(1, len(v)):
            if v[i][1] >= v[i - 1][1]:
                raise ValueError(
                    f"rot_max_curve must be monotonically decreasing: "
                    f"index {i} ({v[i][1]}) >= index {i-1} ({v[i-1][1]})"
                )
        return v

    def rot_max(self, speed_kn: float) -> float:
        """Return interpolated ROT maximum for the given speed."""
        if speed_kn <= self.rot_max_curve[0][0]:
            return self.rot_max_curve[0][1]
        if speed_kn >= self.rot_max_curve[-1][0]:
            return self.rot_max_curve[-1][1]
        for i in range(1, len(self.rot_max_curve)):
            s0, r0 = self.rot_max_curve[i - 1]
            s1, r1 = self.rot_max_curve[i]
            if s0 <= speed_kn <= s1:
                t = (speed_kn - s0) / (s1 - s0)
                return r0 + t * (r1 - r0)
        return self.rot_max_curve[-1][1]
