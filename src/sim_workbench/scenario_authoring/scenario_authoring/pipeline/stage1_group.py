"""Stage 1: MMSI grouping, dedup, and time sorting."""
from __future__ import annotations

from collections import defaultdict

from scenario_authoring.authoring.ais_source import TimedAISRecord

MIN_RECORDS_PER_VESSEL = 4


def group_by_mmsi(records: list[TimedAISRecord]) -> dict[int, list[TimedAISRecord]]:
    """Group AIS records by MMSI. Discard vessels with < MIN_RECORDS_PER_VESSEL.
    Each vessel's records sorted by timestamp ascending.
    """
    groups: dict[int, list[TimedAISRecord]] = defaultdict(list)
    for rec in records:
        groups[rec.mmsi].append(rec)

    result: dict[int, list[TimedAISRecord]] = {}
    for mmsi, recs in groups.items():
        if len(recs) >= MIN_RECORDS_PER_VESSEL:
            recs.sort(key=lambda r: r.timestamp)
            result[mmsi] = recs

    return result
