#!/usr/bin/env python3
# python/build_fmu.py
"""CLI tool: build fcb_mmg_4dof.fmu from FCBMmgFmu pythonfmu model.
Usage: python -m fmi_bridge.python.build_fmu --output fcb_mmg_4dof.fmu
"""
from __future__ import annotations
import argparse
import sys
from pathlib import Path
from pythonfmu.builder import FmuBuilder


def build(output_path: str, config_path: str = "config/fcb_dynamics.yaml") -> Path:
    from fmi_bridge.python.fcb_mmg_fmu import FCBMmgFmu
    fmu_path_str = FmuBuilder.build_FMU(
        FCBMmgFmu,
        dest=output_path,
        needsExecutionTool=False,
    )
    fmu_path = Path(fmu_path_str)
    print(f"FMU built: {fmu_path}")
    return fmu_path


def main() -> None:
    parser = argparse.ArgumentParser(description="Build FCB MMG FMU")
    parser.add_argument("--output", default="fcb_mmg_4dof.fmu")
    parser.add_argument("--config", default="config/fcb_dynamics.yaml")
    args = parser.parse_args()
    build(args.output, args.config)


if __name__ == "__main__":
    main()
