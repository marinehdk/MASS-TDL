import { useEffect, useRef, useState } from 'react';
import maplibregl from 'maplibre-gl';
import 'maplibre-gl/dist/maplibre-gl.css';
import {
  osmSource, osmLayer,
  s57Source, s57Layers,
  cpaRingLayer,
} from './layers';
import { useTelemetryStore } from '../store';

interface SilMapViewProps {
  followOwnShip?: boolean;
  viewMode?: 'captain' | 'god';
}

const RAD_TO_DEG = 180 / Math.PI;

function circleFeature(lon: number, lat: number, nm: number) {
  const points: [number, number][] = [];
  const deg = nm / 60.0;
  const cosLat = Math.cos(lat * Math.PI / 180) || 1;
  const segments = 64;
  for (let i = 0; i <= segments; i++) {
    const ang = (i / segments) * 2 * Math.PI;
    points.push([lon + (deg * Math.sin(ang)) / cosLat, lat + deg * Math.cos(ang)]);
  }
  return {
    type: 'Feature' as const,
    geometry: { type: 'LineString' as const, coordinates: points },
    properties: { radius_nm: nm },
  };
}

// Build a DOM element for a vessel marker. SVG triangle so it can rotate
// without needing a font glyph; outlined circle as the base sprite.
function makeVesselEl(color: string, size = 26): HTMLDivElement {
  const el = document.createElement('div');
  el.style.width = `${size}px`;
  el.style.height = `${size}px`;
  el.style.pointerEvents = 'none';
  el.style.transformOrigin = '50% 50%';
  el.innerHTML = `
    <svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg" width="${size}" height="${size}">
      <circle cx="12" cy="12" r="6" fill="${color}" stroke="#0b1320" stroke-width="2"/>
      <polygon points="12,1 16,9 12,7 8,9" fill="${color}" stroke="#0b1320" stroke-width="1.5"/>
    </svg>`;
  return el;
}

