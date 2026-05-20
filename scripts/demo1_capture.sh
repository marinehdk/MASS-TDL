#!/usr/bin/env bash
set -euo pipefail

# ============================================================
# D1.3.2.3 · DEMO-1 R14 Head-On Captain View · Screen Capture
# ============================================================
# Prerequisites:
#   1. docker compose up (SIL container + foxglove_bridge on :8765)
#   2. web/ Vite dev server running (npm run dev → :5173)
#   3. Playwright browsers installed (npx playwright install chromium)
#
# Output:
#   demo-evidence/demo1-headon-r14-{timestamp}.webm   (video)
#   demo-evidence/demo1-headon-r14-{timestamp}.png    (screenshot)
# ============================================================

TIMESTAMP=$(date +%Y%m%d-%H%M%S)
EVIDENCE_DIR="demo-evidence"
MONITOR_URL="http://localhost:5173/#/monitor/imazu-08"
CAPTURE_DURATION_S="${1:-300}"  # default 5 min

mkdir -p "$EVIDENCE_DIR"

echo "=== D1.3.2.3 DEMO-1 Capture ==="
echo "Target: $MONITOR_URL"
echo "Duration: ${CAPTURE_DURATION_S}s"
echo "Output: $EVIDENCE_DIR/demo1-headon-r14-${TIMESTAMP}.*"
echo ""

# ---- Check prerequisites ----
if ! curl -s --max-time 2 "http://localhost:5173" > /dev/null 2>&1; then
  echo "ERROR: Vite dev server not reachable at http://localhost:5173"
  echo "  Start with: cd web && npm run dev"
  exit 1
fi

if ! curl -s --max-time 2 "http://localhost:8765" > /dev/null 2>&1; then
  echo "WARNING: foxglove_bridge not reachable at ws://localhost:8765"
  echo "  E2E test may show demo_telemetry fallback"
fi

# ---- Screenshot (single frame, post-warmup) ----
echo "[1/2] Capturing screenshot (15s warmup)..."
npx playwright screenshot \
  --browser chromium \
  --viewport-size 1920,1080 \
  "$MONITOR_URL" \
  "$EVIDENCE_DIR/demo1-headon-r14-${TIMESTAMP}.png" 2>&1 | tail -1

# ---- Video recording (Headless Chromium via Playwright) ----
echo "[2/2] Recording ${CAPTURE_DURATION_S}s video..."
cat > /tmp/demo1-recorder.mjs << 'EORECORDER'
import { chromium } from 'playwright';
import { mkdirSync } from 'fs';

const url = process.argv[2] || 'http://localhost:5173/#/monitor/imazu-08';
const duration = parseInt(process.argv[3] || '300', 10);
const outFile = process.argv[4] || 'demo-evidence/demo1-headon-r14.webm';

mkdirSync('demo-evidence', { recursive: true });

const browser = await chromium.launch({ headless: true });
const context = await browser.newContext({
  viewport: { width: 1920, height: 1080 },
  recordVideo: { dir: 'demo-evidence', size: { width: 1920, height: 1080 } },
});

const page = await context.newPage();
await page.goto(url, { waitUntil: 'networkidle' });
console.log(`Recording ${duration}s → ${outFile}`);

// Wait for WS to connect + telemetry to appear
try {
  await page.waitForFunction(
    () => document.body.textContent?.includes('WS CONNECTED'),
    { timeout: 15000 },
  );
  console.log('WS connected — telemetry live');
} catch {
  console.warn('WS not detected — recording demo_telemetry fallback');
}

// Record for specified duration
await page.waitForTimeout(duration * 1000);

await context.close();
await browser.close();

// Rename the auto-generated video to our timestamped filename
const fs = await import('fs');
const path = await import('path');
const files = fs.readdirSync('demo-evidence');
const videoFile = files.find(f => f.endsWith('.webm'));
if (videoFile) {
  const src = path.join('demo-evidence', videoFile);
  fs.renameSync(src, outFile);
  console.log(`Renamed: ${videoFile} → ${outFile}`);
} else {
  console.warn('No .webm video found in demo-evidence/');
}
EORECORDER

node /tmp/demo1-recorder.mjs "$MONITOR_URL" "$CAPTURE_DURATION_S" \
  "$EVIDENCE_DIR/demo1-headon-r14-${TIMESTAMP}.webm" 2>&1

rm -f /tmp/demo1-recorder.mjs

echo ""
echo "=== Capture complete ==="
echo "  Screenshot: $EVIDENCE_DIR/demo1-headon-r14-${TIMESTAMP}.png"
echo "  Video:      $EVIDENCE_DIR/demo1-headon-r14-${TIMESTAMP}.webm"
echo "  Duration:   ${CAPTURE_DURATION_S}s"
