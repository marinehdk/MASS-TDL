#!/usr/bin/env python3
"""
build_s57_tiles.py — Convert S-57 ENC files to MBTiles format
============================================================
Extracts key layers from S-57 (.000) chart files, maps them to standard MVT
layers, and builds a single mbtiles using tippecanoe.

Usage:
    python scripts/build_s57_tiles.py --enc /path/to/EA200004.000 --out data/tiles --name coastal_archipelago
"""

import argparse
import json
import subprocess
import sys
from pathlib import Path

try:
    import fiona
    from shapely.geometry import shape, mapping
except ImportError:
    print("ERROR: Required packages missing. Install via:")
    print("  pip install fiona shapely")
    sys.exit(1)

# S-57 Layer mapping configuration
# Maps S-57 layer name -> MVT source-layer name + property mappings (src_prop, dest_prop)
LAYER_MAPPING = {
    "LNDARE": {
        "mvt_layer": "land",
        "properties": [],
    },
    "DEPARE": {
        "mvt_layer": "depth_area",
        "properties": [("DRVAL1", "minimumsdybde"), ("DRVAL2", "maksimumsdybde")],
    },
    "COALNE": {
        "mvt_layer": "coastline",
        "properties": [],
    },
    "DEPCNT": {
        "mvt_layer": "depth_contour",
        "properties": [("VALDCO", "dybde")],
    },
    "SOUNDG": {
        "mvt_layer": "sounding",
        "properties": [],  # Handled separately to extract Z coordinate
    },
    "UWTROC": {
        "mvt_layer": "rock",
        "properties": [],
    },
    "OBSTRN": {
        "mvt_layer": "shoal",
        "properties": [("VALSOU", "dybde")],
    }
}

def extract_layer(enc_path: Path, s57_layer: str, config: dict) -> list:
    """Extract features from a single S-57 layer to GeoJSON features."""
    features = []
    mvt_layer = config["mvt_layer"]

    try:
        with fiona.open(str(enc_path), layer=s57_layer) as src:
            total = len(src)
            print(f"  [{s57_layer}] → {mvt_layer}: {total} features ...", end="", flush=True)

            for feat in src:
                if not feat["geometry"]:
                    continue

                geom_type = feat["geometry"]["type"]

                # Handle SOUNDG differently (split MultiPoint into individual Point features)
                if s57_layer == "SOUNDG":
                    coords = feat["geometry"]["coordinates"]
                    if geom_type == "MultiPoint":
                        for pt in coords:
                            if len(pt) >= 2:
                                lon, lat = pt[0], pt[1]
                                depth = pt[2] if len(pt) > 2 else 0.0
                                features.append({
                                    "type": "Feature",
                                    "geometry": {"type": "Point", "coordinates": [lon, lat]},
                                    "properties": {
                                        "_layer": mvt_layer,
                                        "dybde": float(depth)
                                    }
                                })
                    elif geom_type == "Point":
                        pt = coords
                        if len(pt) >= 2:
                            lon, lat = pt[0], pt[1]
                            depth = pt[2] if len(pt) > 2 else 0.0
                            features.append({
                                "type": "Feature",
                                "geometry": {"type": "Point", "coordinates": [lon, lat]},
                                "properties": {
                                    "_layer": mvt_layer,
                                    "dybde": float(depth)
                                }
                            })
                else:
                    # General layer extraction
                    try:
                        geom = shape(feat["geometry"])
                        if geom.is_empty:
                            continue

                        # Clean geometry (force 2D for standard layers if they have Z coordinates)
                        # shapely's mapping will export coordinates as list of tuples
                        geom_geojson = mapping(geom)
                        
                        # Build properties
                        props = {"_layer": mvt_layer}
                        for src_prop, dest_prop in config["properties"]:
                            val = feat["properties"].get(src_prop)
                            if val is not None:
                                try:
                                    props[dest_prop] = float(val)
                                except (ValueError, TypeError):
                                    props[dest_prop] = val

                        features.append({
                            "type": "Feature",
                            "geometry": geom_geojson,
                            "properties": props,
                        })
                    except Exception as e:
                        continue

            print(f" → {len(features)} features exported")
    except Exception as e:
        print(f" SKIP: {e}")

    return features

