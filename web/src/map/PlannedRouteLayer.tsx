import React, { useEffect, useRef } from 'react';
import maplibregl from 'maplibre-gl';
import { computeRangeNm, computeBearing } from '../screens/shared/navMath';

interface Waypoint { lat: number; lon: number; }
export interface WaypointRouteMetrics {
  totalDistanceNm: number;
  waypointNumber: number;
  waypointCount: number;
  nextSegmentNm: number | null;
  turnAngleDeg: number | null;
}
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

function segmentDistanceNm(wps: Waypoint[], startIdx: number): number | null {
  const a = wps[startIdx];
  const b = wps[startIdx + 1];
  if (!a || !b) return null;
  return computeRangeNm(a.lat, a.lon, b.lat, b.lon);
}

function bearingDeltaDeg(previous: number, next: number): number {
  const signed = ((next - previous + 540) % 360) - 180;
  return Math.abs(signed);
}

export function buildWaypointRouteMetrics(wps: Waypoint[], waypointIndex: number): WaypointRouteMetrics {
  const totalDistanceNm = wps.reduce((total, _wp, idx) => (
    total + (segmentDistanceNm(wps, idx) ?? 0)
  ), 0);
  const previous = wps[waypointIndex - 1];
  const current = wps[waypointIndex];
  const next = wps[waypointIndex + 1];
  const turnAngleDeg = previous && current && next
    ? bearingDeltaDeg(
      computeBearing(previous.lat, previous.lon, current.lat, current.lon),
      computeBearing(current.lat, current.lon, next.lat, next.lon)
    )
    : null;

  return {
    totalDistanceNm,
    waypointNumber: waypointIndex + 1,
    waypointCount: wps.length,
    nextSegmentNm: segmentDistanceNm(wps, waypointIndex),
    turnAngleDeg,
  };
}

function formatMetric(value: number | null, unit: string, digits = 1): string {
  return value === null ? '—' : `${value.toFixed(digits)} ${unit}`;
}

function buildWaypointPopupHtml(metrics: WaypointRouteMetrics): string {
  const rows = [
    ['总航线长度', formatMetric(metrics.totalDistanceNm, 'nm')],
    ['当前航点', `WP ${metrics.waypointNumber} / ${metrics.waypointCount}`],
    ['下一航段', formatMetric(metrics.nextSegmentNm, 'nm')],
    ['偏航角', formatMetric(metrics.turnAngleDeg, '°')],
  ];

  return `
    <div style="min-width: 190px; padding: 10px 12px; background: rgba(10, 15, 24, 0.95); color: #e5f3ff; border: 1px solid rgba(91,192,190,0.48); border-radius: 8px; font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace; box-shadow: 0 12px 30px rgba(0,0,0,0.35);">
      <div style="font-size: 12px; font-weight: 800; color: #5bc0be; margin-bottom: 8px; letter-spacing: 0.06em;">航点信息</div>
      ${rows.map(([label, value]) => `
        <div style="display: flex; justify-content: space-between; gap: 14px; font-size: 11px; line-height: 1.8;">
          <span style="color: #8fa3b8;">${label}</span>
          <span style="color: #e5f3ff; font-weight: 700;">${value}</span>
        </div>
      `).join('')}
    </div>
  `;
}

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
      properties: { idx: `WP${i + 1}`, waypointIndex: i },
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
  const popupRef = useRef<maplibregl.Popup | null>(null);
  useEffect(() => {
    let retryTimer: ReturnType<typeof setTimeout> | null = null;
    let cleanupMap: (() => void) | null = null;
    let cancelled = false;

    const attachToMap = (map: maplibregl.Map) => {
      const opacity = visible && waypoints.length >= 2 ? 1 : 0;
      let handlersAdded = false;
      const showWaypointPopup = (event: maplibregl.MapMouseEvent & { features?: maplibregl.MapGeoJSONFeature[] }) => {
        const feature = event.features?.[0];
        const waypointIndex = Number(feature?.properties?.waypointIndex);
        if (!Number.isInteger(waypointIndex) || waypointIndex < 0 || waypointIndex >= waypoints.length) return;
        const waypoint = waypoints[waypointIndex];
        const metrics = buildWaypointRouteMetrics(waypoints, waypointIndex);
        popupRef.current?.remove();
        popupRef.current = new maplibregl.Popup({
          closeButton: true,
          closeOnClick: false,
          offset: 14,
          className: 'planned-route-popup',
        })
          .setLngLat([waypoint.lon, waypoint.lat])
          .setHTML(buildWaypointPopupHtml(metrics))
          .addTo(map);
      };
      const onMouseEnter = () => {
        map.getCanvas().style.cursor = 'pointer';
      };
      const onMouseLeave = () => {
        map.getCanvas().style.cursor = '';
      };
      const addHandlers = () => {
        if (handlersAdded || !map.getLayer(WP_LYR)) return;
        map.on('click', WP_LYR, showWaypointPopup);
        map.on('mouseenter', WP_LYR, onMouseEnter);
        map.on('mouseleave', WP_LYR, onMouseLeave);
        handlersAdded = true;
      };

      function setup() {
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
            paint: { 'circle-radius': 5, 'circle-color': '#0369a1',
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
        addHandlers();
      }

      if (!map.isStyleLoaded()) map.once('style.load', setup);
      else setup();

      return () => {
        map.off('style.load', setup);
        if (handlersAdded) {
          map.off('click', WP_LYR, showWaypointPopup);
          map.off('mouseenter', WP_LYR, onMouseEnter);
          map.off('mouseleave', WP_LYR, onMouseLeave);
        }
        popupRef.current?.remove();
        popupRef.current = null;
      };
    };

    const waitForMap = () => {
      const map = mapRef.current;
      if (map) {
        cleanupMap = attachToMap(map);
        return;
      }
      if (!cancelled) retryTimer = setTimeout(waitForMap, 50);
    };
    waitForMap();

    return () => {
      cancelled = true;
      if (retryTimer) clearTimeout(retryTimer);
      cleanupMap?.();
    };
  }, [mapRef, waypoints, visible]);
  return null;
});
