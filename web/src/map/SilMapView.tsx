import { useEffect, useRef, useState } from 'react';
import maplibregl from 'maplibre-gl';
import 'maplibre-gl/dist/maplibre-gl.css';
import { osmSource, osmLayer, s57Source, ALL_S57_LAYERS } from './layers';
import { useTelemetryStore, useMapStore } from '../store';
import { MAP_MAX_ZOOM } from '../store/mapStore';
import { useMapPersistence } from '../hooks/useMapPersistence';

interface SilMapViewProps {
  followOwnShip?: boolean;
  viewMode?: 'captain' | 'god';
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
  el.style.cssText = `width:${size}px;height:${size}px;pointer-events:none;transform-origin:50% 50%`;
  const strokeW = ownship ? 2.5 : 1.8;
  el.innerHTML = `
    <svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg" width="${size}" height="${size}">
      <circle cx="12" cy="12" r="${ownship ? 7 : 5.5}" fill="${color}" stroke="#0b1320" stroke-width="${strokeW}" opacity="0.92"/>
      <polygon points="12,1 16.5,10 12,8 7.5,10" fill="${color}" stroke="#0b1320" stroke-width="${strokeW}"/>
    </svg>`;
  return el;
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
  geometry
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
  useMapPersistence(mapRef, viewMode);

  // Store selectors (memoised slices avoid 50 Hz whole-component re-renders)
  const ownShipFromStore  = useTelemetryStore((s) => s.ownShip);
  const targetsFromStore  = useTelemetryStore((s) => s.targets);
  const env      = useTelemetryStore((s) => s.environment);
  const trail    = useTelemetryStore((s) => s.ownShipTrail);

  // Use preview data if provided, otherwise use store
  const ownShip = previewData?.ownShip ? {
    pose: { lat: previewData.ownShip.lat, lon: previewData.ownShip.lon, heading: previewData.ownShip.heading / RAD },
    kinematics: { sog: previewData.ownShip.sog || 0, cog: (previewData.ownShip.cog || previewData.ownShip.heading) / RAD }
  } : ownShipFromStore;

  const targets = (previewData?.targets ? previewData.targets.map(t => ({
    mmsi: t.id,
    pose: { lat: t.lat, lon: t.lon, heading: t.heading / RAD },
    kinematics: { sog: t.sog || 0, cog: (t.cog || t.heading) / RAD }
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
            }
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
    if (typeof window !== 'undefined') { (window as any).__maplibre_map = map; }

    return () => {
      ownMarker.current?.remove(); ownMarker.current = null;
      windMarker.current?.remove(); windMarker.current = null;
      tgtMarkers.current.forEach((m) => m.remove());
      tgtMarkers.current.clear();
      try { mapRef.current?.remove(); } catch { /* noop */ }
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
      ownMarker.current = new maplibregl.Marker({ element: el, rotationAlignment: 'map' })
        .setLngLat([lon, lat]).addTo(map);
    } else {
      ownMarker.current.setLngLat([lon, lat]);
    }
    ownMarker.current.setRotation(hdgDeg);

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
        map.jumpTo({ center: [lon, lat], zoom: MAP_MAX_ZOOM});
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
  }, [ownShip, followOwnShip, viewMode, previewData]);

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
          m = new maplibregl.Marker({ element: el, rotationAlignment: 'map' })
            .setLngLat([lon, lat]).addTo(map);
          tgtMarkers.current.set(id, m);
        } else {
          m.setLngLat([lon, lat]);
        }
        m.setRotation(hdgDeg);

        // COG leader
        cogFeatures.push(cogLine(lon, lat, cogRad, sogMs));
      }
    }

    // Remove stale
    for (const [id, m] of Array.from(tgtMarkers.current.entries())) {
      if (!seen.has(id)) { m.remove(); tgtMarkers.current.delete(id); }
    }

    (map.getSource('tgt-cog') as any)?.setData({ type: 'FeatureCollection', features: cogFeatures });
  }, [targets]);

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
          margin-bottom: 20px !important;
          margin-left: 20px !important;
          box-shadow: 0 4px 30px rgba(0, 0, 0, 0.3) !important;
        }
      `}</style>

      {/* Coordinate Display next to Scale */}
      {status === 'ready' && displayCoords && (
        <div className="glass-panel hmi-surface" style={{
          position: 'absolute', bottom: 20, left: 120, padding: '0 16px',
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
