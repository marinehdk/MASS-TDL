import type { LayerSpecification, SourceSpecification } from 'maplibre-gl';

// ─────────────────────────────────────────────────────────────────────────────
// S-52 / IHO colour palette reference
// ─────────────────────────────────────────────────────────────────────────────
// The colour assignments below follow IHO S-52 "day" presentation library
// conventions for ECDIS displays. Norwegian Kartverket FGDB layers are mapped
// to their closest S-57 object classes.

export const S52 = {
  // Water depths — Reversed palette (deeper = darker blue)
  DEPVS: '#f0f9ff',  // Very shallow  0-2m
  DEPIT: '#bae6fd',  // Intertidal    2-5m
  DEPMS: '#7dd3fc',  // Medium shallow 5-10m
  DEPMD: '#38bdf8',  // Medium deep   10-20m
  DEPDW: '#0284c7',  // Deep water    20-50m
  DEPVD: '#0369a1',  // Very deep     50m+
  LANDA: '#f0e8d0',  // Land area (LANDA)
  CHBRN: '#d8c8a0',  // Land detail / drying height
  COAST: '#4a3728',  // Coastline
  DEPSC: '#7baec2',  // Safety contour
  LITGN: '#c8dab8',  // Tidal / drying area (green-ish)
  DANGR: '#d63031',  // Danger symbols
  CSTLN: '#333333',  // Coastline thick
  CHBLK: '#333333',  // Chart black (labels, contour lines)
  SNDG1: '#5c8aa6',  // Sounding label < 10m
  SNDG2: '#333333',  // Sounding label >= 10m
  BKAJ1: '#808080',  // Built-up areas (wharves, piers)
  RESBL: '#ba6400',  // Restricted area boundary
  ROCKS: '#333333',  // Rock symbol
};

// ─────────────────────────────────────────────────────────────────────────────
// Tile sources
// ─────────────────────────────────────────────────────────────────────────────

// OpenStreetMap raster basemap — visible ocean/coast/place names even before
// the S-57 MVT pipeline is wired. Acts as a fallback so DEMO-1 has *some* chart
// substrate. Spec §9 LATER swaps this for S-52-styled S-57 vector tiles.
export const OSM_TILE_URL = 'https://tile.openstreetmap.org/{z}/{x}/{y}.png';

export const osmSource: SourceSpecification = {
  type: 'raster',
  tiles: [OSM_TILE_URL],
  tileSize: 256,
  attribution: '© OpenStreetMap contributors',
  minzoom: 0,
  maxzoom: 19,
};

export const osmLayer: LayerSpecification = {
  id: 'osm-base',
  type: 'raster',
  source: 'osm',
  paint: {
    // Cool nautical tint so the base reads as "chart" not "road map"
    'raster-saturation': -0.4,
    'raster-brightness-min': 0.15,
    'raster-brightness-max': 0.85,
    'raster-contrast': 0.1,
  },
};

// S-57 multi-layer vector tile source hosted by Martin MVT server.
// The mbtiles is built from Kartverket FGDB by scripts/build_enc_tiles.py
// and contains 20 named source-layers (land, depth_area, coastline, etc.).
export const S57_TILE_URL = 'http://localhost:3000/trondelag/{z}/{x}/{y}';
export const s57Source: SourceSpecification = {
  type: 'vector',
  tiles: [S57_TILE_URL],
  minzoom: 0,
  maxzoom: 16,
};

// ─────────────────────────────────────────────────────────────────────────────
// S-57 ENC rendering layers (ordered bottom → top)
// ─────────────────────────────────────────────────────────────────────────────

