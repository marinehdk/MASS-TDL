#!/usr/bin/env python3
"""
build_enc_tiles.py — Full ENC data extraction pipeline
=====================================================
Extracts all navigational-chart-relevant layers from Kartverket FGDB files,
reprojects from EPSG:25833 (ETRS89 / UTM 33N) → EPSG:4326 (WGS-84),
and builds per-layer GeoJSON + tippecanoe mbtiles.

Usage:
    python scripts/build_enc_tiles.py [--gdb DATA/enc/Trondelag.gdb] [--out data/tiles] [--name trondelag]

The generated mbtiles can be served directly by Martin tile server.
"""

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

try:
    import fiona
    import pyproj
    from shapely.geometry import shape, mapping
    from shapely.ops import transform
except ImportError:
    print("ERROR: Required packages missing. Install via:")
    print("  pip install fiona pyproj shapely")
    sys.exit(1)

# ─── Layer configuration ────────────────────────────────────────────────────
# Each entry maps GDB layer → MVT source-layer name + properties to keep.
# Priority: 'core' layers are essential, 'detail' layers add richness at higher zoom.

LAYER_CONFIG = {
    # ── Core polygon layers ──
    "landareal": {
        "mvt_layer": "land",
        "geom_type": "polygon",
        "priority": "core",
        "properties": [],  # no extra props needed
        "description": "Land areas (islands, mainland)",
    },
    "dybdeareal": {
        "mvt_layer": "depth_area",
        "geom_type": "polygon",
        "priority": "core",
        "properties": ["minimumsdybde", "maksimumsdybde"],
        "description": "Depth areas with min/max depth values",
    },
    "torrfall": {
        "mvt_layer": "drying_area",
        "geom_type": "polygon",
        "priority": "core",
        "properties": [],
        "description": "Tidal flats / drying areas",
    },
    "fareomrade": {
        "mvt_layer": "danger_area",
        "geom_type": "polygon",
        "priority": "core",
        "properties": [],
        "description": "Danger areas",
    },
    "mudretomrade": {
        "mvt_layer": "dredged_area",
        "geom_type": "polygon",
        "priority": "detail",
        "properties": ["minimumsdybde", "maksimumsdybde"],
        "description": "Dredged areas",
    },
    "ikkekartlagtsjomaltomr": {
        "mvt_layer": "unsurveyed_area",
        "geom_type": "polygon",
        "priority": "detail",
        "properties": [],
        "description": "Unsurveyed areas",
    },
    "torrdokk": {
        "mvt_layer": "drydock",
        "geom_type": "polygon",
        "priority": "detail",
        "properties": [],
        "description": "Dry docks",
    },

    # ── Core line layers ──
    "kystkontur": {
        "mvt_layer": "coastline",
        "geom_type": "line",
        "priority": "core",
        "properties": ["kysttype"],
        "description": "Coastline contours",
    },
    "dybdekurve": {
        "mvt_layer": "depth_contour",
        "geom_type": "line",
        "priority": "core",
        "properties": ["dybde"],
        "description": "Depth contour lines",
    },
    "torrfallsgrense": {
        "mvt_layer": "drying_line",
        "geom_type": "line",
        "priority": "core",
        "properties": ["dybde"],
        "description": "Drying line boundary",
    },
    "kaibrygge": {
        "mvt_layer": "wharf",
        "geom_type": "line",
        "priority": "detail",
        "properties": [],
        "description": "Wharves and quays",
    },
    "molo": {
        "mvt_layer": "breakwater",
        "geom_type": "line",
        "priority": "detail",
        "properties": [],
        "description": "Breakwaters / moles",
    },
    "havelvsperre": {
        "mvt_layer": "dam",
        "geom_type": "line",
        "priority": "detail",
        "properties": [],
        "description": "Marine dams / barriers",
    },
    "slipp": {
        "mvt_layer": "slipway",
        "geom_type": "line",
        "priority": "detail",
        "properties": [],
        "description": "Slipways",
    },
    "pirkant": {
        "mvt_layer": "pier",
        "geom_type": "line",
        "priority": "detail",
        "properties": [],
        "description": "Pier edges",
    },
    "utstikker": {
        "mvt_layer": "jetty",
        "geom_type": "line",
        "priority": "detail",
        "properties": [],
        "description": "Jetties",
    },
    "fareomradegrense": {
        "mvt_layer": "danger_line",
        "geom_type": "line",
        "priority": "core",
        "properties": [],
        "description": "Danger area boundaries",
    },

    # ── Point layers ──
    "dybdepunkt": {
        "mvt_layer": "sounding",
        "geom_type": "point",
        "priority": "core",
        "properties": ["dybde", "dybdetype"],
        "description": "Spot soundings (depth points)",
    },
    "skjer": {
        "mvt_layer": "rock",
        "geom_type": "point",
        "priority": "core",
        "properties": [],
        "description": "Rocks / reefs",
    },
    "grunne": {
        "mvt_layer": "shoal",
        "geom_type": "point",
        "priority": "core",
        "properties": ["dybde"],
        "description": "Shoals / shallow spots",
    },
}

# For depth areas, this is the Kartverket schema with 'minimumdybde'
# but some layers use 'dybde' directly (dybdekurve, dybdepunkt)


