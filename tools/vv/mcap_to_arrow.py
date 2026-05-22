#!/usr/bin/env python3
"""Convert SIL MCAP replay data to Arrow IPC.

The replay Arrow schema is intentionally narrow:
timestamp_ns int64, channel string, payload_bytes binary.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Iterable, Sequence

import pyarrow as pa
import pyarrow.ipc as pa_ipc

DEFAULT_TOPICS = (
    "/sil/own_ship_state",
    "/sil/target_vessel_state",
    "/sil/sat2_data",
    "/sil/sat3_data",
    "/sil/sotif_metrics",
    "/sil/asdr_event",
    "/sil/scoring",
)

BATCH_WINDOW_NS = 10_000_000_000

SCHEMA = pa.schema(
    [
        pa.field("timestamp_ns", pa.int64()),
        pa.field("channel", pa.string()),
        pa.field("payload_bytes", pa.binary()),
    ]
)


def _empty_table() -> pa.Table:
    return pa.table(
        {
            "timestamp_ns": pa.array([], type=pa.int64()),
            "channel": pa.array([], type=pa.string()),
            "payload_bytes": pa.array([], type=pa.binary()),
        },
        schema=SCHEMA,
    )


def _write_batches(out_path: Path, rows: Sequence[tuple[int, str, bytes]]) -> int:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with pa.OSFile(str(out_path), "wb") as sink:
        with pa_ipc.new_file(sink, SCHEMA) as writer:
            if not rows:
                writer.write_table(_empty_table())
                return 0

            batch: list[tuple[int, str, bytes]] = []
            batch_start_ns: int | None = None

            def flush() -> None:
                if not batch:
                    return
                timestamps, channels, payloads = zip(*batch)
                table = pa.table(
                    {
                        "timestamp_ns": pa.array(timestamps, type=pa.int64()),
                        "channel": pa.array(channels, type=pa.string()),
                        "payload_bytes": pa.array(payloads, type=pa.binary()),
                    },
                    schema=SCHEMA,
                )
                writer.write_table(table)
                batch.clear()

            for timestamp_ns, channel, payload_bytes in rows:
                if batch_start_ns is None:
                    batch_start_ns = timestamp_ns
                if timestamp_ns - batch_start_ns >= BATCH_WINDOW_NS:
                    flush()
                    batch_start_ns = timestamp_ns
                batch.append((timestamp_ns, channel, payload_bytes))

            flush()
            return len(rows)


def _read_mcap_rows(
    mcap_path: Path,
    topics: Iterable[str],
) -> list[tuple[int, str, bytes]]:
    try:
        from rosbags.highlevel import AnyReader
    except ImportError:
        print("warning: rosbags is not installed; writing empty Arrow file", file=sys.stderr)
        return []

    selected_topics = set(topics)
    rows: list[tuple[int, str, bytes]] = []
    try:
        with AnyReader([mcap_path]) as reader:
            connections = [
                connection
                for connection in reader.connections
                if connection.topic in selected_topics
            ]
            for connection, timestamp_ns, rawdata in reader.messages(connections=connections):
                rows.append((int(timestamp_ns), connection.topic, bytes(rawdata)))
    except Exception as exc:
        print(f"warning: failed to read MCAP {mcap_path}: {exc}", file=sys.stderr)
        return []

    rows.sort(key=lambda row: (row[0], row[1]))
    return rows


def convert_mcap_to_arrow(
    mcap_path: str,
    out_path: str,
    topics: Iterable[str] | None = None,
) -> int:
    """Convert an MCAP file to Arrow IPC and return the message count."""
    source = Path(mcap_path)
    destination = Path(out_path)
    selected_topics = tuple(topics) if topics is not None else DEFAULT_TOPICS

    if not source.exists() or source.stat().st_size == 0:
        return _write_batches(destination, [])

    rows = _read_mcap_rows(source, selected_topics)
    return _write_batches(destination, rows)


def _find_mcap(run_dir: Path) -> Path | None:
    mcaps = sorted(run_dir.glob("*.mcap"))
    if not mcaps:
        mcaps = sorted(run_dir.rglob("*.mcap"))
    return mcaps[0] if mcaps else None


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-dir", required=True, help="Run directory containing an MCAP file")
    parser.add_argument("--output", required=True, help="Output Arrow IPC file")
    parser.add_argument("--topics", nargs="*", default=None, help="Topics to include")
    args = parser.parse_args(argv)

    run_dir = Path(args.run_dir)
    mcap_path = _find_mcap(run_dir) if run_dir.exists() else None
    if mcap_path is None:
        print(f"warning: no .mcap found in {run_dir}; writing empty Arrow file", file=sys.stderr)
        count = _write_batches(Path(args.output), [])
    else:
        count = convert_mcap_to_arrow(str(mcap_path), args.output, topics=args.topics)

    print(f"wrote {count} messages to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
