#!/usr/bin/env python3
"""Canonical CLI for the COLREGs clean 8-probe acceptance workflow."""

from __future__ import annotations

import importlib.util
import sys
from pathlib import Path


def _load_runner():
    runner_path = Path(__file__).resolve().with_name("run_6_scenarios.py")
    spec = importlib.util.spec_from_file_location("run_6_scenarios", runner_path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def main(argv=None):
    return _load_runner().main(argv)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
