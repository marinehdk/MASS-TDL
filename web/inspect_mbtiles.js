import Database from 'better-sqlite3';
import path from 'path';

const mbtilesPath = '/Users/marine/Code/MASS-L3-Tactical Layer/data/tiles/trondelag.mbtiles';

try {
  console.log(`Opening MBTiles at: ${mbtilesPath}`);
  const db = new Database(mbtilesPath, { readonly: true, fileMustExist: true });

  console.log('\n--- METADATA ---');
  const rows = db.prepare('SELECT name, value FROM metadata').all();
  for (const row of rows) {
    if (row.name === 'json') {
      console.log(`${row.name}:`);
      try {
        console.log(JSON.stringify(JSON.parse(row.value), null, 2));
      } catch {
        console.log(row.value);
      }
    } else {
      console.log(`${row.name}: ${row.value}`);
    }
  }

  console.log('\n--- TILE COUNTS PER ZOOM LEVEL ---');
  const tileCounts = db.prepare('SELECT zoom_level, COUNT(*) as count FROM tiles GROUP BY zoom_level').all();
  for (const row of tileCounts) {
    console.log(`Zoom ${row.zoom_level}: ${row.count} tiles`);
  }

  db.close();
} catch (e) {
  console.error('Error opening MBTiles:', e);
}
