#!/usr/bin/env bash
set -euo pipefail
# D1.3b.3 DEMO-1 R14 Head-on live demo SOP
# Usage: bash tools/demo/demo1_start.sh

echo "=== D1.3b.3 DEMO-1: R14 Head-on Live Demo ==="
echo ""

# Resolve project root absolute path
PROJECT_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ROS_PID=""; API_PID=""; VITE_PID=""
cleanup() { kill $ROS_PID $API_PID $VITE_PID 2>/dev/null || true; }
trap cleanup EXIT

# 1. Start ROS2 sil_mock_node + foxglove_bridge (if ROS2 available)
if command -v ros2 &>/dev/null; then
  echo "[1/4] Starting ROS2 sil_mock_node + foxglove_bridge..."
  ros2 launch "$PROJECT_ROOT/launch/foxglove_sil.launch.xml" &
  ROS_PID=$!
  sleep 3
  echo "  foxglove_bridge: ws://localhost:8765"
else
  echo "[1/4] ROS2 not available — using DEMO static mode"
fi

# 2. Start FastAPI backend (from M8 python package directory)
echo "[2/4] Starting FastAPI backend..."
cd "$PROJECT_ROOT/src/l3_tdl_kernel/m8_hmi_transparency_bridge/python"
python3 -m uvicorn web_server.main:app --host 0.0.0.0 --port 8000 &
API_PID=$!
sleep 2
echo "  FastAPI: http://localhost:8000"

# 3. Start Vite dev server (or use production build)
echo "[3/4] Starting frontend..."
if [ -d "$PROJECT_ROOT/web/dist" ]; then
  echo "  Using production build (http://localhost:8000)"
  open http://localhost:8000 2>/dev/null || echo "  Open http://localhost:8000"
else
  cd "$PROJECT_ROOT/web" && npx vite --host &
  VITE_PID=$!
  sleep 3
  echo "  Vite: http://localhost:5173"
  open http://localhost:5173 2>/dev/null || echo "  Open http://localhost:5173"
fi

echo ""
echo "=== DEMO-1 Ready ==="
echo "Press Ctrl+C to stop"
wait
