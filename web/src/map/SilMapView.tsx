import { useEffect, useRef, useState } from 'react';
import maplibregl from 'maplibre-gl';
import 'maplibre-gl/dist/maplibre-gl.css';
import { osmSource, osmLayer, ALL_S57_LAYERS } from './layers';
import { useTelemetryStore, useMapStore, useUIStore, useControlStore } from '../store';
import { MAP_MAX_ZOOM } from '../store/mapStore';
import { useMapPersistence } from '../hooks/useMapPersistence';
import type { TargetVesselState } from '../types/sil/target_vessel_state';
import { checkGroundingRisk, predictedPath } from '../utils/groundingDetect';
import type { OwnShipPosition } from '../utils/groundingDetect';
import { booleanIntersects } from '@turf/turf';
import { Ruler, Trash2 } from 'lucide-react';

interface SilMapViewProps {
  followOwnShip?: boolean;
  viewMode?: 'captain' | 'engineer' | 'roc' | 'god';
  /** Fraction of viewport to offset own-ship towards. Captain: [0.5, 0.7] (bottom 30%) */
  viewportOffset?: [number, number];
  /** Optional preview data for Scenario Builder mode */
  previewData?: {
    ownShip?: { lat: number; lon: number; heading: number; sog?: number; cog?: number };
    targets?: Array<{ id: string; lat: number; lon: number; heading: number; sog?: number; cog?: number }>;
    encRegion?: string;
  };
  /** ENC Region tileset name (e.g. 'trondelag', 'coastal_archipelago') */
  encRegion?: string;
  /** Callback for map clicks (useful for setting positions in Builder) */
  onMapClick?: (lon: number, lat: number) => void;
  /** Substrate layer type: 'enc' (vector), 'sat' (satellite raster), 'osm' (standard raster) */
  substrate?: 'enc' | 'sat' | 'osm';
  /** Optional geometry (Imazu circles, sectors, etc.) */
  geometry?: GeoJSON.FeatureCollection | null;

  // ── New props (Scheme B, Screen ① drag interaction) ──
  /** External mapRef for useMapInteraction hook to bind events */
  mapRef?: React.MutableRefObject<maplibregl.Map | null>;
}

import { ImazuGeometry } from './ImazuGeometry';
import { MapZoomControl } from './MapZoomControl';

const RAD = 180 / Math.PI;
const NM_TO_DEG = 1 / 60;

// ── geometry helpers ──────────────────────────────────────────────────────────

/** Project a point [lon,lat] by bearing (deg) and distance (nm). */
function project(lon: number, lat: number, bearingDeg: number, distNm: number): [number, number] {
  const d = distNm * NM_TO_DEG;
  const brRad = bearingDeg * (Math.PI / 180);
  const cosLat = Math.cos(lat * (Math.PI / 180)) || 1e-9;
  return [lon + (d * Math.sin(brRad)) / cosLat, lat + d * Math.cos(brRad)];
}

// Removed legacy cpa rings helpers

/** COG leader line — length depends on SOG (6-min projection). */
function cogLine(lon: number, lat: number, cogRad: number, sogMs: number): GeoJSON.Feature {
  const cogDeg = cogRad * RAD;
  const distNm = (sogMs * 360) / 1852; // 6 min @ sogMs m/s → nm
  const finalDist = sogMs < 0.05 ? 0 : distNm;
  const [lon2, lat2] = project(lon, lat, cogDeg, finalDist);
  return {
    type: 'Feature',
    geometry: { type: 'LineString', coordinates: [[lon, lat], [lon2, lat2]] },
    properties: { cogDeg },
  };
}

interface CogPathData {
  line: GeoJSON.Feature;
  ticks: GeoJSON.Feature[];
}

function calculateCogPath(
  lon: number,
  lat: number,
  cogRad: number,
  sogMs: number
): CogPathData {
  const cogDeg = cogRad * RAD;
  
  // Calculate distances for 1m, 3m, 6m (SOG is in m/s)
  const distNm1 = (sogMs * 60) / 1852;
  const distNm3 = (sogMs * 180) / 1852;
  const distNm6 = (sogMs * 360) / 1852;
  
  const finalDist = sogMs < 0.05 ? 0 : distNm6;
  const [lonEnd, latEnd] = project(lon, lat, cogDeg, finalDist);
  
  const line: GeoJSON.Feature = {
    type: 'Feature',
    geometry: { type: 'LineString', coordinates: [[lon, lat], [lonEnd, latEnd]] },
    properties: { cogDeg },
  };
  
  const ticks: GeoJSON.Feature[] = [];
  if (sogMs >= 0.05) {
    const times = [
      { time: 1, dist: distNm1, label: '1m' },
      { time: 3, dist: distNm3, label: '3m' },
      { time: 6, dist: distNm6, label: '6m' },
    ];
    
    for (const item of times) {
      const [ptLon, ptLat] = project(lon, lat, cogDeg, item.dist);
      ticks.push({
        type: 'Feature',
        geometry: { type: 'Point', coordinates: [ptLon, ptLat] },
        properties: { label: item.label },
      });
    }
  }
  
  return { line, ticks };
}

// ── vessel DOM markers (not GeoJSON layers — avoids MapLibre v4 tile-index bug) ──

function makeVesselEl(color: string, size = 28, ownship = false): HTMLDivElement {
  const el = document.createElement('div');
  el.style.cssText = `width:${size}px;height:${size}px;pointer-events:auto;cursor:pointer;transform-origin:50% 50%;position:relative;`;
  const strokeW = ownship ? 2.5 : 1.8;
  el.innerHTML = `
    <svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg" width="${size}" height="${size}" style="pointer-events:none; display:block;">
      <circle cx="12" cy="12" r="${ownship ? 7 : 5.5}" fill="${color}" stroke="#0b1320" stroke-width="${strokeW}" opacity="0.92"/>
      <polygon points="12,1 16.5,10 12,8 7.5,10" fill="${color}" stroke="#0b1320" stroke-width="${strokeW}"/>
    </svg>`;
  return el;
}

