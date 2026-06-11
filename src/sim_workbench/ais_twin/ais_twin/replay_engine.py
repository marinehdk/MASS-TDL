from __future__ import annotations

import csv
from collections import defaultdict
from pathlib import Path

from ais_twin.model import RankedTarget, RoutePoint, TrackPoint, TrackSegment
from ais_twin.risk import rank_targets


def load_track_segments_csv(path: Path) -> list[TrackSegment]:
    points_by_mmsi: dict[int, list[TrackPoint]] = defaultdict(list)
    with path.open(encoding="utf-8", newline="") as fp:
        for row in csv.DictReader(fp):
            mmsi = int(row["mmsi"])
            heading_raw = row["heading_deg"]
            points_by_mmsi[mmsi].append(
                TrackPoint(
                    t_s=float(row["t_s"]),
                    mmsi=mmsi,
                    lat=float(row["lat"]),
                    lon=float(row["lon"]),
                    sog_kn=float(row["sog_kn"]),
                    cog_deg=float(row["cog_deg"]),
                    heading_deg=float(heading_raw) if heading_raw else None,
                )
            )
    return [
        TrackSegment(mmsi=mmsi, points=tuple(sorted(points, key=lambda point: point.t_s)))
        for mmsi, points in sorted(points_by_mmsi.items())
    ]


def target_payloads(ranked_targets: list[RankedTarget]) -> list[dict[str, float | int | str]]:
    payloads: list[dict[str, float | int | str]] = []
    for ranked in ranked_targets:
        heading = ranked.point.heading_deg if ranked.point.heading_deg is not None else ranked.point.cog_deg
        payloads.append(
            {
                "target_id": ranked.mmsi,
                "lat": ranked.point.lat,
                "lon": ranked.point.lon,
                "sog_kn": ranked.point.sog_kn,
                "cog_deg": ranked.point.cog_deg,
                "heading_deg": heading,
                "source_sensor": "ais",
                "cpa_m": ranked.cpa_m,
                "tcpa_s": ranked.tcpa_s,
                "rationale": ranked.rationale,
            }
        )
    return payloads


def replay_payloads_at(
    own_route: list[RoutePoint],
    segments: list[TrackSegment],
    sim_elapsed_s: float,
    top_n: int,
) -> list[dict[str, float | int | str]]:
    return target_payloads(rank_targets(own_route, segments, sim_elapsed_s, top_n))
