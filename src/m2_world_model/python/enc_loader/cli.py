#!/usr/bin/env python3
"""ENC data loader CLI.

Usage:
    python -m enc_loader.cli build --input <s57-file> --output <geom.json>
    enc-loader build --input CN300100.000 --output out.geom.json
"""
import argparse
import json
import sys


def build_metadata(input_path: str, output_path: str) -> None:
    """Build ENC metadata JSON from raw S-57 file (placeholder).

    In the full implementation, this function will:
    1. Parse S-57 ISO 8211 binary format using a dedicated library
    2. Extract chart boundaries, TSS zones, narrow channels, depth areas
    3. Serialize as GeoJSON-compatible metadata consumed by C++ EncLoader

    Args:
        input_path: Path to the S-57 .000 file.
        output_path: Path for the output JSON metadata file.
    """
    metadata = {
        "name": input_path,
        "charts": [
            {
                "name": "placeholder",
                "lat_min": 30.0,
                "lat_max": 31.0,
                "lon_min": 121.5,
                "lon_max": 122.5,
                "has_tss": True,
                "has_narrow_channel": False,
                "min_depth_m": 15.0,
            }
        ],
    }
    with open(output_path, "w") as f:
        json.dump(metadata, f, indent=2)
    print(f"Written {output_path}")


def main() -> None:
    parser = argparse.ArgumentParser(description="ENC data loader")
    parser.add_argument("--input", required=True, help="S-57 input file")
    parser.add_argument("--output", required=True, help="Output JSON path")
    args = parser.parse_args()
    build_metadata(args.input, args.output)


if __name__ == "__main__":
    main()
