import type { LayerSpecification, SourceSpecification } from 'maplibre-gl';

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
    paint: {
      'fill-color': '#b8d4f0',
      'fill-opacity': 0.4,
    },
  },
  {
    id: 's57-land',
    type: 'fill',
    source: 's57',
    'source-layer': 'land_area',
    paint: {
      'fill-color': '#f5f0e0',
    },
  },
  {
    id: 's57-coastline',
    type: 'line',
    source: 's57',
    'source-layer': 'coastline',
    paint: {
      'line-color': '#333333',
      'line-width': 1.5,
    },
  },
];
