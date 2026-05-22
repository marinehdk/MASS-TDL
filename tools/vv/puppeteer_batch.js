#!/usr/bin/env node
/**
 * tools/vv/puppeteer_batch.js
 *
 * Puppeteer headless batch GIF renderer.
 * Takes --run-ids-file <json> or --runs-dir <dir>.
 * For each run: navigates to /monitor/{runId}?dev=1&replay={runId},
 * captures 8 keyframe PNGs, assembles GIF via gif-encoder-2
 * (with canvas fallback when node-canvas is unavailable).
 *
 * Usage:
 *   node tools/vv/puppeteer_batch.js --runs-dir ./runs
 *   node tools/vv/puppeteer_batch.js --run-ids-file ./run_ids.json
 *   CONCURRENCY=8 node tools/vv/puppeteer_batch.js --runs-dir ./runs
 *   BASE_URL=http://localhost:8000 node tools/vv/puppeteer_batch.js --runs-dir ./runs
 *
 * Output:
 *   evidence/{runId}/          — 8 PNG keyframes + replay.gif (or .no-canvas marker)
 *   test-results/gif_batch_summary.json
 */

const fs = require('fs');
const path = require('path');

const CONCURRENCY = parseInt(process.env.CONCURRENCY, 10) || 4;
const BASE_URL = process.env.BASE_URL || 'http://localhost:3000';

// ── 8 keyframes (0%, 14%, 28%, 43%, 57%, 71%, 85%, 100% of progression) ──
const KEYFRAME_PCTS = [0, 14, 28, 43, 57, 71, 85, 100];
// Pad for filename sorting
const FRAME_LABELS = KEYFRAME_PCTS.map((p) => String(p).padStart(3, '0'));

// ── Canvas availability check ─────────────────────────────────────────
let hasCanvas = false;
try {
  require.resolve('canvas');
  hasCanvas = true;
} catch {
  /* node-canvas not installed — fallback to PNG-only */
}

/**
 * Assemble animated GIF from PNG frames using gif-encoder-2 + canvas.
 * Fallback: save PNGs individually and write a .no-canvas marker.
 */
async function assembleGif(framePaths, outputPath) {
  if (!hasCanvas) {
    const markerPath = outputPath.replace(/\.gif$/, '.no-canvas');
    fs.writeFileSync(markerPath, '');
    console.warn(`    [fallback] canvas unavailable — wrote marker ${path.basename(markerPath)}`);
    return false;
  }

  const { createCanvas, loadImage } = require('canvas');
  const GIFEncoder = require('gif-encoder-2');

  // Load all frames in parallel
  const images = await Promise.all(framePaths.map((fp) => loadImage(fp)));
  const width = images[0].width;
  const height = images[0].height;

  return new Promise((resolve, reject) => {
    const encoder = new GIFEncoder(width, height);
    const writeStream = fs.createWriteStream(outputPath);

    encoder.createReadStream().pipe(writeStream);

    writeStream.on('finish', () => {
      console.log(`    GIF written: ${path.basename(outputPath)} (${width}x${height})`);
      resolve(true);
    });
    writeStream.on('error', (err) => {
      console.warn(`    GIF write error: ${err.message}`);
      reject(err);
    });

    encoder.setQuality(20);
    encoder.setDelay(500); // 500 ms per frame
    encoder.setRepeat(0);  // loop forever
    encoder.start();

    const canvas = createCanvas(width, height);
    const ctx = canvas.getContext('2d');

    for (const img of images) {
      ctx.drawImage(img, 0, 0);
      encoder.addFrame(ctx);
    }

    encoder.finish();
  });
}

// ── CLI argument parsing ──────────────────────────────────────────────

function parseArgs() {
  const args = process.argv.slice(2);
  const opts = { runIdsFile: null, runsDir: null };

  for (let i = 0; i < args.length; i++) {
    if (args[i] === '--run-ids-file' && i + 1 < args.length) {
      opts.runIdsFile = args[++i];
    } else if (args[i] === '--runs-dir' && i + 1 < args.length) {
      opts.runsDir = args[++i];
    }
  }

  return opts;
}

/**
 * Resolve the list of run IDs from either a JSON file or directory listing.
 * JSON may be: ["id1", "id2"], { run_ids: [...] }, or { runIds: [...] }.
 */
function getRunIds({ runIdsFile, runsDir }) {
  if (runIdsFile) {
    const raw = fs.readFileSync(runIdsFile, 'utf-8');
    const data = JSON.parse(raw);
    if (Array.isArray(data)) return data;
    if (data.run_ids) return data.run_ids;
    if (data.runIds) return data.runIds;
    console.error('ERROR: JSON must be an array or { run_ids: [...] }');
    process.exit(1);
  }

  if (runsDir) {
    return fs
      .readdirSync(runsDir)
      .filter((name) => {
        const stat = fs.statSync(path.join(runsDir, name));
        return stat.isDirectory() && !name.startsWith('.');
      })
      .sort();
  }

  console.error('ERROR: specify --run-ids-file <json> or --runs-dir <dir>');
  process.exit(1);
}

// ── Per-run processing ────────────────────────────────────────────────

/**
 * Process a single run: navigate to the page, wait for telemetry, capture
 * 8 keyframes, and assemble the GIF.
 */
