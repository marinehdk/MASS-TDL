/**
 * Lightweight MBTiles tile server for development.
 * Reads .mbtiles (SQLite) files and serves MVT tiles at:
 *   GET /:tileset/:z/:x/:y
 *
 * Usage:
 *   node web/tile-server.cjs [tiles-directory] [port]
 *
 * Example:
 *   node web/tile-server.cjs ../data/tiles 3000
 */

const http = require('http');
const path = require('path');
const fs = require('fs');

let Database;
try {
  Database = require('better-sqlite3');
} catch {
  console.error('ERROR: better-sqlite3 not found. Install via: npm i better-sqlite3');
  process.exit(1);
}

const TILES_DIR = path.resolve(process.argv[2] || path.join(__dirname, '..', 'data', 'tiles'));
const PORT = parseInt(process.argv[3] || '3000', 10);

// ── Scan for .mbtiles files ─────────────────────────────────────────────────
const dbs = {};
function scanTilesets() {
  if (!fs.existsSync(TILES_DIR)) {
    console.error(`Tiles directory not found: ${TILES_DIR}`);
    process.exit(1);
  }
  for (const file of fs.readdirSync(TILES_DIR)) {
    if (!file.endsWith('.mbtiles')) continue;
    const name = file.replace('.mbtiles', '');
    const fullPath = path.join(TILES_DIR, file);
    try {
      const db = new Database(fullPath, { readonly: true, fileMustExist: true });
      dbs[name] = db;
      
      // Read metadata
      const meta = {};
      const rows = db.prepare('SELECT name, value FROM metadata').all();
      for (const row of rows) meta[row.name] = row.value;
      console.log(`  ✓ ${name} (${(fs.statSync(fullPath).size / 1e6).toFixed(1)} MB, z${meta.minzoom || '?'}-${meta.maxzoom || '?'})`);
    } catch (e) {
      console.warn(`  ✗ ${name}: ${e.message}`);
    }
  }
}

// ── HTTP Server ─────────────────────────────────────────────────────────────
const server = http.createServer((req, res) => {
  // CORS headers
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET, OPTIONS');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type');
  
  if (req.method === 'OPTIONS') {
    res.writeHead(204);
    res.end();
    return;
  }

  const url = new URL(req.url, `http://localhost:${PORT}`);
  
  // Catalog endpoint
  if (url.pathname === '/catalog' || url.pathname === '/') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(Object.keys(dbs)));
    return;
  }

  // TileJSON endpoint: /:tileset
  const tileJsonMatch = url.pathname.match(/^\/([a-z0-9_-]+)$/i);
  if (tileJsonMatch) {
    const name = tileJsonMatch[1];
    const db = dbs[name];
    if (!db) {
      res.writeHead(404, { 'Content-Type': 'text/plain' });
      res.end(`Tileset "${name}" not found`);
      return;
    }
    const meta = {};
    const rows = db.prepare('SELECT name, value FROM metadata').all();
    for (const row of rows) meta[row.name] = row.value;
    
    const tileJson = {
      tilejson: '3.0.0',
      name: name,
      tiles: [`http://localhost:${PORT}/${name}/{z}/{x}/{y}`],
      minzoom: parseInt(meta.minzoom || '0'),
      maxzoom: parseInt(meta.maxzoom || '16'),
      bounds: meta.bounds ? meta.bounds.split(',').map(Number) : undefined,
      center: meta.center ? meta.center.split(',').map(Number) : undefined,
      vector_layers: meta.json ? JSON.parse(meta.json).vector_layers : [],
    };
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(tileJson, null, 2));
    return;
  }

  // Tile endpoint: /:tileset/:z/:x/:y[.pbf]
  const match = url.pathname.match(/^\/([a-z0-9_-]+)\/(\d+)\/(\d+)\/(\d+)(?:\.pbf)?$/i);
  if (!match) {
    res.writeHead(404, { 'Content-Type': 'text/plain' });
    res.end('Not found');
    return;
  }

  const [, tileset, z, x, y] = match;
  const db = dbs[tileset];
  if (!db) {
    res.writeHead(404, { 'Content-Type': 'text/plain' });
    res.end(`Tileset "${tileset}" not found`);
    return;
  }

  // MBTiles uses TMS y-flip
  const tmsY = (1 << parseInt(z)) - 1 - parseInt(y);

  try {
    console.log(`Tile request: z=${z} x=${x} y=${y}`); // Debug log
    const row = db.prepare(
      'SELECT tile_data FROM tiles WHERE zoom_level = ? AND tile_column = ? AND tile_row = ?'
    ).get(parseInt(z), parseInt(x), tmsY);

    if (!row || !row.tile_data) {
      res.writeHead(204);
      res.end();
      return;
    }

    let data = row.tile_data;

    // Check if data is gzip-compressed (first two bytes: 0x1f 0x8b)
    const isGzipped = data.length > 2 && data[0] === 0x1f && data[1] === 0x8b;

    res.writeHead(200, {
      'Content-Type': 'application/x-protobuf',
      ...(isGzipped ? { 'Content-Encoding': 'gzip' } : {}),
      'Cache-Control': 'public, max-age=3600',
    });
    res.end(data);
  } catch (e) {
    res.writeHead(500, { 'Content-Type': 'text/plain' });
    res.end(`Error: ${e.message}`);
  }
});

// ── Start ─────────────────────────────────────────────────────────────────
console.log(`\n╔══════════════════════════════════════════╗`);
console.log(`║  ENC Tile Server                         ║`);
console.log(`╠══════════════════════════════════════════╣`);
console.log(`║  Directory: ${path.basename(TILES_DIR).padEnd(27)} ║`);
console.log(`║  Port:      ${String(PORT).padEnd(27)} ║`);
console.log(`╚══════════════════════════════════════════╝`);
console.log(`\nScanning tilesets...`);

scanTilesets();

if (Object.keys(dbs).length === 0) {
  console.error('\nNo .mbtiles files found!');
  process.exit(1);
}

server.listen(PORT, () => {
  console.log(`\n🗺️  Serving ${Object.keys(dbs).length} tileset(s) at http://localhost:${PORT}`);
  console.log(`   Tiles: http://localhost:${PORT}/<tileset>/{z}/{x}/{y}`);
  console.log(`   Press Ctrl+C to stop.\n`);
});
