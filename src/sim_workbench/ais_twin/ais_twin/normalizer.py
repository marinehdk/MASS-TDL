from __future__ import annotations

from collections import defaultdict

from ais_twin.model import CanonicalAISRecord, TrackPoint, TrackSegment


def normalize_records(records: list[CanonicalAISRecord], max_gap_s: float = 300.0, min_points: int = 3) -> list[TrackSegment]:
    if not records:
        return []
    base = min(r.ais_time_utc for r in records)
    grouped: dict[int, dict[float, CanonicalAISRecord]] = defaultdict(dict)
    for record in records:
        if record.sog_kn is None or record.cog_deg is None:
            continue
        t_s = (record.ais_time_utc - base).total_seconds()
        existing = grouped[record.mmsi].get(t_s)
        if existing is None:
            grouped[record.mmsi][t_s] = record
        elif _normalized_fields(existing) != _normalized_fields(record):
            raise ValueError(f"conflicting duplicate AIS record for mmsi={record.mmsi} t_s={t_s}")

    segments: list[TrackSegment] = []
    for mmsi in sorted(grouped):
        by_time = grouped[mmsi]
        current: list[TrackPoint] = []
        last_t: float | None = None
        for t_s in sorted(by_time):
            record = by_time[t_s]
            if last_t is not None and t_s - last_t > max_gap_s:
                if len(current) >= min_points:
                    segments.append(TrackSegment(mmsi=mmsi, points=tuple(current)))
                current = []
            current.append(
                TrackPoint(
                    t_s=t_s,
                    mmsi=mmsi,
                    lat=record.lat,
                    lon=record.lon,
                    sog_kn=float(record.sog_kn),
                    cog_deg=float(record.cog_deg),
                    heading_deg=record.heading_deg,
                )
            )
            last_t = t_s
        if len(current) >= min_points:
            segments.append(TrackSegment(mmsi=mmsi, points=tuple(current)))
    return segments


def _normalized_fields(record: CanonicalAISRecord) -> tuple[float, float, float | None, float | None, float | None]:
    return (record.lat, record.lon, record.sog_kn, record.cog_deg, record.heading_deg)