async function processRun(browser, runId, runsDir) {
  const evidenceDir = path.join('evidence', runId);
  fs.mkdirSync(evidenceDir, { recursive: true });

  const page = await browser.newPage();
  await page.setViewport({ width: 1280, height: 720 });

  try {
    const url = `${BASE_URL}/#/monitor/${runId}?dev=1&replay=${runId}`;
    console.log(`  [${runId}] navigating to ${url}`);

    await page.goto(url, {
      waitUntil: 'networkidle0',
      timeout: 45_000,
    });

    // Wait for telemetry overlay to disappear or initial render (max 15 s)
    try {
      await page.waitForFunction(
        () => {
          const el = document.querySelector('[data-testid="simulation-monitor"]');
          // Check that the "AWAITING TELEMETRY" overlay is gone
          const overlay = el?.textContent?.includes('AWAITING TELEMETRY');
          return !!el && !overlay;
        },
        { timeout: 15_000 },
      );
    } catch {
      console.warn(`    [${runId}] telemetry overlay may still be visible — capturing anyway`);
    }

    // Small settle pause after data arrives
    await new Promise((r) => setTimeout(r, 2_000));

    // Capture 8 keyframes with increasing delay to let sim time advance
    const framePaths = [];
    for (let i = 0; i < KEYFRAME_PCTS.length; i++) {
      const pct = KEYFRAME_PCTS[i];
      // Wait between frames: earlier frames closer together, later spaced out
      if (i > 0) {
        const waitMs = 2_000 + i * 1_500;
        await new Promise((r) => setTimeout(r, waitMs));
      }

      const pngPath = path.join(evidenceDir, `frame_${FRAME_LABELS[i]}.png`);
      await page.screenshot({ path: pngPath, type: 'png' });
      framePaths.push(pngPath);
      console.log(`    [${runId}] keyframe ${i + 1}/${KEYFRAME_PCTS.length} (${pct}%)`);
    }

    // Assemble GIF from captured frames
    const gifPath = path.join(evidenceDir, 'replay.gif');
    let gifOk = false;
    if (hasCanvas) {
      try {
        gifOk = await assembleGif(framePaths, gifPath);
      } catch (err) {
        console.warn(`    [${runId}] GIF assembly failed: ${err.message}`);
      }
    } else {
      console.warn(`    [${runId}] canvas unavailable — GIF not assembled`);
    }

    return { runId, frames: framePaths.length, gif: gifOk ? gifPath : null, error: null };
  } catch (err) {
    console.error(`  [${runId}] FAILED: ${err.message}`);
    return { runId, frames: 0, gif: null, error: err.message };
  } finally {
    await page.close();
  }
}

// ── Main ──────────────────────────────────────────────────────────────

async function main() {
  const args = parseArgs();
  const runIds = getRunIds(args);
  const runsDir = args.runsDir || 'runs';

  if (runIds.length === 0) {
    console.log('No run IDs found — nothing to do.');
    process.exit(0);
  }

  console.log(`Batch GIF renderer: ${runIds.length} runs, concurrency=${CONCURRENCY}`);
  if (!hasCanvas) console.log('  [note] canvas package not found — PNG frames only, no GIF');

  const browser = await require('puppeteer').launch({
    headless: true,
    args: [
      '--no-sandbox',
      '--disable-setuid-sandbox',
      '--disable-gpu',
      '--disable-dev-shm-usage',
    ],
  });

  const results = [];

  // Process runs in parallel chunks
  for (let offset = 0; offset < runIds.length; offset += CONCURRENCY) {
    const chunk = runIds.slice(offset, offset + CONCURRENCY);
    const chunkNum = Math.floor(offset / CONCURRENCY) + 1;
    const totalChunks = Math.ceil(runIds.length / CONCURRENCY);

    console.log(`\nChunk ${chunkNum}/${totalChunks} (runs ${offset + 1}–${offset + chunk.length})`);
    const chunkResults = await Promise.allSettled(
      chunk.map((runId) => processRun(browser, runId, runsDir)),
    );

    for (const r of chunkResults) {
      if (r.status === 'fulfilled') results.push(r.value);
      else results.push({ runId: 'unknown', frames: 0, gif: null, error: r.reason?.message ?? 'Unknown error' });
    }
  }

  await browser.close();

  // ── Summary ──────────────────────────────────────────────────────────
  const succeeded = results.filter((r) => !r.error);
  const failed = results.filter((r) => r.error);

  const summary = {
    total: runIds.length,
    succeeded: succeeded.length,
    failed: failed.length,
    concurrency: CONCURRENCY,
    has_canvas: hasCanvas,
    results,
    generated_at: new Date().toISOString(),
  };

  fs.mkdirSync('test-results', { recursive: true });
  fs.writeFileSync('test-results/gif_batch_summary.json', JSON.stringify(summary, null, 2));
  console.log(`\n${'='.repeat(50)}`);
  console.log(`Done: ${succeeded.length} OK, ${failed.length} failed`);
  if (failed.length > 0) {
    console.log('Failed runs:');
    for (const f of failed) {
      console.log(`  - ${f.runId}: ${f.error}`);
    }
  }

  process.exit(failed.length > 0 ? 1 : 0);
}

main().catch((err) => {
  console.error('Fatal error:', err);
  process.exit(1);
});
