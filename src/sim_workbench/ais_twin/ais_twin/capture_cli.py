from __future__ import annotations

import argparse
import asyncio
from dataclasses import replace
import os
from pathlib import Path
import time

from ais_twin.aisstream_provider import AISstreamProvider
from ais_twin.config import load_config
from ais_twin.model import CanonicalAISRecord
from ais_twin.normalizer import normalize_records
from ais_twin.store import DatasetStore


def _strip_raw_json(record: CanonicalAISRecord) -> CanonicalAISRecord:
    return replace(record, raw_json={})


def _record_for_normalization(record: CanonicalAISRecord) -> CanonicalAISRecord:
    return replace(
        record,
        nav_status=None,
        ship_name=None,
        ship_type=None,
        raw_message_type="",
        raw_json={},
        quality_flags=frozenset(),
    )


async def run_capture(config_path: Path, api_key: str, overwrite: bool = False) -> int:
    cfg = load_config(config_path)
    provider = AISstreamProvider(api_key=api_key)
    store = DatasetStore(cfg.output_dir, overwrite=overwrite)
    deadline = time.monotonic() + cfg.capture_duration_hours * 3600.0
    count = 0
    # Keep only normalized scalar fields in memory; full raw JSON stays in raw.jsonl.
    normalizer_records: list[CanonicalAISRecord] = []
    records = provider.records(cfg.bbox).__aiter__()
    while True:
        remaining_s = deadline - time.monotonic()
        if remaining_s <= 0.0:
            break
        try:
            record = await asyncio.wait_for(records.__anext__(), timeout=remaining_s)
        except (asyncio.TimeoutError, StopAsyncIteration):
            break
        store.write_raw(record)
        normalizer_records.append(_record_for_normalization(record))
        count += 1
    segments = normalize_records(normalizer_records)
    store.write_tracks(segments)
    store.write_manifest(
        provider=cfg.provider,
        bbox=cfg.bbox,
        route_path=str(cfg.route_path),
        capture_duration_hours=cfg.capture_duration_hours,
        records_written=count,
        segments_written=len(segments),
        api_key=api_key,
    )
    return count


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", required=True)
    parser.add_argument("--overwrite", action="store_true")
    args = parser.parse_args(argv)
    api_key = os.environ["AISSTREAM_API_KEY"]
    count = asyncio.run(run_capture(Path(args.config), api_key, overwrite=args.overwrite))
    print(f"ais_twin_capture records_written={count}")
