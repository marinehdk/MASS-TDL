#!/usr/bin/env python3
"""Download Kystverket AIS data (NLOD license, equivalent CC BY 4.0).

URL: https://kystverket.no/en/navigation-and-monitoring/ais/ais-data-for-research/
Format: CSV files.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

import requests

KYSTVERKET_BASE = "https://ais.kystverket.no/ais-data/"
OUTPUT_DIR = Path("data/ais_datasets/kystverket")


def download_day(year: int, month: int, day: int) -> Path:
    """Download a single day's AIS data CSV."""
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    filename = f"ais_{year}{month:02d}{day:02d}.csv"
    url = f"{KYSTVERKET_BASE}{year}/{filename}"

    print(f"Downloading {url} ...")
    resp = requests.get(url, stream=True, timeout=300)
    resp.raise_for_status()

    output_path = OUTPUT_DIR / filename
    with open(output_path, "wb") as f:
        for chunk in resp.iter_content(chunk_size=8192):
            f.write(chunk)

    mb_size = output_path.stat().st_size / 1024 / 1024
    print(f"Downloaded: {output_path} ({mb_size:.1f} MB)")
    return output_path


def main() -> None:
    parser = argparse.ArgumentParser(description="Download Kystverket AIS data")
    parser.add_argument("--year", type=int, default=2024)
    parser.add_argument("--month", type=int, default=6)
    parser.add_argument("--day", type=int, default=15)
    args = parser.parse_args()

    try:
        download_day(args.year, args.month, args.day)
        print("Done. Kystverket AIS data available for pipeline ingestion.")
    except requests.HTTPError as e:
        print(f"Download failed: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
