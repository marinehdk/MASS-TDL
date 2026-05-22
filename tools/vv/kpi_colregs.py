#!/usr/bin/env python3
"""COLREGs violation rate collector — DoD #16.

Reads the most recent MCAP bag, counts:
  - total_decisions: number of /sil/asdr_event with verdict != UNSPECIFIED
  - violations: number with verdict == FAIL (3)

Usage:
    python tools/vv/kpi_colregs.py --run-dir runs/run-latest
"""
from __future__ import annotations
import argparse
import json
import sys
from pathlib import Path

ASDR_TOPIC = "/sil/asdr_event"
VERDICT_FAIL = 3
VERDICT_UNSPEC = 0


def collect_violation_rate(run_dir: Path) -> dict:
    mcap_files = list(run_dir.glob("*.mcap"))
    if not mcap_files:
        return {"total_decisions": 0, "violations": 0, "violation_rate": 0.0,
                "note": "No MCAP file — returning 0 violations"}

    try:
        from rosbags.highlevel import AnyReader
        from rosbags.typesys import Stores, get_typestore
        typestore = get_typestore(Stores.ROS2_HUMBLE)
    except ImportError:
        return {"total_decisions": 0, "violations": 0, "violation_rate": 0.0,
                "note": "rosbags not installed"}

    total = 0
    violations = 0

    with AnyReader([mcap_files[0]]) as reader:
        connections = [c for c in reader.connections if c.topic == ASDR_TOPIC]
        for conn, _, rawdata in reader.messages(connections=connections):
            msg = typestore.deserialize_cdr(rawdata, conn.msgtype)
            verdict = getattr(msg, "verdict", VERDICT_UNSPEC)
            if verdict != VERDICT_UNSPEC:
                total += 1
                if verdict == VERDICT_FAIL:
                    violations += 1

    rate = violations / total if total > 0 else 0.0
    return {"total_decisions": total, "violations": violations,
            "violation_rate": round(rate, 4)}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--run-dir", default="runs/run-latest")
    ap.add_argument("--output", default="test-results/kpi_colregs.json")
    args = ap.parse_args()

    run_dir = Path(args.run_dir)
    if not run_dir.exists() and args.run_dir == "runs/run-latest":
        runs = sorted(Path("runs").glob("run-*"), key=lambda p: p.stat().st_mtime, reverse=True)
        if runs:
            run_dir = runs[0]

    result = collect_violation_rate(run_dir)
    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))

    ok = result["violation_rate"] < 0.05
    print(f"\n[DoD #16] violation_rate={result['violation_rate']:.1%} < 5%: {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