export const s57Layers: LayerSpecification[] = [
  // ── 1. Deep-water background ──────────────────────────────────────────────
  // The ocean floor colour. Tiles that don't intersect any depth_area polygon
  // show through to the map background (set in the style's background layer).

  // ── 2. Depth areas (S-57: DEPARE) ─────────────────────────────────────────
  // Bathymetric zones colour-coded by minimum depth following S-52 palette.
  {
    id: 'enc-depth-area',
    type: 'fill',
    source: 's57',
    'source-layer': 'depth_area',
    paint: {
      'fill-color': [
        'step', ['coalesce', ['get', 'minimumsdybde'], 0],
        S52.DEPVS,          //  0-2m  very shallow
        2,  S52.DEPIT,      //  2-5m  intertidal band
        5,  S52.DEPMS,      //  5-10m medium shallow
        10, S52.DEPMD,      // 10-20m medium deep
        20, S52.DEPDW,      // 20-50m deep
        50, S52.DEPVD,      // 50m+   very deep (nearly white)
      ],
      'fill-opacity': 0.92,
    },
  },

  // ── 3. Drying / intertidal areas (S-57: UNSARE tidal) ─────────────────────
  {
    id: 'enc-drying-area',
    type: 'fill',
    source: 's57',
    'source-layer': 'drying_area',
    paint: {
      'fill-color': S52.LITGN,
      'fill-opacity': 0.65,
    },
  },

  // ── 4. Unsurveyed area (hatched pattern via semi-transparent fill) ────────
  {
    id: 'enc-unsurveyed',
    type: 'fill',
    source: 's57',
    'source-layer': 'unsurveyed_area',
    paint: {
      'fill-color': '#c0c0c0',
      'fill-opacity': 0.25,
    },
  },

  // ── 5. Dredged areas ──────────────────────────────────────────────────────
  {
    id: 'enc-dredged',
    type: 'fill',
    source: 's57',
    'source-layer': 'dredged_area',
    paint: {
      'fill-color': S52.DEPIT,
      'fill-opacity': 0.6,
    },
  },

  // ── 6. Danger area fill (S-57: CTNARE) ────────────────────────────────────
  {
    id: 'enc-danger-area',
    type: 'fill',
    source: 's57',
    'source-layer': 'danger_area',
    paint: {
      'fill-color': S52.DANGR,
      'fill-opacity': 0.12,
    },
  },

  // ── 7. Land (S-57: LNDARE) ────────────────────────────────────────────────
  {
    id: 'enc-land',
    type: 'fill',
    source: 's57',
    'source-layer': 'land',
    paint: {
      'fill-color': S52.LANDA,
      'fill-opacity': 1.0,
    },
  },

  // ── 8. Dry dock (S-57: DRYDOC) ────────────────────────────────────────────
  {
    id: 'enc-drydock',
    type: 'fill',
    source: 's57',
    'source-layer': 'drydock',
    minzoom: 12,
    paint: {
      'fill-color': '#b8b0a0',
      'fill-opacity': 0.8,
    },
  },

  // ── 9. Depth contour lines (S-57: DEPCNT) ────────────────────────────────
  {
    id: 'enc-depth-contour',
    type: 'line',
    source: 's57',
    'source-layer': 'depth_contour',
    minzoom: 9,
    paint: {
      'line-color': [
        'step', ['coalesce', ['get', 'dybde'], 0],
        S52.DEPSC,      // shallow contours: highlighted blue
        10, '#9bb8c8',  // medium
        50, '#b0c8d4',  // deep contours: lighter
      ],
      'line-width': [
        'step', ['coalesce', ['get', 'dybde'], 0],
        1.2,     // shallow contours thicker
        10, 0.8,
        50, 0.5, // deep contours thinner
      ],
      'line-opacity': 0.7,
    },
  },

  // ── 10. Depth contour labels ──────────────────────────────────────────────
  {
    id: 'enc-depth-contour-label',
    type: 'symbol',
    source: 's57',
    'source-layer': 'depth_contour',
    minzoom: 12,
    layout: {
      'symbol-placement': 'line',
      'text-field': ['concat', ['to-string', ['get', 'dybde']], 'm'],
      'text-font': ['Open Sans Regular'],
      'text-size': 9,
      'text-max-angle': 30,
      'text-allow-overlap': false,
      'text-padding': 40,
    },
    paint: {
      'text-color': S52.DEPSC,
      'text-halo-color': '#ffffff',
      'text-halo-width': 1.2,
    },
  },

  // ── 11. Drying line / tidal boundary (S-57: SLCONS / drying) ──────────────
  {
    id: 'enc-drying-line',
    type: 'line',
    source: 's57',
    'source-layer': 'drying_line',
    minzoom: 10,
    paint: {
      'line-color': S52.LITGN,
      'line-width': 1.0,
      'line-dasharray': [4, 2],
      'line-opacity': 0.7,
    },
  },

  // ── 12. Coastline (S-57: COALNE) ──────────────────────────────────────────
  {
    id: 'enc-coastline',
    type: 'line',
    source: 's57',
    'source-layer': 'coastline',
    paint: {
      'line-color': S52.CSTLN,
      'line-width': [
        'interpolate', ['linear'], ['zoom'],
        0, 0.5,
        8, 1.0,
        12, 1.8,
        16, 2.5,
      ],
    },
  },

  // ── 13. Danger area outline ───────────────────────────────────────────────
  {
    id: 'enc-danger-line',
    type: 'line',
    source: 's57',
    'source-layer': 'danger_line',
    paint: {
      'line-color': S52.DANGR,
      'line-width': 2.0,
      'line-dasharray': [6, 3],
    },
  },

  // ── 14. Breakwater / mole (S-57: SLCONS) ──────────────────────────────────
  {
    id: 'enc-breakwater',
    type: 'line',
    source: 's57',
    'source-layer': 'breakwater',
    minzoom: 10,
    paint: {
      'line-color': S52.CHBLK,
      'line-width': 2.5,
    },
  },

  // ── 15. Wharf / quay (S-57: SLCONS) ───────────────────────────────────────
  {
    id: 'enc-wharf',
    type: 'line',
    source: 's57',
    'source-layer': 'wharf',
    minzoom: 11,
    paint: {
      'line-color': S52.BKAJ1,
      'line-width': 2.0,
    },
  },

  // ── 16. Pier edge ─────────────────────────────────────────────────────────
  {
    id: 'enc-pier',
    type: 'line',
    source: 's57',
    'source-layer': 'pier',
    minzoom: 12,
    paint: {
      'line-color': S52.BKAJ1,
      'line-width': 1.8,
    },
  },

  // ── 17. Jetty ─────────────────────────────────────────────────────────────
  {
    id: 'enc-jetty',
    type: 'line',
    source: 's57',
    'source-layer': 'jetty',
    minzoom: 12,
    paint: {
      'line-color': S52.BKAJ1,
      'line-width': 1.5,
    },
  },

  // ── 18. Slipway ───────────────────────────────────────────────────────────
  {
    id: 'enc-slipway',
    type: 'line',
    source: 's57',
    'source-layer': 'slipway',
    minzoom: 13,
    paint: {
      'line-color': '#807060',
      'line-width': 1.2,
    },
  },

  // ── 19. Dam / barrier ─────────────────────────────────────────────────────
  {
    id: 'enc-dam',
    type: 'line',
    source: 's57',
    'source-layer': 'dam',
    minzoom: 10,
    paint: {
      'line-color': S52.CHBLK,
      'line-width': 2.0,
    },
  },

  // ── 20. Spot soundings (S-57: SOUNDG) ─────────────────────────────────────
  // Only shown at high zoom to avoid clutter. Label colour follows S-52:
  // black for depths ≥10m, blue for depths <10m.
  {
    id: 'enc-sounding',
    type: 'symbol',
    source: 's57',
    'source-layer': 'sounding',
    minzoom: 13,
    layout: {
      'text-field': [
        'number-format',
        ['get', 'dybde'],
        { 'max-fraction-digits': 1 },
      ],
      'text-font': ['Open Sans Regular'],
      'text-size': [
        'interpolate', ['linear'], ['zoom'],
        13, 8,
        16, 11,
      ],
      'text-allow-overlap': false,
      'text-padding': 3,
    },
    paint: {
      'text-color': [
        'case',
        ['<', ['coalesce', ['get', 'dybde'], 99], 10],
        S52.SNDG1,  // shallow soundings in blue
        S52.SNDG2,  // deep soundings in black
      ],
      'text-halo-color': '#ffffff',
      'text-halo-width': 1.0,
    },
  },

  // ── 21. Rock symbols (S-57: UWTROC / WRECKS) ─────────────────────────────
  {
    id: 'enc-rock',
    type: 'circle',
    source: 's57',
    'source-layer': 'rock',
    minzoom: 11,
    paint: {
      'circle-radius': [
        'interpolate', ['linear'], ['zoom'],
        11, 2,
        16, 5,
      ],
      'circle-color': S52.ROCKS,
      'circle-stroke-color': '#ffffff',
      'circle-stroke-width': 0.5,
    },
  },

  // ── 22. Rock cross symbol (higher zoom) ───────────────────────────────────
  {
    id: 'enc-rock-symbol',
    type: 'symbol',
    source: 's57',
    'source-layer': 'rock',
    minzoom: 14,
    layout: {
      'text-field': '✱',
      'text-font': ['Open Sans Regular'],
      'text-size': 12,
      'text-allow-overlap': true,
      'text-ignore-placement': true,
    },
    paint: {
      'text-color': S52.ROCKS,
    },
  },

  // ── 23. Shoal symbols (S-57: OBSTRN) ──────────────────────────────────────
  {
    id: 'enc-shoal',
    type: 'symbol',
    source: 's57',
    'source-layer': 'shoal',
    minzoom: 13,
    layout: {
      'text-field': [
        'number-format',
        ['get', 'dybde'],
        { 'max-fraction-digits': 1 },
      ],
      'text-font': ['Open Sans Regular'],
      'text-size': 8,
      'text-allow-overlap': false,
      'text-padding': 5,
    },
    paint: {
      'text-color': [
        'case',
        ['<', ['coalesce', ['get', 'dybde'], 99], 0],
        S52.DANGR,   // above-water shoals in red
        S52.SNDG1,   // submerged shoals in blue
      ],
      'text-halo-color': '#ffffff',
      'text-halo-width': 0.8,
    },
  },
];

