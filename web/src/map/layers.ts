import type { LayerSpecification, SourceSpecification } from 'maplibre-gl';

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

// S-57 vector tile source — kept for when the MVT pipeline lands (Phase 2 D2.5)
export const S57_TILE_URL = '/tiles/s57/{z}/{x}/{y}.pbf';
export const s57Source: SourceSpecification = {
  type: 'vector',
  tiles: [S57_TILE_URL],
  minzoom: 8,
  maxzoom: 18,
};
export const s57Layers: LayerSpecification[] = [
  {
    id: 's57-depth-area',
    type: 'fill',
    source: 's57',
    'source-layer': 'depth_area',
    paint: { 'fill-color': '#b8d4f0', 'fill-opacity': 0.4 },
  },
  {
    id: 's57-land',
    type: 'fill',
    source: 's57',
    'source-layer': 'land_area',
    paint: { 'fill-color': '#f5f0e0' },
  },
  {
    id: 's57-coastline',
    type: 'line',
    source: 's57',
    'source-layer': 'coastline',
    paint: { 'line-color': '#333333', 'line-width': 1.5 },
  },
];

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
