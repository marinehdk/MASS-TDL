# SPDX-License-Identifier: Proprietary
"""Test that AIS dataset parse rate meets the >= 95% DoD gate."""
import csv
import os
import sys

_NOAA_PATH = os.environ.get(
    'AIS_DATASET_PATH',
    'data/ais_datasets/AIS_synthetic_1h.csv'
)

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))


def _count_noaa_csv(filepath: str) -> tuple:
    """Count (total_rows, successfully_parsed_rows) for a NOAA CSV file."""
    total = 0
    parsed = 0
    with open(filepath, encoding='utf-8-sig') as f:
        reader = csv.DictReader(f)
        for row in reader:
            total += 1
            try:
                float(row['LAT'])
                float(row['LON'])
                int(row['MMSI'])
                parsed += 1
            except (KeyError, ValueError):
                pass
    return total, parsed


def _count_nmea_file(filepath: str) -> tuple:
    """Count (total_aivdm_sentences, successfully_decoded) for a NMEA file."""
    from ais_bridge.nmea_decoder import decode_file
    total = 0
    parsed = 0
    with open(filepath, errors='ignore') as f:
        for line in f:
            if '!AIVDM' in line or '!AIVDO' in line:
                total += 1
    try:
        for _ in decode_file(filepath):
            parsed += 1
    except Exception:
        pass
    return total, parsed


class TestParseRate:

    def test_parse_rate_meets_95_percent_gate(self):
        dataset_path = _NOAA_PATH
        if not os.path.exists(dataset_path):
            _generate_synthetic(dataset_path)

        if dataset_path.endswith('.csv'):
            total, parsed = _count_noaa_csv(dataset_path)
            fmt = 'NOAA CSV'
        else:
            total, parsed = _count_nmea_file(dataset_path)
            fmt = 'NMEA'

        assert total > 0, f"Dataset is empty: {dataset_path}"
        rate_pct = parsed / total * 100.0
        print(f"\n[D1.3a-audit] {fmt} parse rate: {parsed}/{total} = {rate_pct:.1f}%")

        assert rate_pct >= 95.0, (
            f"Parse rate {rate_pct:.1f}% < 95% gate.\n"
            f"Dataset: {dataset_path} ({fmt})\n"
            f"Parsed: {parsed}/{total} records"
        )

    def test_decoded_records_have_valid_coordinates(self):
        dataset_path = _NOAA_PATH
        if not os.path.exists(dataset_path):
            _generate_synthetic(dataset_path)

        if dataset_path.endswith('.csv'):
            from ais_bridge.dataset_loader import load_noaa_csv
            records = list(load_noaa_csv(dataset_path))
        else:
            from ais_bridge.nmea_decoder import decode_file
            records = list(decode_file(dataset_path))

        assert len(records) >= 100, f"Too few records: {len(records)}"

        sample = records[::100]
        for rec in sample:
            assert -90.0 <= rec.lat <= 90.0, f"Invalid lat: {rec.lat}"
            assert -180.0 <= rec.lon <= 180.0, f"Invalid lon: {rec.lon}"
            assert 0 <= rec.sog_kn <= 100.0, f"Implausible SOG: {rec.sog_kn}"


def _generate_synthetic(path: str):
    import subprocess
    script = os.path.join(
        os.path.dirname(__file__), '..', 'scripts', 'download_dataset.py')
    out_dir = os.path.dirname(path)
    subprocess.run(
        [sys.executable, script, '--source', 'synthetic', '--output-dir', out_dir],
        check=True
    )