export function SilMapView({ followOwnShip = true, viewMode = 'captain' }: SilMapViewProps) {
  const mapContainer = useRef<HTMLDivElement>(null);
  const mapRef = useRef<maplibregl.Map | null>(null);
  const styleReady = useRef(false);
  const lastPanAt = useRef(0);
  const firstFitDone = useRef(false);
  const ownMarkerRef = useRef<maplibregl.Marker | null>(null);
  const targetMarkers = useRef<Map<string, maplibregl.Marker>>(new Map());
  const [status, setStatus] = useState<'init' | 'ready' | 'no-webgl'>('init');

  const ownShip = useTelemetryStore((s) => s.ownShip);
  const targets = useTelemetryStore((s) => s.targets);

  // Initialise the map once. Only the OSM raster basemap + S-57 placeholder
  // sources are declared up front; the CPA ring is added as a GeoJSON line
  // source post-load. Vessel sprites are rendered as DOM Markers (not
  // GeoJSON Point sources) because MapLibre v4.7's GeoJSON worker silently
  // fails to (re)build tile indices for moving Point geometries — the
  // sprites never paint. Markers bypass the tile pipeline entirely.
  useEffect(() => {
    if (!mapContainer.current || mapRef.current) return;
    let map: maplibregl.Map;
    try {
      map = new maplibregl.Map({
        container: mapContainer.current,
        style: {
          version: 8,
          glyphs: 'https://demotiles.maplibre.org/font/{fontstack}/{range}.pbf',
          sources: {
            osm: osmSource as any,
            s57: s57Source as any,
          },
          layers: [
            osmLayer as any,
            ...s57Layers.map((l) => l as any),
          ],
        },
        center: [10.4, 63.4],
        zoom: 12,
      });
    } catch (err) {
      console.warn('[SilMapView] WebGL init failed', err);
      setStatus('no-webgl');
      return;
    }
    map.on('error', (e: any) => {
      const msg = String(e?.error?.message || e?.message || e);
      if (msg.includes('pbf') || msg.includes('s57') || msg.includes('Failed to fetch tile')) return;
      console.warn('[SilMapView] map error', msg);
    });
    map.on('load', () => {
      // CPA ring (LineString) is added as a real source so it renders below
      // markers in the WebGL canvas; the layer is filled with real data once
      // own-ship telemetry arrives (see effect below).
      map.addSource('cpa-ring', {
        type: 'geojson',
        data: {
          type: 'FeatureCollection',
          features: [circleFeature(10.4, 63.4, 0.5), circleFeature(10.4, 63.4, 1.0)],
        },
      });
      map.addLayer(cpaRingLayer as any);
      styleReady.current = true;
      setStatus('ready');
    });
    map.addControl(new maplibregl.NavigationControl(), 'top-right');
    mapRef.current = map;
    return () => {
      ownMarkerRef.current?.remove();
      ownMarkerRef.current = null;
      targetMarkers.current.forEach((m) => m.remove());
      targetMarkers.current.clear();
      try { mapRef.current?.remove(); } catch { /* noop */ }
      mapRef.current = null;
      styleReady.current = false;
      firstFitDone.current = false;
    };
  }, []);

  // Update own-ship marker + CPA ring whenever telemetry changes
  useEffect(() => {
    const map = mapRef.current;
    if (!map || !styleReady.current || !ownShip) return;
    const lat = ownShip.pose?.lat;
    const lon = ownShip.pose?.lon;
    const heading = (ownShip.pose?.heading ?? 0) * RAD_TO_DEG;
    if (typeof lat !== 'number' || typeof lon !== 'number') return;

    // Create marker once, reuse via setLngLat / setRotation thereafter
    if (!ownMarkerRef.current) {
      const el = makeVesselEl('#2dd4bf', 28);
      ownMarkerRef.current = new maplibregl.Marker({ element: el, rotationAlignment: 'map' })
        .setLngLat([lon, lat])
        .addTo(map);
    } else {
      ownMarkerRef.current.setLngLat([lon, lat]);
    }
    ownMarkerRef.current.setRotation(heading);

    // Update CPA ring — setData works fine for LineString sources
    const ringFC = {
      type: 'FeatureCollection' as const,
      features: [circleFeature(lon, lat, 0.5), circleFeature(lon, lat, 1.0)],
    };
    (map.getSource('cpa-ring') as any)?.setData(ringFC);

    if (followOwnShip && viewMode === 'captain') {
      if (!firstFitDone.current) {
        map.jumpTo({ center: [lon, lat], zoom: 14 });
        firstFitDone.current = true;
        lastPanAt.current = Date.now();
      } else if (Date.now() - lastPanAt.current > 1000) {
        map.easeTo({ center: [lon, lat], duration: 600 });
        lastPanAt.current = Date.now();
      }
    }
  }, [ownShip, followOwnShip, viewMode]);

  // Update / create / remove target markers as the targets array changes
  useEffect(() => {
    const map = mapRef.current;
    if (!map || !styleReady.current) return;

    const seen = new Set<string>();
    for (const t of targets) {
      const lat = t.pose?.lat;
      const lon = t.pose?.lon;
      if (typeof lat !== 'number' || typeof lon !== 'number') continue;
      const id = t.mmsi != null ? String(t.mmsi) : `tgt-${seen.size}`;
      seen.add(id);
      const heading = (t.pose?.heading ?? 0) * RAD_TO_DEG;
      let marker = targetMarkers.current.get(id);
      if (!marker) {
        const el = makeVesselEl('#fbbf24', 22);
        marker = new maplibregl.Marker({ element: el, rotationAlignment: 'map' })
          .setLngLat([lon, lat])
          .addTo(map);
        targetMarkers.current.set(id, marker);
      } else {
        marker.setLngLat([lon, lat]);
      }
      marker.setRotation(heading);
    }
    // Remove markers for targets no longer in the array
    for (const [id, marker] of Array.from(targetMarkers.current.entries())) {
      if (!seen.has(id)) {
        marker.remove();
        targetMarkers.current.delete(id);
      }
    }
  }, [targets]);

  return (
    <div style={{ position: 'relative', width: '100%', height: '100%' }}>
      <div
        ref={mapContainer}
        style={{ width: '100%', height: '100%' }}
        data-testid="sil-map-view"
      />
      {status !== 'ready' && (
        <div style={{
          position: 'absolute', bottom: 8, left: 8,
          padding: '4px 10px', borderRadius: 4,
          background: 'rgba(11,19,32,0.85)', color: '#888',
          fontFamily: 'monospace', fontSize: 11, pointerEvents: 'none',
        }} data-testid="sil-map-status">
          {status === 'no-webgl' && 'Map: WebGL unavailable (headless?)'}
          {status === 'init' && 'Map: initialising…'}
        </div>
      )}
    </div>
  );
}
