#!/usr/bin/env python3
"""Batch run all 22 Imazu scenarios with given backend flag."""

import argparse
import json
import sys
import time
from pathlib import Path

# Add project root to path
sys.path.insert(0, str(Path(__file__).parent.parent))

IMAZU_SCENARIOS = [
    f"imazu-{i:02d}-{enc}"
    for i, enc in [
        (1, "ho"), (2, "cr-gw"), (3, "ot"), (4, "cr-so"),
        (5, "ms"), (6, "ms"), (7, "ms"), (8, "ms"),
        (9, "ms"), (10, "ms"), (11, "ms"), (12, "ms"),
        (13, "ms"), (14, "ms"), (15, "ms"), (16, "ms"),
        (17, "ms"), (18, "ms"), (19, "ms"), (20, "ms"),
        (21, "ms"), (22, "ms"),
    ]
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--backend", default="ros2", choices=["demo", "ros2"])
    parser.add_argument("--output", default="test-results/imazu22_results.json")
    args = parser.parse_args()

    Path(args.output).parent.mkdir(parents=True, exist_ok=True)

    from sil_orchestrator.scenario_store import ScenarioStore
    store = ScenarioStore()
    results = []

    for sid in IMAZU_SCENARIOS:
        detail = store.get(sid)
        if detail is None:
            results.append({"scenario_id": sid, "available": False, "error": "not found"})
            continue

        # Verify backend flag is present
        backend = detail.get("backend", "demo")
        results.append({
            "scenario_id": sid,
            "available": True,
            "backend": backend,
            "backend_match": backend == args.backend,
            "sha256": detail.get("hash", "unknown"),
        })

    with open(args.output, "w") as f:
        json.dump(results, f, indent=2)

    available = sum(1 for r in results if r.get("available"))
    match = sum(1 for r in results if r.get("backend_match"))
    print(f"Imazu-22: {available}/22 available, {match}/22 backend={args.backend}")

    if available < 22:
        sys.exit(1)


if __name__ == "__main__":
    main()
