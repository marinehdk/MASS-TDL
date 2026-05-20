#!/usr/bin/env python3
"""ENC data loader CLI.

Usage:
    python -m enc_loader.cli build --input <s57-file> --output <geom.json>
"""
import argparse
from pathlib import Path

from enc_loader.geometry_serializer import serialize
from enc_loader.s57_parser import S57Parser


def build_metadata(input_path: str, output_path: str) -> None:
    """Build ENC metadata JSON from raw S-57 file.

    Invokes S57Parser to read features and geometry_serializer to write JSON.
    """
    print(f"Parsing S-57 file: {input_path}")
    parser = S57Parser()
    bounds, polygons = parser.parse(Path(input_path))

    print(f"Parsed {len(polygons)} features. Serializing to {output_path}...")
    serialize(
        chart_name=Path(input_path).name,
        bounds=bounds,
        polygons=polygons,
        out_path=Path(output_path)
    )
    print(f"Successfully built and written SENC to {output_path}")


def main() -> None:
    parser = argparse.ArgumentParser(description="ENC data loader")
    parser.add_argument("--input", required=True, help="S-57 input file (.000)")
    parser.add_argument("--output", required=True, help="Output JSON path")
    args = parser.parse_args()
    build_metadata(args.input, args.output)


if __name__ == "__main__":
    main()
