#!/usr/bin/env python3
"""
add_simulation_settings.py — Batch inject simulation_settings.backend feature flag
into all scenario YAML files under a scenarios directory.

Usage:
    python tools/add_simulation_settings.py --scenarios-dir scenarios/
    python tools/add_simulation_settings.py --scenarios-dir scenarios/ --default-backend ros2
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

import yaml

SIMULATION_SETTINGS_DEFAULTS = {
    "backend": "demo",
    "dt": 0.02,
    "total_time": 700,
    "enc_path": "data/enc/trondheim_fjord",
    "coordinate_origin": [63.44, 10.38],
    "dynamics_mode": "internal",
}


def is_schema_file(filepath: Path) -> bool:
    """Check if filename indicates a schema definition (not a scenario)."""
    return "schema" in filepath.name.lower()


def needs_simulation_settings(data: dict) -> tuple[bool, bool]:
    """
    Check if the YAML data needs simulation_settings injection.

    Returns:
        (needs_full_block, needs_backend_only)
        - needs_full_block: metadata exists but simulation_settings missing entirely
        - needs_backend_only: simulation_settings exists but 'backend' key missing
    """
    metadata = data.get("metadata")
    if not isinstance(metadata, dict):
        return False, False

    sim_settings = metadata.get("simulation_settings")
    if not isinstance(sim_settings, dict):
        return True, False

    if "backend" not in sim_settings:
        return False, True

    return False, False


def process_yaml_file(filepath: Path, default_backend: str) -> str:
    """
    Process a single YAML file, adding backend feature flag.

    Returns one of: "updated", "skipped", "error"
    """
    try:
        with open(filepath, "r", encoding="utf-8") as f:
            data = yaml.safe_load(f)
    except Exception as e:
        print(f"  ERROR: {filepath}: failed to read YAML: {e}", file=sys.stderr)
        return "error"

    if not isinstance(data, dict):
        print(f"  SKIP: {filepath}: not a dict YAML", file=sys.stderr)
        return "skipped"

    needs_full, needs_backend = needs_simulation_settings(data)

    if needs_full:
        metadata = data["metadata"]
        settings = dict(SIMULATION_SETTINGS_DEFAULTS)
        settings["backend"] = default_backend
        metadata["simulation_settings"] = settings
    elif needs_backend:
        data["metadata"]["simulation_settings"]["backend"] = default_backend
    else:
        return "skipped"

    try:
        with open(filepath, "w", encoding="utf-8") as f:
            yaml.dump(
                data,
                f,
                default_flow_style=False,
                sort_keys=False,
                allow_unicode=True,
            )
    except Exception as e:
        print(f"  ERROR: {filepath}: failed to write YAML: {e}", file=sys.stderr)
        return "error"

    return "updated"


def discover_scenario_files(scenarios_dir: Path) -> list[Path]:
    """Find all YAML files under scenarios_dir, excluding schema and manifest files."""
    yaml_files: list[Path] = []
    for yf in sorted(scenarios_dir.rglob("*.yaml")):
        if is_schema_file(yf):
            continue
        if "manifest" in str(yf):
            continue
        yaml_files.append(yf)
    return yaml_files


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Batch-inject simulation_settings.backend feature flag into scenario YAMLs"
    )
    parser.add_argument(
        "--scenarios-dir",
        required=True,
        help="Root directory containing scenario YAML files (recursive)",
    )
    parser.add_argument(
        "--default-backend",
        default="demo",
        choices=["demo", "ros2"],
        help="Backend value to inject (default: demo)",
    )
    args = parser.parse_args()

    scenarios_dir = Path(args.scenarios_dir)
    if not scenarios_dir.is_dir():
        print(f"ERROR: --scenarios-dir '{scenarios_dir}' is not a directory", file=sys.stderr)
        sys.exit(1)

    yaml_files = discover_scenario_files(scenarios_dir)

    if not yaml_files:
        print("No scenario YAML files found.")
        sys.exit(0)

    print(f"Found {len(yaml_files)} scenario YAML file(s) under '{scenarios_dir}'")
    print(f"Default backend: '{args.default_backend}'")
    print()

    updated = 0
    skipped = 0
    errors = 0

    for yf in yaml_files:
        result = process_yaml_file(yf, args.default_backend)
        if result == "updated":
            updated += 1
            print(f"  ✓ {yf.relative_to(scenarios_dir)}")
        elif result == "skipped":
            skipped += 1
        elif result == "error":
            errors += 1

    print()
    print(f"Summary: {updated} files updated, {skipped} already had settings, {errors} errors")


if __name__ == "__main__":
    main()
