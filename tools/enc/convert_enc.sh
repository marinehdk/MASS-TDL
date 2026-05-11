#!/usr/bin/env bash
set -euo pipefail
GDB="${1:-/Users/marine/Code/colav-simulator/data/enc/Trondelag.gdb}"
OUTDIR="${2:-web/public/tiles/trondheim}"
TMPDIR="$(mktemp -d)"
LAYERS_FILE="$(dirname "$0")/layers.txt"
echo "=== D1.3b.3 ENC Pipeline v2 ==="
echo "[1/3] Exporting layers..."
LAYER_ARGS=()
while read -r layer; do
  [ -z "$layer" ] && continue
  echo "  $layer"
  OUTFILE="$TMPDIR/${layer}.geojson"
  ogr2ogr -f GeoJSON -t_srs EPSG:4326 "$OUTFILE" "$GDB" "$layer" 2>/dev/null
  LAYER_ARGS+=(-L "${layer}:${OUTFILE}")
done < "$LAYERS_FILE"
echo "[2/3] Tippecanoe..."
tippecanoe -q -Z6 -z14 --drop-densest-as-needed --simplification=4 \
  --maximum-zoom=14 --minimum-zoom=6 --force \
  --output "$TMPDIR/trondheim.mbtiles" "${LAYER_ARGS[@]}" 2>&1
echo "  $(du -h "$TMPDIR/trondheim.mbtiles" | cut -f1)"
echo "[3/3] Extracting..."
rm -rf "$OUTDIR"
mb-util --image_format=pbf "$TMPDIR/trondheim.mbtiles" "$OUTDIR" 2>&1 | tail -1
echo "  $(find "$OUTDIR" -name '*.pbf' | wc -l | tr -d ' ') tiles"
rm -rf "$TMPDIR"
echo "=== Done ==="