// Union all S-57 layers for visibility toggling
export const ALL_S57_LAYERS: LayerSpecification[] = [...s57Layers];

// ─────────────────────────────────────────────────────────────────────────────
// GeoJSON source placeholders for dynamic overlays
// ─────────────────────────────────────────────────────────────────────────────

export const colregsSectorsSource: SourceSpecification = {
  type: 'geojson',
  data: { type: 'FeatureCollection', features: [] },
};

export const imazuGeometrySource: SourceSpecification = {
  type: 'geojson',
  data: { type: 'FeatureCollection', features: [] },
};

// Vessel sources (GeoJSON, updated from telemetryStore).
//
// IMPORTANT: MapLibre v4 GeoJSON sources created with an empty FeatureCollection
// do not always rebuild their tile index on later `setData` calls for Point
// geometries — the layer stays invisible. The workaround is to seed each source
// with a minimal placeholder feature so the worker initialises a real tile.
// The placeholder is replaced on the first telemetry tick.
const TRONDHEIM: [number, number] = [10.4, 63.4];

const placeholderPoint = (props: Record<string, any> = {}) => ({
  type: 'FeatureCollection' as const,
  features: [{
    type: 'Feature' as const,
    geometry: { type: 'Point' as const, coordinates: TRONDHEIM },
    properties: { _placeholder: true, ...props },
  }],
});

