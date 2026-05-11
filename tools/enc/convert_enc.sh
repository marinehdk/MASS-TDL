#!/usr/bin/env bash
set -euo pipefail

# D1.3b.3 ENC MVT tile pipeline
# Input:  ESRI File Geodatabase (.gdb) from Kartverket Norway
# Output: web/public/tiles/trondheim/{z}/{x}/{y}.pbf

GDB="${1:-/Users/marine/Code/colav-simulator/data/enc/Trondelag.gdb}"
OUTDIR="${2:-web/public/tiles/trondheim}"
TMPDIR="$(mktemp -d)"
LAYERS_FILE="$(dirname "$0")/layers.txt"

echo "=== D1.3b.3 ENC Pipeline ==="
echo "Source: $GDB"
echo "Output: $OUTDIR"

# 1. Export each layer to GeoJSONSeq (newline-delimited, memory-efficient)
echo "[1/3] Exporting layers from GDB..."
> "$TMPDIR/all.geojsons"
while read -r layer; do
  [ -z "$layer" ] && continue
  echo "  Layer: $layer"
  ogr2ogr -f GeoJSONSeq -t_srs EPSG:4326 \
    "$TMPDIR/${layer}.geojsons" "$GDB" "$layer" 2>/dev/null
  # Tag each feature with its source layer name for MapLibre filter expressions
  # GeoJSONSeq (RFC 8142) uses RS (0x1E) as record separator; strip it
  python3 -c "
import json, sys
for line in open('$TMPDIR/${layer}.geojsons'):
    line = line.lstrip('\x1e')
    feat = json.loads(line)
    feat.setdefault('properties', {})['_layer'] = '$layer'
    print(json.dumps(feat))
" >> "$TMPDIR/all.geojsons"
done < "$LAYERS_FILE"

FEAT_COUNT=$(wc -l < "$TMPDIR/all.geojsons")
echo "  Total features: $FEAT_COUNT"

# 2. Convert to MVT with Tippecanoe
echo "[2/3] Generating MVT tiles (Z6-Z14)..."
tippecanoe -q -Z6 -z14 \
  --drop-densest-as-needed \
  --simplification=4 \
  --maximum-zoom=14 \
  --minimum-zoom=6 \
  --force \
  --output "$TMPDIR/trondheim.mbtiles" \
  "$TMPDIR/all.geojsons" 2>&1

TILE_SIZE=$(du -h "$TMPDIR/trondheim.mbtiles" | cut -f1)
echo "  .mbtiles size: $TILE_SIZE"

# 3. Extract to directory
echo "[3/3] Extracting tiles..."
rm -rf "$OUTDIR"
mb-util --image_format=pbf "$TMPDIR/trondheim.mbtiles" "$OUTDIR" 2>&1

TILE_COUNT=$(find "$OUTDIR" -name '*.pbf' | wc -l)
echo "  Tile count: $TILE_COUNT"

rm -rf "$TMPDIR"
echo "=== Done: $OUTDIR ==="
