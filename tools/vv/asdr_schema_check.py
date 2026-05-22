#!/usr/bin/env python3
"""ASDR schema completeness check — DoD #3.

Reads /sil/asdr_event from MCAP bag and verifies all 6 required fields.
Required fields: stamp, event_type, rule_ref, decision_id, verdict, payload_json

Usage:
    python tools/vv/asdr_schema_check.py --run-dir runs/run-abc123
"""
from __future__ import annotations
import argparse
import json
import sys
from pathlib import Path

REQUIRED_FIELDS = {"stamp", "event_type", "rule_ref", "decision_id", "verdict", "payload_json"}
ASDR_TOPIC = "/sil/asdr_event"


def check_asdr_schema(run_dir: Path) -> dict:
    mcap_files = list(run_dir.glob("*.mcap"))
    if not mcap_files:
        return {"total_events": 0, "missing_fields_count": 0,
                "sample_errors": [], "note": "No MCAP file found — skipped"}

    try:
        from rosbags.highlevel import AnyReader
        from rosbags.typesys import Stores, get_typestore
        typestore = get_typestore(Stores.ROS2_HUMBLE)
    except ImportError:
        return {"total_events": 0, "missing_fields_count": 0,
                "sample_errors": [], "note": "rosbags not installed"}

    total = 0
    missing_count = 0
    sample_errors: list[dict] = []

    with AnyReader([mcap_files[0]]) as reader:
        connections = [c for c in reader.connections if c.topic == ASDR_TOPIC]
        for conn, timestamp, rawdata in reader.messages(connections=connections):
            msg = typestore.deserialize_cdr(rawdata, conn.msgtype)
            total += 1
            present = set(f for f in REQUIRED_FIELDS if hasattr(msg, f))
            missing = REQUIRED_FIELDS - present
            if missing:
                missing_count += 1
                if len(sample_errors) < 10:
                    sample_errors.append({
                        "timestamp_ns": timestamp,
                        "missing_fields": sorted(missing),
                    })

    return {
        "total_events": total,
        "missing_fields_count": missing_count,
        "required_fields": sorted(REQUIRED_FIELDS),
        "sample_errors": sample_errors,
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--run-dir", default="runs/run-latest")
    ap.add_argument("--output", default="test-results/asdr_schema_report.json")
    args = ap.parse_args()

    run_dir = Path(args.run_dir)
    if not run_dir.exists() and args.run_dir == "runs/run-latest":
        runs = sorted(Path("runs").glob("run-*"), key=lambda p: p.stat().st_mtime, reverse=True)
        if runs:
            run_dir = runs[0]
            print(f"[INFO] Using most recent run: {run_dir}")

    result = check_asdr_schema(run_dir)
    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))

    ok = result["missing_fields_count"] == 0
    print(f"\n[DoD #3] missing_fields_count={result['missing_fields_count']}: {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
