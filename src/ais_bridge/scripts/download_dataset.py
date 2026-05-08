#!/usr/bin/env python3
# SPDX-License-Identifier: Proprietary
"""Download NOAA AccessAIS or generate synthetic AIS dataset for D1.3a SIL testing."""
import argparse
import os
import sys
import urllib.request
import zipfile
from pathlib import Path

# NOAA AccessAIS Zone 19 (US East Coast, NY area) — CC0 1.0 public domain
NOAA_URL_2023_ZONE19 = (
    "https://coast.noaa.gov/htdata/CMSP/AISDataHandler/2023/"
    "AIS_2023_01_Zone19.zip"
)

def _progress_hook(count, block_size, total_size):
    if total_size > 0:
        pct = min(100, int(count * block_size * 100 / total_size))
        sys.stdout.write(f"\r  {pct}% ({count * block_size // 1024 // 1024}MB)")
        sys.stdout.flush()


def download_noaa(output_dir: Path) -> Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    zip_path = output_dir / "AIS_2023_01_Zone19.zip"

    if not zip_path.exists():
        print(f"Downloading NOAA AIS Zone 19 (CC0) from:\n  {NOAA_URL_2023_ZONE19}")
        urllib.request.urlretrieve(NOAA_URL_2023_ZONE19, zip_path, _progress_hook)
        print()

    print(f"Extracting {zip_path} ...")
    with zipfile.ZipFile(zip_path) as zf:
        csv_names = sorted(n for n in zf.namelist() if n.lower().endswith('.csv'))
        if not csv_names:
            raise RuntimeError("No CSV found inside NOAA ZIP")
        target = csv_names[0]
        zf.extract(target, output_dir)
        csv_path = output_dir / target
        print(f"Extracted: {csv_path} ({csv_path.stat().st_size // 1024 // 1024} MB)")
        return csv_path


def generate_synthetic_fallback(output_dir: Path, n_records: int = 72000) -> Path:
    """Generate synthetic NOAA-format CSV as R3.2 fallback. 72000 records = 1h at 2Hz for 10 vessels."""
    import csv, math, random
    output_dir.mkdir(parents=True, exist_ok=True)
    out_path = output_dir / "AIS_synthetic_1h.csv"

    random.seed(42)
    # 10 vessels covering COLREGs Rule 13-17 geometries relative to 30.5N, 122.0E
    vessels = [
        (123456001, 30.52, 121.98, 12.0, 180.0, 70),
        (123456002, 30.54, 121.97, 10.0, 200.0, 70),
        (123456003, 30.48, 121.96,  8.0,   0.0, 70),
        (123456004, 30.51, 121.95, 14.0,  90.0, 70),
        (123456005, 30.49, 122.05, 11.0, 270.0, 70),
        (123456006, 30.53, 122.02,  9.0, 130.0, 70),
        (123456007, 30.50, 121.99, 15.0,  45.0, 70),
        (123456008, 30.48, 121.99,  6.0,  45.0, 70),
        (123456009, 30.49, 122.00,  7.0,  50.0, 70),
        (123456010, 30.51, 122.01,  5.0,  40.0, 70),
    ]

    records_per_vessel = n_records // len(vessels)
    dt_s = 0.5

    with open(out_path, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['MMSI', 'BaseDateTime', 'LAT', 'LON', 'SOG', 'COG',
                         'Heading', 'VesselName', 'IMO', 'CallSign',
                         'VesselType', 'Status', 'Length', 'Width', 'Draft',
                         'Cargo', 'TranscieverClass'])

        for mmsi, lat0, lon0, sog, cog, vtype in vessels:
            lat, lon = lat0, lon0
            cog_rad = math.radians(cog)
            speed_ms = sog * 0.514444
            for step in range(records_per_vessel):
                t_s = step * dt_s
                lat += speed_ms * dt_s * math.cos(cog_rad) / 111320.0
                lon += speed_ms * dt_s * math.sin(cog_rad) / (
                    111320.0 * math.cos(math.radians(lat)))
                ts = f"2023-01-15 08:{int(t_s // 60):02d}:{int(t_s % 60):02d}"
                writer.writerow([
                    mmsi, ts,
                    f"{lat:.6f}", f"{lon:.6f}",
                    f"{sog:.1f}", f"{cog:.1f}", f"{cog:.1f}",
                    f"vessel_{mmsi % 10}", f"IMO{mmsi}", f"CALL{mmsi % 1000:03d}",
                    vtype, 0, 50, 10, 5, 70, 'A',
                ])

    print(f"Synthetic fallback created: {out_path} ({records_per_vessel * len(vessels)} records)")
    return out_path


def main():
    parser = argparse.ArgumentParser(description='Download AIS dataset for D1.3a SIL testing')
    parser.add_argument('--source', choices=['noaa', 'synthetic'], default='noaa')
    parser.add_argument('--output-dir', default='data/ais_datasets')
    args = parser.parse_args()

    out = Path(args.output_dir)

    if args.source == 'noaa':
        try:
            path = download_noaa(out)
        except Exception as e:
            print(f"NOAA download failed: {e}")
            print("Falling back to synthetic dataset (R3.2 contingency)")
            path = generate_synthetic_fallback(out)
    else:
        path = generate_synthetic_fallback(out)

    print(f"\nDataset ready: {path}")
    print("Next step: update config/ais_replay.yaml → dataset_path")


if __name__ == '__main__':
    main()
