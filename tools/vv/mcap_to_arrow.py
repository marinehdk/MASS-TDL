#!/usr/bin/env python3
"""MCAP bag -> Apache Arrow IPC replay file.

Schema: {timestamp_ns: int64, channel: utf8, payload_bytes: binary}
One record batch per topic per 10-second window (for efficient HTTP range requests).

Usage:
    python tools/vv/mcap_to_arrow.py --run-dir runs/run-abc123 --output runs/run-abc123/replay.arrow
"""
from __future__ import annotations
import argparse
import sys
from pathlib import Path
from typing import Optional

import pyarrow as pa
import pyarrow.ipc as pa_ipc

DEFAULT_TOPICS = [
    "/sil/own_ship_state",
    "/sil/target_vessel_state",
    "/sil/sat2_data",
    "/sil/sat3_data",
    "/sil/sotif_metrics",
    "/sil/asdr_event",
    "/sil/scoring",
]
BATCH_WINDOW_NS = 10 * 10**9

SCHEMA = pa.schema([
    pa.field("timestamp_ns", pa.int64()),
    pa.field("channel", pa.string()),
    pa.field("payload_bytes", pa.binary()),
])


def _empty_file(out_path: Path) -> None:
    with pa_ipc.new_file(str(out_path), SCHEMA) as writer:
        pass


def convert_mcap_to_arrow(
    mcap_path: str,
    out_path: str,
    topics: list[str] | None = None,
) -> int:
    """Convert MCAP -> Arrow IPC. Returns number of messages written."""
    if topics is None:
        topics = DEFAULT_TOPICS

    mcap_file = Path(mcap_path)
    out = Path(out_path)
    out.parent.mkdir(parents=True, exist_ok=True)

    if not mcap_file.exists() or mcap_file.stat().st_size == 0:
        _empty_file(out)
        return 0

    try:
        from rosbags.highlevel import AnyReader
    except ImportError as e:
        print("Error: rosbags dependency is missing.", file=sys.stderr)
        raise e

    timestamps: list[int] = []
    channels: list[str] = []
    payloads: list[bytes] = []
    total = 0

    with AnyReader([mcap_file]) as reader:
        connections = [c for c in reader.connections if c.topic in topics]
        for conn, timestamp, rawdata in reader.messages(connections=connections):
            timestamps.append(timestamp)
            channels.append(conn.topic)
            payloads.append(bytes(rawdata))
            total += 1

    triples = sorted(zip(timestamps, channels, payloads), key=lambda x: x[0])

    with pa_ipc.new_file(str(out), SCHEMA) as writer:
        if not triples:
            pass
        else:
            t_start = triples[0][0]
            batch_ts, batch_ch, batch_pl = [], [], []
            for ts, ch, pl in triples:
                if ts - t_start >= BATCH_WINDOW_NS and batch_ts:
                    writer.write_batch(pa.record_batch(
                        [batch_ts, batch_ch, batch_pl], schema=SCHEMA))
                    batch_ts, batch_ch, batch_pl = [], [], []
                    t_start = ts
                batch_ts.append(ts)
                batch_ch.append(ch)
                batch_pl.append(pl)
            if batch_ts:
                writer.write_batch(pa.record_batch(
                    [batch_ts, batch_ch, batch_pl], schema=SCHEMA))
    return total


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--run-dir", required=True, help="Path to run directory (contains *.mcap)")
    ap.add_argument("--output", help="Arrow output path (default: {run_dir}/replay.arrow)")
    ap.add_argument("--topics", nargs="*", help="Topic filter list")
    args = ap.parse_args()

    run_dir = Path(args.run_dir)
    mcap_files = list(run_dir.glob("*.mcap"))
    if not mcap_files:
        print(f"[WARN] No .mcap in {run_dir}, writing empty Arrow file")
        mcap_path = str(run_dir / "nonexistent.mcap")
    else:
        mcap_path = str(mcap_files[0])

    out_path = args.output or str(run_dir / "replay.arrow")
    count = convert_mcap_to_arrow(mcap_path, out_path, args.topics)
    print(f"[OK] Wrote {count} messages -> {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