function updatePlaqueDOM(
  container: HTMLDivElement,
  isSelected: boolean,
  vesselId: string,
  label: string,
  hdg: string,
  rud: string,
  sog: string,
  thr: string,
  headingDeg: number,
  markerSize: number   // DOM element size in px: 30 for own ship, 24 for targets
) {
  const isOs = vesselId === 'ownship';
  const primaryColor  = isOs ? '#2dd4bf' : '#fbbf24';
  const strokeColor   = isOs ? 'rgba(45, 212, 191, 0.85)' : 'rgba(251, 191, 36, 0.85)';
  const borderColor   = isOs ? 'rgba(45, 212, 191, 0.75)' : 'rgba(251, 191, 36, 0.75)';
  const glowColor     = isOs ? 'rgba(45,212,191,0.25)' : 'rgba(251,191,36,0.25)';

  // ── Geometry constants ─────────────────────────────────────────────────────
  const PLAQUE_LEFT   = 26;   // plaque CSS left in container space (px)
  const PLAQUE_TOP    = -95;  // plaque CSS top  in container space (px)
  const PLAQUE_H      = 78;   // approximate plaque height (px) — used to anchor leader line
  const cx = markerSize / 2;  // ship centre x in container space
  const cy = markerSize / 2;  // ship centre y in container space

  // Ship centre in *plaque local* space (before any rotation):
  //   plaque local (0,0) == container point (PLAQUE_LEFT, PLAQUE_TOP)
  const shipX = cx - PLAQUE_LEFT;  // e.g. -11 (own ship 30px) or -14 (target 24px)
  const shipY = cy - PLAQUE_TOP;   // e.g.  110 / 107

  // Setting transform-origin to this point means the counter-rotation pivots
  // around the ship centre → plaque stays at a FIXED screen position.
  const tOrigin = `${shipX}px ${shipY}px`;

  // Leader SVG: draws from ship centre (shipX, shipY) to plaque bottom-left (0, PLAQUE_H)
  // Position the SVG so its coordinate space contains both endpoints:
  const svgLeft   = shipX;            // left of SVG in plaque space  (negative → extends left)
  const svgTop    = PLAQUE_H;         // top  of SVG in plaque space  (at plaque bottom)
  const svgW      = Math.abs(shipX);  // SVG width  covers horizontal gap
  const svgH      = shipY - PLAQUE_H; // SVG height covers vertical  gap
  // In SVG local coords:  ship centre → (0, svgH),  plaque corner → (svgW, 0)
  const lx1 = 0,    ly1 = svgH; // ship centre (bottom-left of SVG)
  const lx2 = svgW, ly2 = 0;    // plaque bottom-left corner (top-right of SVG)

  // ── DOM update ─────────────────────────────────────────────────────────────
  let plaque = container.querySelector('.hud-plaque') as HTMLDivElement | null;

  if (!isSelected) {
    if (plaque) plaque.remove();
    return;
  }

  if (!plaque) {
    plaque = document.createElement('div');
    plaque.className = 'hud-plaque';
    plaque.style.cssText = `
      position: absolute;
      left: ${PLAQUE_LEFT}px;
      top: ${PLAQUE_TOP}px;
      z-index: 1000;
      background: rgba(7, 16, 27, 0.94);
      backdrop-filter: blur(4px);
      border: 1px solid ${borderColor};
      border-radius: 4px;
      padding: 5px 8px;
      width: 135px;
      box-shadow: 0 4px 15px rgba(0,0,0,0.6), 0 0 8px ${glowColor};
      font-family: monospace;
      pointer-events: auto;
      cursor: default;
    `;

    plaque.innerHTML = `
      <svg class="hud-leader-svg" style="position:absolute;
           left:${svgLeft}px; top:${svgTop}px;
           width:${svgW}px; height:${svgH}px;
           overflow:visible; pointer-events:none;">
        <line x1="${lx1}" y1="${ly1}" x2="${lx2}" y2="${ly2}"
              stroke="${strokeColor}" stroke-width="1.2" stroke-dasharray="3 3"/>
      </svg>
      <div class="plaque-content"></div>
    `;

    plaque.addEventListener('click',   (e) => e.stopPropagation());
    plaque.addEventListener('dblclick', (e) => e.stopPropagation());
    container.appendChild(plaque);
  }

  // Counter-rotate around the ship centre so the plaque stays upright and
  // at a fixed screen position regardless of vessel heading.
  plaque.style.transformOrigin = tOrigin;
  plaque.style.transform = `rotate(${-headingDeg}deg)`;

  const contentEl = plaque.querySelector('.plaque-content');
  if (contentEl) {
    const indicator   = isOs ? '●' : '▲';
    const displayName = isOs ? 'OWN SHIP' : `TS ${label}`;

    contentEl.innerHTML = `
      <div style="font-size:10px;font-weight:bold;color:${primaryColor};margin-bottom:4px;
                  display:flex;align-items:center;gap:4px;letter-spacing:0.05em;
                  text-transform:uppercase;">
        <span style="font-size:8px;">${indicator}</span> ${displayName}
      </div>
      <div style="display:grid;grid-template-columns:1fr 1fr;gap:4px 8px;
                  border-top:1px solid rgba(255,255,255,0.1);padding-top:4px;
                  text-align:left;text-transform:none;">
        <div>
          <div style="font-size:8px;color:#8A9AAD;line-height:1;margin-bottom:1px;">HDG</div>
          <div style="font-size:11px;color:#fff;font-weight:bold;line-height:1.1;">${hdg}°</div>
        </div>
        <div>
          <div style="font-size:8px;color:#8A9AAD;line-height:1;margin-bottom:1px;">RUD</div>
          <div style="font-size:11px;color:#fff;font-weight:bold;line-height:1.1;">${rud}</div>
        </div>
        <div>
          <div style="font-size:8px;color:#8A9AAD;line-height:1;margin-bottom:1px;">SOG</div>
          <div style="font-size:11px;color:#fff;font-weight:bold;line-height:1.1;">${sog}<span style="font-size:8px;font-weight:normal;color:#8A9AAD;"> kn</span></div>
        </div>
        <div>
          <div style="font-size:8px;color:#8A9AAD;line-height:1;margin-bottom:1px;">THR</div>
          <div style="font-size:11px;color:#fff;font-weight:bold;line-height:1.1;">${thr}</div>
        </div>
      </div>
    `;
  }
}

// ── wind / current arrow overlay ──────────────────────────────────────────────

function makeWindEl(dirDeg: number, speedMps: number): HTMLDivElement {
  const el = document.createElement('div');
  const kts = (speedMps * 1.944).toFixed(1);
  el.style.cssText = 'pointer-events:none;text-align:center;font-family:monospace;font-size:11px;color:#60a5fa;line-height:1.2;';
  el.innerHTML =
    `<div style="font-size:18px;transform:rotate(${dirDeg}deg);display:inline-block">↑</div>` +
    `<div>${kts} kn</div>`;
  return el;
}

function calculateDistanceNm(lon1: number, lat1: number, lon2: number, lat2: number): number {
  const R = 6371000; // Earth radius in meters
  const dLat = (lat2 - lat1) * Math.PI / 180;
  const dLon = (lon2 - lon1) * Math.PI / 180;
  const a =
    Math.sin(dLat / 2) * Math.sin(dLat / 2) +
    Math.cos(lat1 * Math.PI / 180) * Math.cos(lat2 * Math.PI / 180) *
    Math.sin(dLon / 2) * Math.sin(dLon / 2);
  const c = 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));
  return (R * c) / 1852; // convert meters to nautical miles
}

function calculateBearingDeg(lon1: number, lat1: number, lon2: number, lat2: number): number {
  const lat1Rad = lat1 * Math.PI / 180;
  const lat2Rad = lat2 * Math.PI / 180;
  const dLonRad = (lon2 - lon1) * Math.PI / 180;
  const y = Math.sin(dLonRad) * Math.cos(lat2Rad);
  const x = Math.cos(lat1Rad) * Math.sin(lat2Rad) - Math.sin(lat1Rad) * Math.cos(lat2Rad) * Math.cos(dLonRad);
  const theta = Math.atan2(y, x);
  return (theta * 180 / Math.PI + 360) % 360;
}

