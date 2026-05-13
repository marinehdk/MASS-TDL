#!/bin/bash

# Ensure script stops on error
set -e

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <input_enc_path> <output_name>"
    echo "Example: $0 data/enc/Trondelag.gdb trondelag"
    exit 1
fi

INPUT_PATH="$1"
# Resolve to absolute path
if [[ "$INPUT_PATH" != /* ]]; then
    INPUT_PATH="$PWD/$INPUT_PATH"
fi
OUTPUT_NAME="$2"

PROJECT_ROOT=$(cd "$(dirname "$0")/.." && pwd)
DATA_DIR="$PROJECT_ROOT/data/tiles"
GEOJSON_FILE="$DATA_DIR/${OUTPUT_NAME}.geojson"
MBTILES_FILE="$DATA_DIR/${OUTPUT_NAME}.mbtiles"

mkdir -p "$DATA_DIR"

echo "Step 1: Extracting MVT geometry to GeoJSON from $INPUT_PATH..."
/Users/marine/Code/colav-simulator/.venv/bin/python "$PROJECT_ROOT/scripts/export_geojson.py" "/Users/marine/Code/colav-simulator/config/seacharts.yaml" "$GEOJSON_FILE"

echo "Step 2: Converting GeoJSON to MBTiles using tippecanoe..."
rm -f "$MBTILES_FILE"
tippecanoe -zg -l enc -o "$MBTILES_FILE" --drop-densest-as-needed --force "$GEOJSON_FILE"

echo "MBTiles generated at $MBTILES_FILE"
