import { useEffect, useRef, useState } from 'react';
import maplibregl from 'maplibre-gl';
import 'maplibre-gl/dist/maplibre-gl.css';
import { osmSource, osmLayer, ALL_S57_LAYERS } from './layers';
import { useTelemetryStore, useMapStore, useUIStore } from '../store';
import { MAP_MAX_ZOOM } from '../store/mapStore';
import { useMapPersistence } from '../hooks/useMapPersistence';
import type { TargetVesselState } from '../types/sil/target_vessel_state';
import { checkGroundingRisk } from '../utils/groundingDetect';
import type { OwnShipPosition } from '../utils/groundingDetect';

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

/** Circle polygon [lon,lat] around a centre, radius in nm. */
function circleFeature(lon: number, lat: number, nm: number): GeoJSON.Feature {
  const pts: [number, number][] = [];
  const cosLat = Math.cos(lat * Math.PI / 180) || 1e-9;
  const d = nm * NM_TO_DEG;
  for (let i = 0; i <= 64; i++) {
    const a = (i / 64) * 2 * Math.PI;
    pts.push([lon + (d * Math.sin(a)) / cosLat, lat + d * Math.cos(a)]);
  }
  return { type: 'Feature', geometry: { type: 'LineString', coordinates: pts }, properties: { nm } };
}

