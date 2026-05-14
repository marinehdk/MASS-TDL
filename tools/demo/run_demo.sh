#!/usr/bin/env bash
set -euo pipefail

echo "=== SIL Demo-1: Imazu-01 Head-On ==="

python3 -c "import fastapi, uvicorn, websockets" 2>/dev/null || {
  echo "Missing deps. Run: pip install fastapi uvicorn websockets"
  exit 1
}

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEMO_DIR="$REPO_ROOT/tools/demo"

cleanup() { kill "$SERVER_PID" 2>/dev/null || true; }
trap cleanup EXIT

echo "[1/2] Starting demo server (REST :8000 · WS :8765)..."
cd "$DEMO_DIR"
python3 demo_server.py &
SERVER_PID=$!
sleep 2

echo "[2/2] Starting Vite dev server (:5173)..."
cd "$REPO_ROOT/web"
npx vite --host &
VITE_PID=$!
sleep 2

echo ""
echo "=== Ready ==="
echo "  Frontend : http://localhost:5173/#/builder"
echo "  REST API : http://localhost:8000/api/v1/scenarios"
echo "  WS       : ws://localhost:8765"
echo ""
echo "Press Ctrl+C to stop."
wait "$SERVER_PID"