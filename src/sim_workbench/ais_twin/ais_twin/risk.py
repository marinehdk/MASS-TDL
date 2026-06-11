from __future__ import annotations

from dataclasses import dataclass
import math

from ais_twin.geometry import (
    bearing_deg,
    cpa_tcpa_m,
    destination_along_segment,
    distance_m,
    heading_delta_deg,
    latlon_to_local_m,
    velocity_components_mps,
)
from ais_twin.model import RankedTarget, RoutePoint, TrackSegment


@dataclass(frozen=True)
class _OwnshipState:
    lat: float
    lon: float
    sog_kn: float
    cog_deg: float


def _finite(*values: float | None) -> bool:
    return all(value is not None and math.isfinite(value) for value in values)


def _sample_point(segment: TrackSegment, sim_elapsed_s: float):
    return min(segment.points, key=lambda p: (abs(p.t_s - sim_elapsed_s), p.t_s))


def _segment_cog_deg(own_route: list[RoutePoint], index: int) -> float:
    if len(own_route) < 2:
        return 90.0
    a = own_route[index]
    b = own_route[min(index + 1, len(own_route) - 1)]
    if not _finite(a.lat, a.lon, b.lat, b.lon):
        return float("nan")
    return bearing_deg(a.lat, a.lon, b.lat, b.lon)


def _ownship_at(own_route: list[RoutePoint], sim_elapsed_s: float) -> _OwnshipState:
    if len(own_route) == 1:
        only = own_route[0]
        if not _finite(only.lat, only.lon, only.target_sog_kn):
            return _OwnshipState(float("nan"), float("nan"), float("nan"), float("nan"))
        return _OwnshipState(only.lat, only.lon, only.target_sog_kn, 90.0)

    remaining_s = max(0.0, sim_elapsed_s)
    for idx, start in enumerate(own_route[:-1]):
        end = own_route[idx + 1]
        if not _finite(start.lat, start.lon, start.target_sog_kn, end.lat, end.lon):
            return _OwnshipState(float("nan"), float("nan"), float("nan"), float("nan"))
        leg_m = distance_m(start.lat, start.lon, end.lat, end.lon)
        speed_mps = max(start.target_sog_kn * 0.514444, 0.1)
        leg_s = leg_m / speed_mps
        cog_deg = _segment_cog_deg(own_route, idx)
        if not _finite(leg_m, leg_s, cog_deg):
            return _OwnshipState(float("nan"), float("nan"), float("nan"), float("nan"))
        if remaining_s <= leg_s:
            lat, lon = destination_along_segment(
                start.lat,
                start.lon,
                end.lat,
                end.lon,
                remaining_s / leg_s if leg_s > 0.0 else 1.0,
            )
            return _OwnshipState(lat, lon, start.target_sog_kn, cog_deg)
        remaining_s -= leg_s

    final = own_route[-1]
    if not _finite(final.lat, final.lon, final.target_sog_kn):
        return _OwnshipState(float("nan"), float("nan"), float("nan"), float("nan"))
    return _OwnshipState(
        final.lat,
        final.lon,
        final.target_sog_kn,
        _segment_cog_deg(own_route, len(own_route) - 2),
    )


def rank_targets(
    own_route: list[RoutePoint],
    segments: list[TrackSegment],
    sim_elapsed_s: float,
    top_n: int,
) -> list[RankedTarget]:
    if not own_route or top_n <= 0:
        return []
    own = _ownship_at(own_route, sim_elapsed_s)
    if not _finite(own.lat, own.lon, own.sog_kn, own.cog_deg):
        return []
    own_vx, own_vy = velocity_components_mps(own.sog_kn, own.cog_deg)
    ranked: list[RankedTarget] = []
    for segment in segments:
        if not segment.points:
            continue
        point = _sample_point(segment, sim_elapsed_s)
        if not _finite(point.lat, point.lon, point.sog_kn, point.cog_deg):
            continue
        dist = distance_m(own.lat, own.lon, point.lat, point.lon)
        crossing = heading_delta_deg(own.cog_deg, point.cog_deg)
        px, py = latlon_to_local_m(point.lat, point.lon, own.lat, own.lon)
        target_vx, target_vy = velocity_components_mps(point.sog_kn, point.cog_deg)
        cpa_m, tcpa_s = cpa_tcpa_m(px, py, target_vx - own_vx, target_vy - own_vy)
        tcpa_weight = max(0.0, 1.0 - (cpa_m / 20000.0))
        tcpa_urgency = 0.0 if math.isinf(tcpa_s) else 600.0 / max(tcpa_s + 1.0, 1.0)
        score = (
            1000.0 / max(cpa_m, 1.0)
            + tcpa_urgency * tcpa_weight
            + 200.0 / max(dist, 1.0)
            + min(point.sog_kn, 30.0) / 30.0
            + crossing / 180.0
        )
        if not _finite(score, cpa_m, dist, crossing):
            continue
        ranked.append(
            RankedTarget(
                mmsi=segment.mmsi,
                point=point,
                score=score,
                cpa_m=cpa_m,
                tcpa_s=tcpa_s,
                distance_m=dist,
                rationale=(
                    f"risk_score={score:.3f} cpa_m={cpa_m:.1f} tcpa_s={tcpa_s:.1f} "
                    f"distance_m={dist:.1f} crossing_deg={crossing:.1f}"
                ),
            )
        )
    ranked.sort(key=lambda item: (-item.score, item.mmsi))
    return ranked[:top_n]
