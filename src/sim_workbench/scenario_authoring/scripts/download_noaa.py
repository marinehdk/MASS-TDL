#!/usr/bin/env python3
"""Download NOAA MarineCadastre AIS data (CC0 1.0 Public Domain).

URL: https://marinecadastre.gov/accessais/
Format: ZIP containing CSV files.
"""
from __future__ import annotations

import argparse
import sys
import zipfile
from pathlib import Path

import requests

NOAA_BASE = "https://coast.noaa.gov/htdata/CMSP/AISDataHandler/"
OUTPUT_DIR = Path("data/ais_datasets/noaa")


def download_year_month(year: int, month: int, zone: int = 11) -> Path:
    """Download one month of AIS data for a UTM zone (zone 10 = SF Bay)."""
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    filename = f"AIS_{year}_{month:02d}_Zone{zone}.zip"
    url = f"{NOAA_BASE}{year}/{filename}"

    print(f"Downloading {url} ...")
    resp = requests.get(url, stream=True, timeout=600)
    resp.raise_for_status()

    zip_path = OUTPUT_DIR / filename
    with open(zip_path, "wb") as f:
        for chunk in resp.iter_content(chunk_size=8192):
            f.write(chunk)

    # Extract CSV files
    with zipfile.ZipFile(zip_path) as zf:
        zf.extractall(OUTPUT_DIR)

    print(f"Extracted to {OUTPUT_DIR}")
    return OUTPUT_DIR


def main() -> None:
    parser = argparse.ArgumentParser(description="Download NOAA AIS data")
    parser.add_argument("--year", type=int, default=2024)
    parser.add_argument("--month", type=int, default=1)
    parser.add_argument("--zone", type=int, default=10, help="UTM zone (10=SF Bay)")
    args = parser.parse_args()

    try:
        download_year_month(args.year, args.month, args.zone)
        print("Done. NOAA AIS data available for pipeline ingestion.")
    except requests.HTTPError as e:
        print(f"Download failed: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