def build_geojsons(enc_path: Path, output_dir: Path, name: str) -> dict[str, Path]:
    """Extract S-57 layers into per-layer GeoJSON files."""
    output_dir.mkdir(parents=True, exist_ok=True)
    
    # Check available layers
    available_layers = set(fiona.listlayers(str(enc_path)))
    print(f"ENC has {len(available_layers)} layers: {sorted(available_layers)}")

    geojson_files = {}

    for s57_layer, config in LAYER_MAPPING.items():
        if s57_layer not in available_layers:
            print(f"  [{s57_layer}] not in ENC, skipping")
            continue

        features = extract_layer(enc_path, s57_layer, config)
        if not features:
            continue

        mvt_layer = config["mvt_layer"]
        out_file = output_dir / f"{name}_{mvt_layer}.geojson"

        fc = {"type": "FeatureCollection", "features": features}
        with open(out_file, "w") as f:
            json.dump(fc, f)

        geojson_files[mvt_layer] = out_file

    return geojson_files

def build_mbtiles(geojson_files: dict[str, Path], output_path: Path):
    """Run tippecanoe to build a single mbtiles with multiple named layers."""
    if not geojson_files:
        print("No GeoJSON files to process!")
        return

    cmd = [
        "tippecanoe",
        "-o", str(output_path),
        "--force",
        "--minimum-zoom=0",
        "--maximum-zoom=16",
        "--drop-rate=0",
        "--base-zoom=16",
        "--buffer=64",
        "--full-detail=14",
        "--simplification=1",
        "--no-tiny-polygon-reduction",
        "--detect-shared-borders",
        "--no-tile-compression",
        "--no-tile-size-limit",
        "--no-feature-limit",
    ]

    for mvt_layer, geojson_path in geojson_files.items():
        cmd.extend(["-L", f"{mvt_layer}:{geojson_path}"])

    print(f"\nRunning tippecanoe with {len(geojson_files)} layers...")
    print(f"  Command: {' '.join(cmd[:8])} ...")

    try:
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
            print(f"tippecanoe FAILED:\n{result.stderr}")
            sys.exit(1)
        print(f"  ✓ Built {output_path} ({output_path.stat().st_size / 1024 / 1024:.1f} MB)")
    except FileNotFoundError:
        print("ERROR: tippecanoe not found. Install via: brew install tippecanoe")
        sys.exit(1)

def main():
    parser = argparse.ArgumentParser(description="Build MBTiles from S-57 ENC file")
    parser.add_argument("--enc", required=True, help="Path to S-57 (.000) file")
    parser.add_argument("--out", default="data/tiles", help="Output directory for tiles")
    parser.add_argument("--name", default="coastal_archipelago", help="Dataset name prefix")
    args = parser.parse_args()

    enc_path = Path(args.enc).resolve()
    output_dir = Path(args.out).resolve()
    mbtiles_path = output_dir / f"{args.name}.mbtiles"

    if not enc_path.exists():
        print(f"ERROR: S-57 file not found at {enc_path}")
        sys.exit(1)

    print("╔══════════════════════════════════════════════╗")
    print("║  S-57 ENC to MBTiles Converter               ║")
    print("╠══════════════════════════════════════════════╣")
    print(f"║  ENC:    {enc_path.name:<36} ║")
    print(f"║  Output: {mbtiles_path.name:<36} ║")
    print("╚══════════════════════════════════════════════╝\n")

    # Step 1: Extract to per-layer GeoJSON
    geojson_dir = output_dir / "geojson"
    geojson_files = build_geojsons(enc_path, geojson_dir, args.name)

    # Step 2: Build mbtiles
    build_mbtiles(geojson_files, mbtiles_path)

    print(f"\n✅ Done! Generated MBTiles at {mbtiles_path}")

if __name__ == "__main__":
    main()