/** COG leader line — length depends on SOG (6-min projection). */
function cogLine(lon: number, lat: number, cogRad: number, sogMs: number): GeoJSON.Feature {
  const cogDeg = cogRad * RAD;
  const distNm = (sogMs * 360) / 1852; // 6 min @ sogMs m/s → nm
  const [lon2, lat2] = project(lon, lat, cogDeg, Math.max(distNm, 0.05));
  return {
    type: 'Feature',
    geometry: { type: 'LineString', coordinates: [[lon, lat], [lon2, lat2]] },
    properties: { cogDeg },
  };
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
  cog: string,
  sog: string,
  rot: string,
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
          <div style="font-size:8px;color:#8A9AAD;line-height:1;margin-bottom:1px;">COG</div>
          <div style="font-size:11px;color:#fff;font-weight:bold;line-height:1.1;">${cog}°</div>
        </div>
        <div>
          <div style="font-size:8px;color:#8A9AAD;line-height:1;margin-bottom:1px;">SOG</div>
          <div style="font-size:11px;color:#fff;font-weight:bold;line-height:1.1;">${sog}<span style="font-size:8px;font-weight:normal;color:#8A9AAD;"> kn</span></div>
        </div>
        <div>
          <div style="font-size:8px;color:#8A9AAD;line-height:1;margin-bottom:1px;">ROT</div>
          <div style="font-size:11px;color:#fff;font-weight:bold;line-height:1.1;">${rot}<span style="font-size:8px;font-weight:normal;color:#8A9AAD;">°/m</span></div>
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

// ─────────────────────────────────────────────────────────────────────────────
export function SilMapView({ 
  followOwnShip = true, 
  viewMode = 'captain', 
  viewportOffset = [0.5, 0.5],
  previewData,
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

  // Keep click handler ref in sync without re-initializing map
  useEffect(() => { onMapClickRef.current = onMapClick; }, [onMapClick]);

  // Cross-screen viewport persistence
  useMapPersistence(mapRef, viewMode === 'god' ? 'god' : 'captain');

  // Store selectors (memoised slices avoid 50 Hz whole-component re-renders)
  const ownShipFromStore  = useTelemetryStore((s) => s.ownShip);
  const targetsFromStore  = useTelemetryStore((s) => s.targets);
  const env      = useTelemetryStore((s) => s.environment);
  const trail    = useTelemetryStore((s) => s.ownShipTrail);
  const selectedVesselId = useUIStore((s) => s.selectedVesselId);
  const setSelectedVesselId = useUIStore((s) => s.setSelectedVesselId);

  // Use preview data if provided, otherwise use store
  const ownShip = previewData?.ownShip ? {
    pose: { lat: previewData.ownShip.lat, lon: previewData.ownShip.lon, heading: previewData.ownShip.heading / RAD },
    kinematics: { sog: previewData.ownShip.sog || 0, cog: (previewData.ownShip.cog || previewData.ownShip.heading) / RAD, rot: 0 }
  } : ownShipFromStore;

  const targets = (previewData?.targets ? previewData.targets.map(t => ({
    mmsi: t.id,
    pose: { lat: t.lat, lon: t.lon, heading: t.heading / RAD },
    kinematics: { sog: t.sog || 0, cog: (t.cog || t.heading) / RAD, rot: 0 }
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
              tiles: [`http://localhost:3000/${previewData?.encRegion || 'trondelag'}/{z}/{x}/{y}`],
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
      if (/pbf|s57|enc|Failed to fetch|tile|glyphs|3000|source-layer/i.test(msg)) return;
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
        paint: { 'line-color': '#fbbf24', 'line-width': 2, 'line-opacity': 0.8 },
      });

      // ── CPA / danger rings ───────────────────────────────────────────────
      map.addSource('cpa-rings', {
        type: 'geojson',
        data: { type: 'FeatureCollection', features: [] },
      });
      map.addLayer({
        id: 'cpa-rings-line',
        type: 'line',
        source: 'cpa-rings',
        paint: { 'line-color': '#f87171', 'line-width': 1.5,
                 'line-dasharray': [4, 3], 'line-opacity': 0.7 },
      });

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
    });

    map.on('mouseleave', () => {
      setMousePos(null);
    });

    map.on('move', () => {
      setMapCenter(map.getCenter());
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
        e.stopPropagation();
        setSelectedVesselId('ownship');
      });
      ownMarker.current = new maplibregl.Marker({ element: el, rotationAlignment: 'map' })
        .setLngLat([lon, lat]).addTo(map);
    } else {
      ownMarker.current.setLngLat([lon, lat]);
    }
    ownMarker.current.setRotation(hdgDeg);

    // Plaque update
    const isSelected = selectedVesselId === 'ownship';
    const hdgVal = (((ownShip.pose?.heading ?? 0) * 180 / Math.PI + 360) % 360).toFixed(0);
    const cogVal = (((ownShip.kinematics?.cog ?? 0) * 180 / Math.PI + 360) % 360).toFixed(0);
    const sogVal = ((ownShip.kinematics?.sog ?? 0) * 1.944).toFixed(1);
    const rotVal = ((ownShip.kinematics?.rot ?? 0) * 180 / Math.PI * 60).toFixed(1);

    updatePlaqueDOM(
      ownMarker.current.getElement() as HTMLDivElement,
      isSelected,
      'ownship',
      'OWN',
      hdgVal,
      cogVal,
      sogVal,
      rotVal,
      hdgDeg,
      30   // own-ship marker is 30 px
    );

    // COG leader
    (map.getSource('own-cog') as any)?.setData({
      type: 'FeatureCollection',
      features: [cogLine(lon, lat, cogRad, sogMs)],
    });

    // CPA rings — 0.5 nm and 1.0 nm around own-ship
    (map.getSource('cpa-rings') as any)?.setData({
      type: 'FeatureCollection',
      features: [circleFeature(lon, lat, 0.5), circleFeature(lon, lat, 1.0)],
    });

    // Follow
    if (followOwnShip && viewMode === 'captain' && !previewData) {
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
          map.jumpTo({ center: [lon, lat], zoom: MAP_MAX_ZOOM });
        }
        map.setPadding({
          top: map.getContainer().clientHeight * (0.5 - viewportOffset[1]) * 2,
          bottom: map.getContainer().clientHeight * (viewportOffset[1] - 0.5) * 2,
          left: 0,
          right: 0,
        });
        firstFit.current = true;
        lastPanAt.current = Date.now();
      } else if (Date.now() - lastPanAt.current > 800) {
        map.easeTo({ center: [lon, lat], duration: 500 });
        lastPanAt.current = Date.now();
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
          map.flyTo({ center: [lon, lat], zoom: MAP_MAX_ZOOM, duration: 1500 });
        }
        firstFit.current = true;
      }
    }
  }, [ownShip, followOwnShip, viewMode, previewData, selectedVesselId, setSelectedVesselId]);

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

  // ── Target markers + COG leaders ───────────────────────────────────────────
  useEffect(() => {
    const map = mapRef.current;
    if (!map || !styleReady.current) return;

    const seen = new Set<string>();
    const cogFeatures: GeoJSON.Feature[] = [];

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
            e.stopPropagation();
            setSelectedVesselId(id);
          });
          m = new maplibregl.Marker({ element: el, rotationAlignment: 'map' })
            .setLngLat([lon, lat]).addTo(map);
          tgtMarkers.current.set(id, m);
        } else {
          m.setLngLat([lon, lat]);
        }
        m.setRotation(hdgDeg);

        // COG leader
        cogFeatures.push(cogLine(lon, lat, cogRad, sogMs));

        // Plaque update
        const isSelected = selectedVesselId === id;
        const hdgVal = (((t.pose?.heading ?? 0) * 180 / Math.PI + 360) % 360).toFixed(0);
        const cogVal = (((t.kinematics?.cog ?? 0) * 180 / Math.PI + 360) % 360).toFixed(0);
        const sogVal = ((t.kinematics?.sog ?? 0) * 1.944).toFixed(1);
        const rotVal = ((t.kinematics?.rot ?? 0) * 180 / Math.PI * 60).toFixed(1);

        updatePlaqueDOM(
          m.getElement() as HTMLDivElement,
          isSelected,
          id,
          t.mmsi != null ? String(t.mmsi).slice(-3) : id.slice(-3),
          hdgVal,
          cogVal,
          sogVal,
          rotVal,
          hdgDeg,
          24   // target marker is 24 px
        );
      }
    }

    // Remove stale
    for (const [id, m] of Array.from(tgtMarkers.current.entries())) {
      if (!seen.has(id)) { m.remove(); tgtMarkers.current.delete(id); }
    }

    (map.getSource('tgt-cog') as any)?.setData({ type: 'FeatureCollection', features: cogFeatures });
  }, [targets, selectedVesselId, setSelectedVesselId]);

  // ── Wind/current marker (top-left, fixed screen position) ──────────────────
  useEffect(() => {
    const map = mapRef.current;
    if (!map || !styleReady.current || !env || previewData) {
      if (windMarker.current) { windMarker.current.remove(); windMarker.current = null; }
      return;
    }
    const dir  = env.wind?.direction ?? 0;
    const spd  = env.wind?.speedMps ?? 0;
    const centre = map.getCenter();

    if (!windMarker.current) {
      const el = makeWindEl(dir * RAD, spd);
      windMarker.current = new maplibregl.Marker({ element: el })
        .setLngLat([centre.lng, centre.lat]).addTo(map);
    } else {
      // update inner HTML
      const el = windMarker.current.getElement();
      el.innerHTML = makeWindEl(dir * RAD, spd).innerHTML;
    }
  }, [env, previewData]);

  // ── Grounding hazard detection @ 10 Hz ─────────────────────────────────────
  //
  // 100 ms interval reading own-ship via store.getState() (no re-render) and
  // querying ENC fill layers for intersecting hazard polygons. A red
  // highlight layer is toggled on/off based on checkGroundingRisk().
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

      const result = checkGroundingRisk(ship, encFeatures);

      const intersectingFeats = encFeatures.filter((f) => {
        return (
          result.riskPolygonIds.length > 0 &&
          (result.riskPolygonIds.includes(String(f.id)) ||
            result.riskPolygonIds.includes(String((f as any).properties?.id)))
        );
      });

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
    }, 100);

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

  const displayCoords = mousePos || mapCenter;

  // ── Render ──────────────────────────────────────────────────────────────────
  return (
    <div style={{ position: 'relative', width: '100%', height: '100%' }}>
      <div ref={mapContainer} style={{ width: '100%', height: '100%' }}
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
    </div>
  );
}
