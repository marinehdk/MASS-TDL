#!/usr/bin/env python3
"""Validate downloaded AIS dataset: statistics and completeness check."""
from __future__ import annotations

import argparse
import sys
from collections import Counter
from pathlib import Path

# Add parent to path for scenario_authoring import
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from scenario_authoring.authoring.ais_source import load_ais_dataset


def validate(data_dir: Path) -> dict:
    """Return dataset statistics dict."""
    records = load_ais_dataset(data_dir)
    if not records:
        return {"error": "No valid records found"}

    mmsi_counts = Counter(r.mmsi for r in records)
    timestamps = [r.timestamp for r in records]
    t_min, t_max = min(timestamps), max(timestamps)

    return {
        "total_records": len(records),
        "unique_mmsi": len(mmsi_counts),
        "time_span_hours": (t_max - t_min) / 3600.0,
        "time_start_iso": t_min,
        "time_end_iso": t_max,
        "mmsi_top10": mmsi_counts.most_common(10),
        "missing_sog_pct": round(100 * sum(1 for r in records if r.sog_kn == 0) / len(records), 1),
        "missing_heading_pct": round(100 * sum(1 for r in records if r.heading_deg == r.cog_deg) / len(records), 1),
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="Validate AIS dataset")
    parser.add_argument("data_dir", type=Path)
    args = parser.parse_args()

    stats = validate(args.data_dir)
    for key, value in stats.items():
        print(f"  {key}: {value}")


if __name__ == "__main__":
    main()
