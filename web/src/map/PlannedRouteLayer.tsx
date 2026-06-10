import React, { useEffect, useRef } from 'react';
import type maplibregl from 'maplibre-gl';
import { computeRangeNm, computeBearing } from '../screens/shared/navMath';

interface Waypoint { lat: number; lon: number; }
interface Props {
  mapRef: React.MutableRefObject<maplibregl.Map | null>;
  waypoints: Waypoint[];
  visible: boolean;
}
const LINE_SRC = 'planned-route-src';
const LINE_LYR = 'planned-route-line';
const WP_SRC = 'planned-route-wp-src';
const WP_LYR = 'planned-route-wp-circle';
const LBL_SRC = 'planned-route-lbl-src';
const LBL_LYR = 'planned-route-lbl-symbol';
const MAX_SEGMENT_LABELS = 24;

function buildLine(wps: Waypoint[]) {
  return {
    type: 'FeatureCollection',
    features: [{
      type: 'Feature',
      geometry: { type: 'LineString', coordinates: wps.map((w) => [w.lon, w.lat]) },
      properties: {},
    }],
  } as const;
}
function buildPoints(wps: Waypoint[]) {
  return {
    type: 'FeatureCollection',
    features: wps.map((w, i) => ({
      type: 'Feature',
      geometry: { type: 'Point', coordinates: [w.lon, w.lat] },
      properties: { idx: `WP${i}` },
    })),
  } as const;
}
function buildLabels(wps: Waypoint[]) {
  const feats = [];
  const segmentCount = Math.max(0, wps.length - 1);
  const stride = Math.max(1, Math.ceil(segmentCount / MAX_SEGMENT_LABELS));
  for (let i = 0; i < wps.length - 1; i++) {
    if (i % stride !== 0 && i !== wps.length - 2) continue;
    const a = wps[i], b = wps[i + 1];
    const nm = computeRangeNm(a.lat, a.lon, b.lat, b.lon);
    const brg = computeBearing(a.lat, a.lon, b.lat, b.lon);
    feats.push({
      type: 'Feature',
      geometry: { type: 'Point', coordinates: [(a.lon + b.lon) / 2, (a.lat + b.lat) / 2] },
      properties: { label: `${nm.toFixed(1)} NM · ${brg.toFixed(0)}°` },
    });
  }
  return { type: 'FeatureCollection', features: feats } as const;
}

export const PlannedRouteLayer: React.FC<Props> = React.memo(({ mapRef, waypoints, visible }) => {
  const added = useRef(false);
  useEffect(() => {
    const map = mapRef.current;
    if (!map) return;
    const opacity = visible && waypoints.length >= 2 ? 1 : 0;
    function setup() {
      if (!map) return;
      const line = buildLine(waypoints);
      const pts = buildPoints(waypoints);
      const lbls = buildLabels(waypoints);
      if (!added.current) {
        map.addSource(LINE_SRC, { type: 'geojson', data: line as any });
        map.addLayer({
          id: LINE_LYR, type: 'line', source: LINE_SRC,
          layout: { 'line-cap': 'round', 'line-join': 'round' },
          paint: { 'line-color': '#38bdf8', 'line-width': 2,
                   'line-dasharray': [3, 2], 'line-opacity': opacity },
        });
        map.addSource(WP_SRC, { type: 'geojson', data: pts as any });
        map.addLayer({
          id: WP_LYR, type: 'circle', source: WP_SRC,
          paint: { 'circle-radius': 4, 'circle-color': '#0369a1',
                   'circle-stroke-width': 1, 'circle-stroke-color': '#e0f2fe',
                   'circle-opacity': opacity, 'circle-stroke-opacity': opacity },
        });
        map.addSource(LBL_SRC, { type: 'geojson', data: lbls as any });
        map.addLayer({
          id: LBL_LYR, type: 'symbol', source: LBL_SRC,
          layout: { 'text-field': ['get', 'label'], 'text-size': 11,
                    'text-offset': [0, -0.8], 'text-allow-overlap': false },
          paint: { 'text-color': '#0369a1', 'text-halo-color': '#f0f9ff',
                   'text-halo-width': 1, 'text-opacity': opacity },
        });
        added.current = true;
      } else {
        (map.getSource(LINE_SRC) as any)?.setData(line);
        (map.getSource(WP_SRC) as any)?.setData(pts);
        (map.getSource(LBL_SRC) as any)?.setData(lbls);
        for (const id of [LINE_LYR, WP_LYR, LBL_LYR]) {
          const prop = id === LINE_LYR ? 'line-opacity'
            : id === WP_LYR ? 'circle-opacity' : 'text-opacity';
          if (map.getLayer(id)) map.setPaintProperty(id, prop, opacity);
        }
      }
    }
    if (!map.isStyleLoaded()) map.once('style.load', setup);
    else setup();
  }, [mapRef, waypoints, visible]);
  return null;
});