// ─────────────────────────────────────────────────────────────────────────────
export function SilMapView({ 
  followOwnShip = true, 
  viewMode = 'captain', 
  viewportOffset = [0.5, 0.5],
  previewData,
  encRegion,
  onMapClick,
  substrate = 'enc',
  geometry,
  mapRef: externalMapRef,
}: SilMapViewProps) {
  const mapContainer = useRef<HTMLDivElement>(null);
  const mapRef       = useRef<maplibregl.Map | null>(null);
  const styleReady   = useRef(false);
  const lastPanAt    = useRef(0);
  const firstFit     = useRef(false);
  const onMapClickRef = useRef(onMapClick);
  const viewportFromStore = useMapStore((s) => s.viewport);
  const setViewport       = useMapStore((s) => s.setViewport);

  // Markers
  const ownMarker    = useRef<maplibregl.Marker | null>(null);
  const windMarker   = useRef<maplibregl.Marker | null>(null);
  const tgtMarkers   = useRef<Map<string, maplibregl.Marker>>(new Map());

  const [status, setStatus] = useState<'init' | 'ready' | 'no-webgl'>('init');
  const [mousePos, setMousePos] = useState<{ lng: number, lat: number } | null>(null);
  const [mapCenter, setMapCenter] = useState<{ lng: number, lat: number } | null>(null);
  const [mouseDepth, setMouseDepth] = useState<{ type: 'depth' | 'land' | 'drying'; min?: number; max?: number } | null>(null);

  // Measurement ruler states
  const [measurementMode, setMeasurementMode] = useState<'none' | 'vessel' | 'freeform'>('none');
  const [rulerLines, setRulerLines] = useState<{
    id: string;
    start: [number, number];
    end: [number, number];
    startSnapVesselId?: string;
    endSnapVesselId?: string;
    label: string;
  }[]>([]);
  const [activeLine, setActiveLine] = useState<{
    start: [number, number];
    current: [number, number];
    startSnapVesselId?: string;
    currentSnapVesselId?: string;
    label: string;
  } | null>(null);
  const [contextMenu, setContextMenu] = useState<{ x: number; y: number; lng: number; lat: number } | null>(null);

  // Keep measurementMode in a ref to avoid stale closures in marker click listeners
  const measurementModeRef = useRef(measurementMode);
  useEffect(() => {
    measurementModeRef.current = measurementMode;
  }, [measurementMode]);

  // Keep click handler ref in sync without re-initializing map
  useEffect(() => { onMapClickRef.current = onMapClick; }, [onMapClick]);

  // Cross-screen viewport persistence
  useMapPersistence(mapRef, viewMode === 'god' ? 'god' : 'captain');

  // Store selectors (memoised slices avoid 50 Hz whole-component re-renders)
  const ownShipFromStore  = useTelemetryStore((s) => s.ownShip);
  const targetsFromStore  = useTelemetryStore((s) => s.targets);
  const env      = useTelemetryStore((s) => s.environment);
  const trail    = useTelemetryStore((s) => s.ownShipTrail);
  const targetTrails = useTelemetryStore((s) => s.targetTrails);
  const selectedVesselId = useUIStore((s) => s.selectedVesselId);
  const setSelectedVesselId = useUIStore((s) => s.setSelectedVesselId);
  const simRate = useControlStore((s) => s.simRate);

  // Use preview data if provided, otherwise use store
  const ownShip = previewData?.ownShip ? {
    pose: { lat: previewData.ownShip.lat, lon: previewData.ownShip.lon, heading: previewData.ownShip.heading / RAD },
    kinematics: { sog: (previewData.ownShip.sog || 0) / 1.94384, cog: (previewData.ownShip.cog || previewData.ownShip.heading) / RAD, rot: 0 }
  } : ownShipFromStore;

  const targets = (previewData?.targets ? previewData.targets.map(t => ({
    mmsi: t.id,
    pose: { lat: t.lat, lon: t.lon, heading: t.heading / RAD },
    kinematics: { sog: (t.sog || 0) / 1.94384, cog: (t.cog || t.heading) / RAD, rot: 0 }
  })) : targetsFromStore) || [];

  // ── Map initialisation ──────────────────────────────────────────────────────
  useEffect(() => {
    if (!mapContainer.current || mapRef.current) return;
    let map: maplibregl.Map;
    try {
      map = new maplibregl.Map({
        container: mapContainer.current,
        attributionControl: false,
        style: {
          version: 8,
          glyphs: 'https://demotiles.maplibre.org/font/{fontstack}/{range}.pbf',
          sources: {
            osm: osmSource as any,
            s57: {
              type: 'vector',
              tiles: [
                typeof window !== 'undefined'
                  ? `${window.location.origin}/mvt/${encRegion || previewData?.encRegion || 'trondelag'}/{z}/{x}/{y}?v=2`
                  : `/mvt/${encRegion || previewData?.encRegion || 'trondelag'}/{z}/{x}/{y}?v=2`
              ],
              minzoom: 0,
              maxzoom: 16,
            },
            satellite: {
              type: 'raster',
              tiles: ['https://services.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}'],
              tileSize: 256,
              attribution: 'Esri, Maxar'
            },
            openseamap: {
              type: 'raster' as const,
              tiles: ['https://tiles.openseamap.org/seamark/{z}/{x}/{y}.png'],
              tileSize: 256,
              attribution: '© OpenSeaMap contributors',
              maxzoom: 18,
            },
          },
          layers: [
            {
              id: 'solid-background',
              type: 'background',
              paint: { 'background-color': substrate === 'enc' ? '#0369a1' : '#0f172a' }
            },
            {
              id: 'satellite-base',
              type: 'raster',
              source: 'satellite',
              layout: { visibility: substrate === 'sat' ? 'visible' : 'none' }
            },
            {
              ...osmLayer as any,
              layout: { visibility: substrate === 'osm' ? 'visible' : 'none' }
            },
            {
              id: 'openseamap',
              source: 'openseamap',
              type: 'raster' as const,
              paint: {
                'raster-opacity': 0.7,
                'raster-fade-duration': 200,
              },
              layout: {
                visibility: 'none',
              },
            } as maplibregl.RasterLayerSpecification,
            ...ALL_S57_LAYERS.map((l) => ({
              ...l as any,
              layout: { ...((l as any).layout || {}), visibility: substrate === 'enc' ? 'visible' : 'none' }
            })),
          ],
        },
        center: viewportFromStore.center,
        zoom: Math.min(viewportFromStore.zoom, MAP_MAX_ZOOM),
        maxZoom: MAP_MAX_ZOOM,
      });
    } catch {
      setStatus('no-webgl');
      return;
    }

    map.on('error', (e: any) => {
      const msg = String(e?.error?.message ?? '');
      // Suppress expected tile-fetch failures (martin offline, glyphs CDN, etc.)
      if (/pbf|s57|enc|Failed to fetch|tile|glyphs|3000|mvt|source-layer/i.test(msg)) return;
      console.warn('[SilMapView]', msg);
    });

    map.on('load', () => {
      // ── Trail ────────────────────────────────────────────────────────────
      map.addSource('trail', {
        type: 'geojson',
        data: { type: 'FeatureCollection', features: [] },
      });
      map.addLayer({
        id: 'trail-line',
        type: 'line',
        source: 'trail',
        paint: { 'line-color': '#2dd4bf', 'line-width': 1.5, 'line-opacity': 0.55,
                 'line-dasharray': [3, 2] },
      });

      // ── Target Trails ────────────────────────────────────────────────────
      map.addSource('tgt-trail', {
        type: 'geojson',
        data: { type: 'FeatureCollection', features: [] },
      });
      map.addLayer({
        id: 'tgt-trail-line',
        type: 'line',
        source: 'tgt-trail',
        paint: { 'line-color': '#fbbf24', 'line-width': 1.5, 'line-opacity': 0.55,
                 'line-dasharray': [3, 2] },
      });

      // ── Own-ship COG leader ──────────────────────────────────────────────
      map.addSource('own-cog', {
        type: 'geojson',
        data: { type: 'FeatureCollection', features: [] },
      });
      map.addLayer({
        id: 'own-cog-line',
        type: 'line',
        source: 'own-cog',
        paint: { 'line-color': '#2dd4bf', 'line-width': 2.5, 'line-opacity': 0.85 },
      });

      // ── Target COG leaders ───────────────────────────────────────────────
      map.addSource('tgt-cog', {
        type: 'geojson',
        data: { type: 'FeatureCollection', features: [] },
      });
      map.addLayer({
        id: 'tgt-cog-line',
        type: 'line',
        source: 'tgt-cog',
        paint: {
          'line-color': '#fbbf24',
          'line-width': 1.5,
          'line-opacity': 0.7,
          'line-dasharray': [4, 3], // Dashed line for predicted path
        },
      });

      // ── Target COG ticks ─────────────────────────────────────────────────
      map.addSource('tgt-cog-ticks', {
        type: 'geojson',
        data: { type: 'FeatureCollection', features: [] },
      });
      map.addLayer({
        id: 'tgt-cog-ticks-layer',
        type: 'circle',
        source: 'tgt-cog-ticks',
        paint: {
          'circle-color': '#fbbf24',
          'circle-radius': 3.5,
          'circle-stroke-color': '#0b1320',
          'circle-stroke-width': 1,
          'circle-opacity': 0.9,
        },
      });
      map.addLayer({
        id: 'tgt-cog-ticks-labels',
        type: 'symbol',
        source: 'tgt-cog-ticks',
        layout: {
          'text-field': ['get', 'label'],
          'text-font': ['Noto Sans Regular'],
          'text-size': 9,
          'text-offset': [0.8, -0.6],
          'text-anchor': 'left',
        },
        paint: {
          'text-color': '#fbbf24',
          'text-halo-color': '#070c13',
          'text-halo-width': 1.5,
        },
      });

      // ── Measurement Ruler ───────────────────────────────────────────────
      map.addSource('measurement-ruler', {
        type: 'geojson',
        data: { type: 'FeatureCollection', features: [] },
      });
      map.addLayer({
        id: 'measurement-ruler-line',
        type: 'line',
        source: 'measurement-ruler',
        filter: ['==', ['get', 'type'], 'line'],
        paint: {
          'line-color': '#2dd4bf',
          'line-width': 2.0,
          'line-dasharray': [4, 3],
          'line-opacity': 0.85
        }
      });
      map.addLayer({
        id: 'measurement-ruler-label',
        type: 'symbol',
        source: 'measurement-ruler',
        filter: ['==', ['get', 'type'], 'label'],
        layout: {
          'text-field': ['get', 'label'],
          'text-font': ['Noto Sans Regular'],
          'text-size': 10,
          'text-anchor': 'center',
          'text-offset': [0, -1.0],
          'text-allow-overlap': true,
          'text-ignore-placement': true
        },
        paint: {
          'text-color': '#2dd4bf',
          'text-halo-color': '#070c13',
          'text-halo-width': 1.5,
          'text-halo-blur': 0.5
        }
      });

      // cpa concentric rings removed in favor of asymmetric SafetyDomainLayer

      styleReady.current = true;
      setStatus('ready');
      setMapCenter(map.getCenter());
    });

    map.on('click', (e) => {
      if (onMapClickRef.current) {
        onMapClickRef.current(e.lngLat.lng, e.lngLat.lat);
      }
    });

    map.on('mousemove', (e) => {
      setMousePos({ lng: e.lngLat.lng, lat: e.lngLat.lat });
      
      const features = map.queryRenderedFeatures(e.point, {
        layers: ['enc-depth-area', 'enc-land', 'enc-drying-area']
      });
      if (features.length > 0) {
        const feat = features[0];
        const layerId = feat.layer?.id;
        if (layerId === 'enc-land') {
          setMouseDepth({ type: 'land' });
        } else if (layerId === 'enc-drying-area') {
          setMouseDepth({ type: 'drying' });
        } else {
          setMouseDepth({
            type: 'depth',
            min: feat.properties?.minimumsdybde,
            max: feat.properties?.maksimumsdybde
          });
        }
      } else {
        setMouseDepth(null);
      }
    });

    map.on('mouseleave', () => {
      setMousePos(null);
      setMouseDepth(null);
    });

    map.on('move', () => {
      const center = map.getCenter();
      setMapCenter(center);
      
      if (!mousePos) {
        const px = map.project(center);
        const features = map.queryRenderedFeatures(px, {
          layers: ['enc-depth-area', 'enc-land', 'enc-drying-area']
        });
        if (features.length > 0) {
          const feat = features[0];
          const layerId = feat.layer?.id;
          if (layerId === 'enc-land') {
            setMouseDepth({ type: 'land' });
          } else if (layerId === 'enc-drying-area') {
            setMouseDepth({ type: 'drying' });
          } else {
            setMouseDepth({
              type: 'depth',
              min: feat.properties?.minimumsdybde,
              max: feat.properties?.maksimumsdybde
            });
          }
        } else {
          setMouseDepth(null);
        }
      }
    });

    map.addControl(new maplibregl.ScaleControl({ maxWidth: 80, unit: 'nautical' }), 'bottom-left');
    mapRef.current = map;
    // Scheme B: sync to parent ref so useMapInteraction hook can bind events
    if (externalMapRef) {
      (externalMapRef as React.MutableRefObject<maplibregl.Map | null>).current = map;
    }
    if (typeof window !== 'undefined') { (window as any).__maplibre_map = map; }

    return () => {
      ownMarker.current?.remove(); ownMarker.current = null;
      windMarker.current?.remove(); windMarker.current = null;
      tgtMarkers.current.forEach((m) => m.remove());
      tgtMarkers.current.clear();
      try { mapRef.current?.remove(); } catch { /* noop */ }
      // Scheme B: clear external ref
      if (externalMapRef) {
        (externalMapRef as React.MutableRefObject<maplibregl.Map | null>).current = null;
      }
      mapRef.current = null;
      styleReady.current = false;
      firstFit.current = false;
    };
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // ── Resize Observer for Map Container ──────────────────────────────────────
  useEffect(() => {
    const container = mapContainer.current;
    if (!container || typeof ResizeObserver === 'undefined') return;

    const resizeObserver = new ResizeObserver(() => {
      if (mapRef.current) {
        mapRef.current.resize();
      }
    });

    resizeObserver.observe(container);

    return () => {
      resizeObserver.disconnect();
    };
  }, []);

  // ── Substrate update ────────────────────────────────────────────────────────
  useEffect(() => {
    const map = mapRef.current;
    if (!map || !styleReady.current) return;

    // Switch background colour: S-52 ocean for ENC, dark for satellite/OSM
    map.setPaintProperty('solid-background', 'background-color',
      substrate === 'enc' ? '#0369a1' : '#0f172a');

    map.setLayoutProperty('satellite-base', 'visibility', substrate === 'sat' ? 'visible' : 'none');
    map.setLayoutProperty('osm-base', 'visibility', substrate === 'osm' ? 'visible' : 'none');
    if (map.getLayer('openseamap')) {
      map.setLayoutProperty(
        'openseamap',
        'visibility',
        (substrate === 'osm' || substrate === 'enc') ? 'visible' : 'none',
      );
    }
    
    ALL_S57_LAYERS.forEach(l => {
      map.setLayoutProperty(l.id, 'visibility', substrate === 'enc' ? 'visible' : 'none');
    });
  }, [substrate]);

  // ── Dynamic ENC region update ──────────────────────────────────────────────
  useEffect(() => {
    const map = mapRef.current;
    if (!map || !styleReady.current) return;

    const currentRegion = encRegion || previewData?.encRegion || 'trondelag';
    const source = map.getSource('s57') as maplibregl.VectorTileSource | undefined;
    if (source) {
      const newUrl = typeof window !== 'undefined'
        ? `${window.location.origin}/mvt/${currentRegion}/{z}/{x}/{y}?v=2`
        : `/mvt/${currentRegion}/{z}/{x}/{y}?v=2`;

      if (source.tiles && source.tiles[0] !== newUrl) {
        console.log(`[SilMapView] Switching ENC region to: ${currentRegion}`);
        if (typeof (source as any).setTiles === 'function') {
          (source as any).setTiles([newUrl]);
          // Clear tile cache to force immediate reload
          ((map.style as any).sourceCaches?.['s57'] as any)?.clearSourceCaches();
          map.triggerRepaint();
        }
      }
    }
  }, [encRegion, previewData?.encRegion]);

  // ── Own-ship + trail + CPA rings update ────────────────────────────────────
  useEffect(() => {
    const map = mapRef.current;
    if (!map || !styleReady.current || !ownShip) return;
    const lat = ownShip.pose?.lat;
    const lon = ownShip.pose?.lon;
    const hdgDeg = (ownShip.pose?.heading ?? 0) * RAD;
    const cogRad = ownShip.kinematics?.cog ?? 0;
    const sogMs  = ownShip.kinematics?.sog ?? 0;
    if (typeof lat !== 'number' || typeof lon !== 'number') return;

    // Marker
    if (!ownMarker.current) {
      const el = makeVesselEl('#2dd4bf', 30, true);
      el.addEventListener('click', (e) => {
        if (measurementModeRef.current !== 'none') {
          return; // Allow clicks to bubble up to the map in measurement mode
        }
        e.stopPropagation();
        setSelectedVesselId('ownship');
      });
      ownMarker.current = new maplibregl.Marker({ element: el, rotationAlignment: 'map', anchor: 'center' })
        .setLngLat([lon, lat]).addTo(map);
    } else {
      ownMarker.current.setLngLat([lon, lat]);
    }
    ownMarker.current.setRotation(hdgDeg);

    // Plaque update
    const isSelected = selectedVesselId === 'ownship';
    const hdgVal = (Math.round(((ownShip.pose?.heading ?? 0) * 180 / Math.PI + 360) % 360) % 360).toString();
    const sogVal = ((ownShip.kinematics?.sog ?? 0) * 1.944).toFixed(1);

    // Compute actual Rudder in degrees and format with Port (L) / Starboard (R)
    const rudderRad = (ownShip as any).controlState?.rudderAngle ?? 0;
    const rudderDeg = rudderRad * 180 / Math.PI;
    let rudVal = '0.0°';
    if (rudderDeg > 0.1) {
      rudVal = `${rudderDeg.toFixed(1)}° R`;
    } else if (rudderDeg < -0.1) {
      rudVal = `${Math.abs(rudderDeg).toFixed(1)}° L`;
    }

    // Compute Thrust/Throttle level in Ahead 1/2/3, Astern 1/2/3, or STOP
    const throttle = (ownShip as any).controlState?.throttle ?? 0;
    let thrVal = 'STOP';
    if (throttle > 0) {
      if (throttle <= 0.35) thrVal = 'AH 1';
      else if (throttle <= 0.7) thrVal = 'AH 2';
      else thrVal = 'AH 3';
    } else if (throttle < 0) {
      if (throttle >= -0.35) thrVal = 'AS 1';
      else if (throttle >= -0.7) thrVal = 'AS 2';
      else thrVal = 'AS 3';
    }

    updatePlaqueDOM(
      ownMarker.current.getElement() as HTMLDivElement,
      isSelected,
      'ownship',
      'OWN',
      hdgVal,
      rudVal,
      sogVal,
      thrVal,
      hdgDeg,
      30   // own-ship marker is 30 px
    );

    // COG leader (disabled/removed to prevent overlapping with safety rings)
    (map.getSource('own-cog') as any)?.setData({
      type: 'FeatureCollection',
      features: [],
    });

    // cpa rings update removed

    // Follow
    if (followOwnShip && !previewData) {
      if (!firstFit.current) {
        // Initial load: fit all ships in view so the operator can see the full
        // encounter before the viewport locks to own ship.
        const freshTargets = useTelemetryStore.getState().targets;
        const targetLons = freshTargets
          .map((t: TargetVesselState) => t.pose?.lon)
          .filter((v): v is number => typeof v === 'number');
        const targetLats = freshTargets
          .map((t: TargetVesselState) => t.pose?.lat)
          .filter((v): v is number => typeof v === 'number');
        const allLons = [lon, ...targetLons];
        const allLats = [lat, ...targetLats];
        if (allLons.length > 1 && (Math.max(...allLons) - Math.min(...allLons) > 0.001 || Math.max(...allLats) - Math.min(...allLats) > 0.001)) {
          map.fitBounds([[Math.min(...allLons) - 0.02, Math.min(...allLats) - 0.02], [Math.max(...allLons) + 0.02, Math.max(...allLats) + 0.02]], {
            padding: { top: 60, bottom: 60, left: 60, right: 100 },
            maxZoom: MAP_MAX_ZOOM,
            duration: 1500,
          });
        } else {
          map.jumpTo({ center: [lon, lat], zoom: Math.min(16, MAP_MAX_ZOOM) });
        }
        map.setPadding({
          top: map.getContainer().clientHeight * (0.5 - viewportOffset[1]) * 2,
          bottom: map.getContainer().clientHeight * (viewportOffset[1] - 0.5) * 2,
          left: 0,
          right: 0,
        });
        firstFit.current = true;
        lastPanAt.current = Date.now();
      } else {
        if (simRate > 1) {
          // Smooth easing even at high speed, but with shorter duration to prevent lagging behind
          map.easeTo({ center: [lon, lat], duration: Math.max(16, 100 / simRate), essential: true });
        } else {
          map.easeTo({ center: [lon, lat], duration: 100 });
        }
      }
    } else if (previewData) {
      // Fit all vessels (OS + targets) in view with padding
      const allLons = [lon, ...(previewData.targets || []).map(t => t.lon)];
      const allLats = [lat, ...(previewData.targets || []).map(t => t.lat)];
      const minLon = Math.min(...allLons), maxLon = Math.max(...allLons);
      const minLat = Math.min(...allLats), maxLat = Math.max(...allLats);

      const currentCenter = map.getCenter();
      const dist = Math.abs(currentCenter.lng - lon) + Math.abs(currentCenter.lat - lat);
      if (!firstFit.current || dist > 0.5) {
        if (allLons.length > 1 && (maxLon - minLon > 0.001 || maxLat - minLat > 0.001)) {
          map.fitBounds([[minLon - 0.02, minLat - 0.02], [maxLon + 0.02, maxLat + 0.02]], {
            padding: { top: 60, bottom: 60, left: 60, right: 100 },
            maxZoom: MAP_MAX_ZOOM,
            duration: 1500,
          });
        } else {
          map.flyTo({ center: [lon, lat], zoom: Math.min(16, MAP_MAX_ZOOM), duration: 1500 });
        }
        firstFit.current = true;
      }
    }
  }, [ownShip, followOwnShip, viewMode, previewData, selectedVesselId, setSelectedVesselId, simRate]);

  // ── Trail update ────────────────────────────────────────────────────────────
  useEffect(() => {
    const map = mapRef.current;
    if (!map || !styleReady.current || trail.length < 2 || previewData) {
      // Clear trail in preview mode
      if (styleReady.current && map) {
        (map.getSource('trail') as any)?.setData({ type: 'FeatureCollection', features: [] });
      }
      return;
    }
    (map.getSource('trail') as any)?.setData({
      type: 'FeatureCollection',
      features: [{ type: 'Feature', geometry: { type: 'LineString', coordinates: trail },
                   properties: {} }],
    });
  }, [trail, previewData]);

  // ── Target Trails update ───────────────────────────────────────────────────
  useEffect(() => {
    const map = mapRef.current;
    if (!map || !styleReady.current || previewData) {
      // Clear target trails in preview mode
      if (styleReady.current && map) {
        (map.getSource('tgt-trail') as any)?.setData({ type: 'FeatureCollection', features: [] });
      }
      return;
    }

    const targetTrailFeatures: GeoJSON.Feature[] = [];
    if (Array.isArray(targets)) {
      for (const t of targets) {
        const id = t.mmsi != null ? String(t.mmsi) : null;
        if (id && targetTrails[id] && targetTrails[id].length >= 2) {
          targetTrailFeatures.push({
            type: 'Feature',
            geometry: { type: 'LineString', coordinates: targetTrails[id] },
            properties: { id },
          });
        }
      }
    }

    (map.getSource('tgt-trail') as any)?.setData({
      type: 'FeatureCollection',
      features: targetTrailFeatures,
    });
  }, [targetTrails, targets, previewData]);

  // ── Measurement Ruler update ───────────────────────────────────────────────
  useEffect(() => {
    const map = mapRef.current;
    if (!map || !styleReady.current) return;

    const features: any[] = [];
    const lines = [...rulerLines];
    if (activeLine) {
      lines.push({
        id: 'active',
        start: activeLine.start,
        end: activeLine.current,
        label: activeLine.label,
      });
    }

    for (const line of lines) {
      // LineString Feature
      features.push({
        type: 'Feature',
        geometry: { type: 'LineString', coordinates: [line.start, line.end] },
        properties: { type: 'line' },
      });

      // Midpoint Point Feature for label
      const midLng = (line.start[0] + line.end[0]) / 2;
      const midLat = (line.start[1] + line.end[1]) / 2;
      features.push({
        type: 'Feature',
        geometry: { type: 'Point', coordinates: [midLng, midLat] },
        properties: { type: 'label', label: line.label },
      });
    }

    (map.getSource('measurement-ruler') as any)?.setData({
      type: 'FeatureCollection',
      features,
    });
  }, [rulerLines, activeLine]);

  // ── Snapping & Measurement Map Event Listeners ─────────────────────────────
  useEffect(() => {
    const map = mapRef.current;
    if (!map || !styleReady.current) return;

    // Helper: calculate snapped coordinate
    const getSnappedCoordinate = (lng: number, lat: number, mousePt?: { x: number; y: number }): { coords: [number, number]; id?: string } => {
      const px = mousePt || map.project([lng, lat]);
      
      // Collect all vessel positions
      const vessels: { id: string; lat: number; lon: number }[] = [];
      if (ownShip && ownShip.pose?.lat != null && ownShip.pose?.lon != null) {
        vessels.push({ id: 'ownship', lat: ownShip.pose.lat, lon: ownShip.pose.lon });
      }
      if (targets && Array.isArray(targets)) {
        for (const t of targets) {
          const latVal = t.pose?.lat;
          const lonVal = t.pose?.lon;
          if (latVal != null && lonVal != null) {
            vessels.push({ id: t.mmsi != null ? String(t.mmsi) : 'tgt', lat: latVal, lon: lonVal });
          }
        }
      }

      for (const v of vessels) {
        const vPx = map.project([v.lon, v.lat]);
        const dx = px.x - vPx.x;
        const dy = px.y - vPx.y;
        if (Math.sqrt(dx * dx + dy * dy) < 40) { // Snapping threshold: 40px for generous snapping zone
          return { coords: [v.lon, v.lat], id: v.id };
        }
      }

      return { coords: [lng, lat] };
    };

    const handleMapClick = (e: any) => {
      if (measurementMode === 'none') return;

      const snapped = getSnappedCoordinate(e.lngLat.lng, e.lngLat.lat, e.point);

      if (measurementMode === 'freeform') {
        if (!activeLine) {
          // Set starting point
          setActiveLine({
            start: snapped.coords,
            current: snapped.coords,
            startSnapVesselId: snapped.id,
            label: '0.00 nm / 000°',
          });
        } else {
          // Finish drawing
          const dist = calculateDistanceNm(activeLine.start[0], activeLine.start[1], snapped.coords[0], snapped.coords[1]);
          const brg = calculateBearingDeg(activeLine.start[0], activeLine.start[1], snapped.coords[0], snapped.coords[1]);
          const formatted = `${dist.toFixed(2)} nm / ${String(Math.round(brg)).padStart(3, '0')}°`;

          const newLine = {
            id: String(Date.now()),
            start: activeLine.start,
            end: snapped.coords,
            startSnapVesselId: activeLine.startSnapVesselId,
            endSnapVesselId: snapped.id,
            label: formatted,
          };
          setRulerLines(prev => [...prev, newLine]);
          setActiveLine(null);
          setMeasurementMode('none');
        }
      } else if (measurementMode === 'vessel') {
        if (activeLine) {
          // Finish drawing starting from ship
          const dist = calculateDistanceNm(activeLine.start[0], activeLine.start[1], snapped.coords[0], snapped.coords[1]);
          const brg = calculateBearingDeg(activeLine.start[0], activeLine.start[1], snapped.coords[0], snapped.coords[1]);
          const formatted = `${dist.toFixed(2)} nm / ${String(Math.round(brg)).padStart(3, '0')}°`;

          const newLine = {
            id: String(Date.now()),
            start: activeLine.start,
            end: snapped.coords,
            startSnapVesselId: activeLine.startSnapVesselId,
            endSnapVesselId: snapped.id,
            label: formatted,
          };
          setRulerLines(prev => [...prev, newLine]);
          setActiveLine(null);
          setMeasurementMode('none');
        }
      }
    };

    const handleMapMouseMove = (e: any) => {
      if (measurementMode === 'none' || !activeLine) return;

      const snapped = getSnappedCoordinate(e.lngLat.lng, e.lngLat.lat, e.point);
      const dist = calculateDistanceNm(activeLine.start[0], activeLine.start[1], snapped.coords[0], snapped.coords[1]);
      const brg = calculateBearingDeg(activeLine.start[0], activeLine.start[1], snapped.coords[0], snapped.coords[1]);
      const formatted = `${dist.toFixed(2)} nm / ${String(Math.round(brg)).padStart(3, '0')}°`;

      setActiveLine({
        start: activeLine.start,
        current: snapped.coords,
        startSnapVesselId: activeLine.startSnapVesselId,
        currentSnapVesselId: snapped.id,
        label: formatted,
      });
    };

    const handleMapContextMenu = (e: any) => {
      if (!selectedVesselId) return;

      // Prevent default browser menu
      e.originalEvent.preventDefault();

      setContextMenu({
        x: e.point.x,
        y: e.point.y,
        lng: e.lngLat.lng,
        lat: e.lngLat.lat,
      });
    };

    // Close context menu on click elsewhere
    const handleCloseMenu = () => {
      setContextMenu(null);
    };

    map.on('click', handleMapClick);
    map.on('mousemove', handleMapMouseMove);
    map.on('contextmenu', handleMapContextMenu);
    map.on('mousedown', handleCloseMenu);

    return () => {
      map.off('click', handleMapClick);
      map.off('mousemove', handleMapMouseMove);
      map.off('contextmenu', handleMapContextMenu);
      map.off('mousedown', handleCloseMenu);
    };
  }, [measurementMode, rulerLines, activeLine, selectedVesselId, targets, ownShip, styleReady.current]);

  // ── Escape key measurement cancel listener ───────────────────────────────
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'Escape') {
        setActiveLine(null);
        setMeasurementMode('none');
        setContextMenu(null);
      }
    };
    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, []);

  // ── Target markers + COG leaders ───────────────────────────────────────────
  useEffect(() => {
    const map = mapRef.current;
    if (!map || !styleReady.current) return;

    const seen = new Set<string>();
    const cogFeatures: GeoJSON.Feature[] = [];
    const tickFeatures: GeoJSON.Feature[] = [];

    if (Array.isArray(targets)) {
      for (const t of targets) {
        const lat = t.pose?.lat;
        const lon = t.pose?.lon;
        if (typeof lat !== 'number' || typeof lon !== 'number') continue;
        const id = t.mmsi != null ? String(t.mmsi) : `tgt-${seen.size}`;
        seen.add(id);

        const hdgDeg = (t.pose?.heading ?? 0) * RAD;
        const cogRad = t.kinematics?.cog ?? 0;
        const sogMs  = t.kinematics?.sog ?? 0;

        // Marker
        let m = tgtMarkers.current.get(id);
        if (!m) {
          const el = makeVesselEl('#fbbf24', 24);
          el.addEventListener('click', (e) => {
            if (measurementModeRef.current !== 'none') {
              return; // Allow clicks to bubble up to the map in measurement mode
            }
            e.stopPropagation();
            setSelectedVesselId(id);
          });
          m = new maplibregl.Marker({ element: el, rotationAlignment: 'map', anchor: 'center' })
            .setLngLat([lon, lat]).addTo(map);
          tgtMarkers.current.set(id, m);
        } else {
          m.setLngLat([lon, lat]);
        }
        m.setRotation(hdgDeg);

        // COG leader & ticks (Scheme A)
        const cogPath = calculateCogPath(lon, lat, cogRad, sogMs);
        cogFeatures.push(cogPath.line);
        tickFeatures.push(...cogPath.ticks);

        // Plaque update
        const isSelected = selectedVesselId === id;
        const hdgVal = (Math.round(((t.pose?.heading ?? 0) * 180 / Math.PI + 360) % 360) % 360).toString();
        const sogVal = ((t.kinematics?.sog ?? 0) * 1.944).toFixed(1);

        // Target ships do not telemetry rudder angle or engine thrust states
        const rudVal = '—';
        const thrVal = '—';

        updatePlaqueDOM(
          m.getElement() as HTMLDivElement,
          isSelected,
          id,
          t.mmsi != null ? String(t.mmsi).slice(-3) : id.slice(-3),
          hdgVal,
          rudVal,
          sogVal,
          thrVal,
          hdgDeg,
          24   // target marker is 24 px
        );
      }
    }

    // Remove stale
    for (const [id, m] of Array.from(tgtMarkers.current.entries())) {
      if (!seen.has(id)) { m.remove(); tgtMarkers.current.delete(id); }
    }

    if (previewData) {
      (map.getSource('tgt-cog') as any)?.setData({ type: 'FeatureCollection', features: [] });
      (map.getSource('tgt-cog-ticks') as any)?.setData({ type: 'FeatureCollection', features: [] });
    } else {
      (map.getSource('tgt-cog') as any)?.setData({ type: 'FeatureCollection', features: cogFeatures });
      (map.getSource('tgt-cog-ticks') as any)?.setData({ type: 'FeatureCollection', features: tickFeatures });
    }
  }, [targets, selectedVesselId, setSelectedVesselId, previewData]);

  // ── Wind/current marker (disabled/removed as requested by user) ──────────────────
  useEffect(() => {
    if (windMarker.current) {
      windMarker.current.remove();
      windMarker.current = null;
    }
  }, []);

  /** Check if an ENC feature is a grounding hazard based on safety depth. */
  const isHazardFeature = (feature: any, safetyDepth = 10.0): boolean => {
    const layerId = feature.layer?.id;
    if (!layerId) return false;

    if (
      layerId === 'enc-land' ||
      layerId === 'enc-drying-area' ||
      layerId === 'enc-danger-area' ||
      layerId === 'enc-unsurveyed'
    ) {
      return true;
    }

    if (layerId === 'enc-depth-area') {
      // minimumsdybde represents minimum depth in meters
      const minDepth = feature.properties?.minimumsdybde;
      if (typeof minDepth === 'number') {
        return minDepth < safetyDepth;
      }
      // If undefined or null, default to shallow/hazard for safety
      if (minDepth === undefined || minDepth === null) {
        return true;
      }
    }

    return false;
  };

  // ── Grounding hazard detection @ 2 Hz (500ms downsampled) ──────────────────
  //
  // Evaluates grounding risk against S-57 hazard features and highlights
  // intersecting polygons in red. Bypasses fragile vector tile feature IDs
  // by writing exact intersecting geometries directly to the GeoJSON source.
  useEffect(() => {
    const hazardLayerIds = [
      'enc-depth-area',
      'enc-drying-area',
      'enc-unsurveyed',
      'enc-danger-area',
      'enc-land',
    ];

    const interval = setInterval(() => {
      const map = mapRef.current;
      if (!map || !styleReady.current) return; // skip — retry next tick

      const ensureHighlightLayer = () => {
        if (map.getLayer('grounding-hazard-highlight')) return;
        try {
          map.addSource('grounding-hazard', {
            type: 'geojson',
            data: { type: 'FeatureCollection', features: [] },
          });
          map.addLayer({
            id: 'grounding-hazard-highlight',
            type: 'fill',
            source: 'grounding-hazard',
            paint: {
              'fill-color': '#ff0000',
              'fill-opacity': 0.4,
              'fill-outline-color': '#cc0000',
            },
            layout: { visibility: 'none' },
          });
        } catch { /* */ }
      };
      ensureHighlightLayer();

      const os = useTelemetryStore.getState().ownShip;
      if (!os?.pose || !os?.kinematics) {
        try {
          map.setLayoutProperty('grounding-hazard-highlight', 'visibility', 'none');
        } catch { /* */ }
        return;
      }

      // OwnShipState → OwnShipPosition: cog rad→deg, sog m/s→kn
      const ship: OwnShipPosition = {
        lat: os.pose.lat,
        lon: os.pose.lon,
        cog_deg: os.kinematics.cog * (180 / Math.PI),
        sog_kn: os.kinematics.sog * 1.944,
      };

      const encFeatures = map.queryRenderedFeatures(undefined, {
        layers: hazardLayerIds,
      });

      // Filter viewport features to actual hazards based on safety threshold
      const hazardFeatures = encFeatures.filter((f) => isHazardFeature(f, 10.0));

      const result = checkGroundingRisk(ship, hazardFeatures);

      // Perform robust Turf geometry intersection check
      let intersectingFeats: any[] = [];
      if (ship.sog_kn >= 0.1) {
        const path = predictedPath(ship);
        intersectingFeats = hazardFeatures.filter((f) => {
          if (f.geometry.type !== 'Polygon' && f.geometry.type !== 'MultiPolygon') {
            return false;
          }
          try {
            return booleanIntersects(path, f);
          } catch {
            return false;
          }
        });
      }

      try {
        (map.getSource('grounding-hazard') as any)?.setData({
          type: 'FeatureCollection',
          features: intersectingFeats,
        });
        map.setLayoutProperty(
          'grounding-hazard-highlight',
          'visibility',
          result.isGroundingRisk ? 'visible' : 'none',
        );
      } catch { /* */ }
    }, 500);

    return () => {
      clearInterval(interval);
      try {
        const m = mapRef.current;
        if (!m) return;
        if (m.getLayer('grounding-hazard-highlight')) {
          m.removeLayer('grounding-hazard-highlight');
        }
        if (m.getSource('grounding-hazard')) {
          m.removeSource('grounding-hazard');
        }
      } catch { /* */ }
    };
  }, []);

  const formatCoord = (val: number, isLat: boolean) => {
    const absVal = Math.abs(val);
    const suffix = isLat ? (val >= 0 ? 'N' : 'S') : (val >= 0 ? 'E' : 'W');
    return `${absVal.toFixed(4)}°${suffix}`;
  };

  const formatDepth = (depth: { type: 'depth' | 'land' | 'drying'; min?: number; max?: number } | null): string => {
    if (!depth) return '';
    if (depth.type === 'land') return 'LAND';
    if (depth.type === 'drying') return 'DRY AREA';
    const { min, max } = depth;
    if (min != null && max != null && max < 1000) {
      return `DEP ${min}-${max}m`;
    }
    if (min != null) {
      return `DEP >= ${min}m`;
    }
    if (max != null) {
      return `DEP <= ${max}m`;
    }
    return 'DEP --';
  };

  const displayCoords = mousePos || mapCenter;

  // ── Render ──────────────────────────────────────────────────────────────────
  return (
    <div style={{ position: 'relative', width: '100%', height: '100%' }}>
      <div ref={mapContainer} style={{ width: '100%', height: '100%', background: '#070C13' }}
           data-testid="sil-map-view" />
      
      <ImazuGeometry mapRef={mapRef} geometry={geometry || null} />

      <style>{`
        .maplibregl-ctrl-scale {
          background: rgba(15, 23, 42, 0.6) !important;
          backdrop-filter: blur(12px) !important;
          border: 1px solid var(--line-1) !important;
          color: var(--c-phos) !important;
          font-family: var(--f-mono) !important;
          font-size: 11px !important;
          font-weight: 600 !important;
          height: 38px !important;
          width: 90px !important;
          min-width: 90px !important;
          display: flex !important;
          align-items: center !important;
          justify-content: center !important;
          padding: 0 !important;
          border-radius: 4px !important;
          margin-bottom: 68px !important;
          margin-left: 20px !important;
          box-shadow: 0 4px 30px rgba(0, 0, 0, 0.3) !important;
        }
      `}</style>

      {/* Coordinate Display next to Scale */}
      {status === 'ready' && displayCoords && (
        <div className="glass-panel hmi-surface" style={{
          position: 'absolute', bottom: 68, left: 120, padding: '0 16px',
          height: 38, display: 'flex', alignItems: 'center', gap: 12,
          border: '1px solid var(--line-1)', borderRadius: 4,
          fontFamily: 'var(--f-mono)', fontSize: 11, color: 'var(--txt-1)',
          zIndex: 10, pointerEvents: 'none', letterSpacing: '0.02em'
        }}>
          <span style={{ color: 'var(--c-phos)', transition: 'color 0.2s', fontWeight: 600 }}>
            {formatCoord(displayCoords.lat, true)}
          </span>
          <div style={{ width: 1, height: 14, background: 'var(--line-3)', opacity: 0.5 }} />
          <span style={{ color: 'var(--c-phos)', transition: 'color 0.2s', fontWeight: 600 }}>
            {formatCoord(displayCoords.lng, false)}
          </span>
          {mouseDepth && (
            <>
              <div style={{ width: 1, height: 14, background: 'var(--line-3)', opacity: 0.5 }} />
              <span style={{ color: 'var(--c-phos)', transition: 'color 0.2s', fontWeight: 600 }}>
                {formatDepth(mouseDepth)}
              </span>
            </>
          )}
        </div>
      )}

      {status !== 'ready' && (
        <div style={{
          position: 'absolute', bottom: 8, left: 8, padding: '4px 10px', borderRadius: 4,
          background: 'rgba(11,19,32,0.85)', color: '#888',
          fontFamily: 'monospace', fontSize: 11, pointerEvents: 'none',
        }} data-testid="sil-map-status">
          {status === 'no-webgl' ? 'Map: WebGL unavailable (headless?)' : 'Map: initialising…'}
        </div>
      )}

      {status === 'ready' && <MapZoomControl mapRef={mapRef} />}

      {/* Floating HMI Measurement Toolbar */}
      <div className="glass-panel" style={{
        position: 'absolute', bottom: 196, right: 20, zIndex: 110,
        display: 'flex', flexDirection: 'column', gap: 2,
        padding: '4px', borderRadius: 4, border: '1px solid var(--line-1)',
        pointerEvents: 'auto'
      }}>
        <button
          title="测距/测角 (Measure)"
          onClick={() => {
            setMeasurementMode(prev => prev === 'freeform' ? 'none' : 'freeform');
            setActiveLine(null);
          }}
          style={{
            width: 32, height: 32, borderRadius: 2, border: 'none', cursor: 'pointer',
            background: measurementMode === 'freeform' ? 'rgba(45, 212, 191, 0.2)' : 'transparent',
            color: measurementMode === 'freeform' ? 'var(--c-phos)' : 'var(--txt-2)',
            display: 'flex', alignItems: 'center', justifyContent: 'center', transition: 'all 0.2s',
            outline: 'none', padding: 0
          }}
          onMouseEnter={(e) => {
            if (measurementMode !== 'freeform') e.currentTarget.style.color = 'var(--c-phos)';
          }}
          onMouseLeave={(e) => {
            if (measurementMode !== 'freeform') e.currentTarget.style.color = 'var(--txt-2)';
          }}
        >
          <Ruler size={16} />
        </button>
        <div style={{ height: '1px', background: 'var(--line-1)', margin: '2px 4px' }} />
        <button
          title="清除测量 (Clear)"
          onClick={() => {
            setRulerLines([]);
            setActiveLine(null);
            setMeasurementMode('none');
          }}
          style={{
            width: 32, height: 32, borderRadius: 2, border: 'none', cursor: 'pointer',
            background: 'transparent', color: 'var(--txt-2)',
            display: 'flex', alignItems: 'center', justifyContent: 'center', transition: 'all 0.2s',
            outline: 'none', padding: 0
          }}
          onMouseEnter={(e) => e.currentTarget.style.color = 'var(--c-danger)'}
          onMouseLeave={(e) => e.currentTarget.style.color = 'var(--txt-2)'}
        >
          <Trash2 size={16} />
        </button>
      </div>

      {/* Global Right-Click HMI Context Menu */}
      {contextMenu && (
        <div style={{
          position: 'absolute', left: contextMenu.x, top: contextMenu.y, zIndex: 1000,
          background: 'rgba(7, 16, 27, 0.94)', backdropFilter: 'blur(12px)',
          border: '1px solid var(--line-2)', borderRadius: 6, padding: '4px 0',
          minWidth: 160, boxShadow: '0 8px 24px rgba(0,0,0,0.6)', pointerEvents: 'auto'
        }}>
          <div
            onClick={() => {
              const startVessel = selectedVesselId === 'ownship'
                ? (ownShip && ownShip.pose ? [ownShip.pose.lon, ownShip.pose.lat] as [number, number] : null)
                : (() => {
                    const t = targets.find(tgt => String(tgt.mmsi) === selectedVesselId);
                    return t && t.pose ? [t.pose.lon, t.pose.lat] as [number, number] : null;
                  })();
              if (startVessel) {
                setMeasurementMode('vessel');
                setActiveLine({
                  start: startVessel,
                  current: [contextMenu.lng, contextMenu.lat],
                  label: '0.00 nm / 000°',
                  startSnapVesselId: selectedVesselId || undefined
                });
              }
              setContextMenu(null);
            }}
            style={{
              padding: '8px 12px', color: 'var(--txt-1)', fontSize: 11, fontFamily: 'monospace',
              cursor: 'pointer', transition: 'background 0.15s'
            }}
            onMouseEnter={(e) => e.currentTarget.style.background = 'rgba(45,212,191,0.15)'}
            onMouseLeave={(e) => e.currentTarget.style.background = 'transparent'}
          >
            📐 测量相对距离/方位
          </div>
        </div>
      )}
    </div>
  );
}