def extract_layer(gdb_path: str, layer_name: str, config: dict, transformer) -> list:
    """Extract a single layer from FGDB to GeoJSON features."""
    features = []
    keep_props = config["properties"]
    mvt_layer = config["mvt_layer"]

    try:
        with fiona.open(gdb_path, layer=layer_name) as src:
            total = len(src)
            print(f"  [{layer_name}] → {mvt_layer}: {total} features ...", end="", flush=True)

            for i, feat in enumerate(src):
                try:
                    geom = shape(feat["geometry"])
                    if geom.is_empty:
                        continue

                    # Reproject to WGS-84
                    wgs_geom = transform(transformer, geom)

                    # Build minimal properties
                    props = {"_layer": mvt_layer}
                    for p in keep_props:
                        val = feat["properties"].get(p)
                        if val is not None:
                            # Convert to float for numeric fields
                            try:
                                props[p] = float(val)
                            except (ValueError, TypeError):
                                props[p] = val

                    features.append(
                        {
                            "type": "Feature",
                            "geometry": mapping(wgs_geom),
                            "properties": props,
                        }
                    )
                except Exception as e:
                    if i == 0:
                        print(f" [warn: {e}]", end="")
                    continue

            print(f" → {len(features)} exported")
    except Exception as e:
        print(f"  [{layer_name}] SKIP: {e}")

    return features


def build_geojsons(gdb_path: str, output_dir: Path, name: str) -> dict[str, Path]:
    """Extract all layers from GDB into per-layer GeoJSON files."""
    output_dir.mkdir(parents=True, exist_ok=True)

    # Set up coordinate transformer: EPSG:25833 → EPSG:4326
    transformer = pyproj.Transformer.from_crs(
        "EPSG:25833", "EPSG:4326", always_xy=True
    ).transform

    # Check what layers exist in the GDB
    available_layers = set(fiona.listlayers(gdb_path))
    print(f"\nGDB has {len(available_layers)} layers: {sorted(available_layers)}")

    geojson_files: dict[str, Path] = {}

    for gdb_layer, config in LAYER_CONFIG.items():
        if gdb_layer not in available_layers:
            print(f"  [{gdb_layer}] not in GDB, skipping")
            continue

        features = extract_layer(gdb_path, gdb_layer, config, transformer)
        if not features:
            continue

        mvt_layer = config["mvt_layer"]
        out_file = output_dir / f"{name}_{mvt_layer}.geojson"

        fc = {"type": "FeatureCollection", "features": features}
        with open(out_file, "w") as f:
            json.dump(fc, f)

        geojson_files[mvt_layer] = out_file
        print(f"    → saved {out_file.name} ({len(features)} features)")

    return geojson_files


def build_mbtiles(geojson_files: dict[str, Path], output_path: Path, name: str):
    """
    Use tippecanoe to build a single mbtiles with multiple named layers.
    Each GeoJSON becomes a separate source-layer in the MVT.
    """
    if not geojson_files:
        print("No GeoJSON files to process!")
        return

    # Build tippecanoe command with named layers
    cmd = [
        "tippecanoe",
        "-o", str(output_path),
        "--force",
        "--minimum-zoom=0",
        "--maximum-zoom=16",
        "--no-tile-compression",    # martin handles gzip itself
        "--simplification=4",       # moderate simplification for perf
        "--detect-shared-borders",  # important for depth area polygons
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
    parser = argparse.ArgumentParser(description="Build ENC vector tiles from Kartverket FGDB")
    parser.add_argument("--gdb", default="data/enc/Trondelag.gdb", help="Path to FGDB file")
    parser.add_argument("--out", default="data/tiles", help="Output directory for tiles")
    parser.add_argument("--name", default="trondelag", help="Dataset name prefix")
    parser.add_argument("--core-only", action="store_true", help="Only extract core layers")
    args = parser.parse_args()

    gdb_path = Path(args.gdb).resolve()
    output_dir = Path(args.out).resolve()
    mbtiles_path = output_dir / f"{args.name}.mbtiles"

    if not gdb_path.exists():
        print(f"ERROR: GDB not found at {gdb_path}")
        sys.exit(1)

    print(f"╔══════════════════════════════════════════════╗")
    print(f"║  ENC Vector Tile Builder                     ║")
    print(f"╠══════════════════════════════════════════════╣")
    print(f"║  GDB:    {gdb_path.name:<36} ║")
    print(f"║  Output: {mbtiles_path.name:<36} ║")
    print(f"╚══════════════════════════════════════════════╝")

    # If --core-only, filter
    if args.core_only:
        global LAYER_CONFIG
        LAYER_CONFIG = {k: v for k, v in LAYER_CONFIG.items() if v["priority"] == "core"}
        print(f"Core-only mode: {len(LAYER_CONFIG)} layers")

    # Step 1: Extract to per-layer GeoJSON
    geojson_dir = output_dir / "geojson"
    geojson_files = build_geojsons(str(gdb_path), geojson_dir, args.name)

    # Step 2: Build mbtiles
    build_mbtiles(geojson_files, mbtiles_path, args.name)

    print(f"\n✅ Done! Serve with: martin {output_dir}")


if __name__ == "__main__":
    main()