const placeholderLine = () => ({
  type: 'FeatureCollection' as const,
  features: [{
    type: 'Feature' as const,
    geometry: {
      type: 'LineString' as const,
      coordinates: [TRONDHEIM, [TRONDHEIM[0] + 0.001, TRONDHEIM[1] + 0.001]],
    },
    properties: { _placeholder: true },
  }],
});

export const ownShipSource: SourceSpecification = {
  type: 'geojson',
  data: placeholderPoint({ heading_deg: 0 }) as any,
};
export const targetsSource: SourceSpecification = {
  type: 'geojson',
  data: placeholderPoint({ heading_deg: 0, mmsi: '' }) as any,
};
export const cpaRingSource: SourceSpecification = {
  type: 'geojson',
  data: placeholderLine() as any,
};

export const ownShipLayer: LayerSpecification = {
  id: 'own-ship',
  type: 'circle',
  source: 'own-ship',
  paint: {
    'circle-radius': 13,
    'circle-color': '#2dd4bf',
    'circle-stroke-color': '#0b1320',
    'circle-stroke-width': 3,
  },
};

// Demotiles serves the Open Sans glyphs at the /font/{fontstack}/ URL pattern
const TEXT_FONT = ['Open Sans Regular'];

export const ownShipHeadingLayer: LayerSpecification = {
  id: 'own-ship-heading',
  type: 'symbol',
  source: 'own-ship',
  layout: {
    'text-field': '▲',
    'text-font': TEXT_FONT,
    'text-size': 22,
    // Rotate the marker by the ship's heading (degrees, clockwise from north)
    'text-rotate': ['get', 'heading_deg'],
    'text-rotation-alignment': 'map',
    'text-allow-overlap': true,
    'text-ignore-placement': true,
  },
  paint: {
    'text-color': '#2dd4bf',
    'text-halo-color': '#0b1320',
    'text-halo-width': 1.5,
  },
};

export const targetsLayer: LayerSpecification = {
  id: 'targets',
  type: 'circle',
  source: 'targets',
  paint: {
    'circle-radius': 11,
    'circle-color': '#fbbf24',
    'circle-stroke-color': '#0b1320',
    'circle-stroke-width': 3,
  },
};

export const targetsHeadingLayer: LayerSpecification = {
  id: 'targets-heading',
  type: 'symbol',
  source: 'targets',
  layout: {
    'text-field': '▲',
    'text-font': TEXT_FONT,
    'text-size': 18,
    'text-rotate': ['get', 'heading_deg'],
    'text-rotation-alignment': 'map',
    'text-allow-overlap': true,
    'text-ignore-placement': true,
  },
  paint: {
    'text-color': '#fbbf24',
    'text-halo-color': '#0b1320',
    'text-halo-width': 1.5,
  },
};

export const targetsLabelLayer: LayerSpecification = {
  id: 'targets-label',
  type: 'symbol',
  source: 'targets',
  layout: {
    'text-field': ['get', 'mmsi'],
    'text-font': TEXT_FONT,
    'text-size': 10,
    'text-offset': [0, 1.4],
    'text-anchor': 'top',
    'text-allow-overlap': true,
  },
  paint: {
    'text-color': '#fbbf24',
    'text-halo-color': '#0b1320',
    'text-halo-width': 1.2,
  },
};

export const cpaRingLayer: LayerSpecification = {
  id: 'cpa-ring',
  type: 'line',
  source: 'cpa-ring',
  paint: {
    'line-color': '#f87171',
    'line-width': 1.5,
    'line-dasharray': [4, 3],
  },
};
