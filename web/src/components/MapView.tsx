import { useEffect, useRef, useState } from 'react';
import maplibregl from 'maplibre-gl';
import 'maplibre-gl/dist/maplibre-gl.css';
import { OWN_SHIP_SPRITE, TARGET_SHIP_SPRITE } from './shipSprite';
import type { NavState, TargetState } from '../types/sil';

/** Trondheim Fjord demo viewport */
const CENTER: [number, number] = [10.395, 63.435]; // [lng, lat]
const ZOOM = 11;

interface Props {
  mapRef?: React.MutableRefObject<maplibregl.Map | null>;
}

export default function MapView({ mapRef }: Props) {
  const mapContainer = useRef<HTMLDivElement>(null);
  const internalMapRef = useRef<maplibregl.Map | null>(null);
  const [loaded, setLoaded] = useState(false);
  const [webglError, setWebglError] = useState(false);

  useEffect(() => {
    if (!mapContainer.current || internalMapRef.current) return;

    let map: maplibregl.Map;
    try {
      map = new maplibregl.Map({
        container: mapContainer.current!,
        style: {
          version: 8,
          name: 'D1.3b.3 SIL HMI',
          sources: {
            enc: {
              type: 'vector',
              tiles: [`${window.location.origin}/tiles/trondheim/{z}/{x}/{y}.pbf`],
              minzoom: 6,
              maxzoom: 14,
            },
          },
          layers: [
            // Depth areas (water fill)
            {
              id: 'enc-depth',
              source: 'enc',
              'source-layer': 'enc',
              type: 'fill',
              filter: ['==', ['get', '_layer'], 'dybdeareal'],
              paint: {
                'fill-color': [
                  'interpolate', ['linear'], ['zoom'],
                  6, '#0d2b4a',
                  10, '#12406b',
                  14, '#1a5c8a',
                ],
                'fill-opacity': 0.85,
              },
            },
            // Land areas
            {
              id: 'enc-land',
              source: 'enc',
              'source-layer': 'enc',
              type: 'fill',
              filter: ['==', ['get', '_layer'], 'landareal'],
              paint: { 'fill-color': '#1e2d1e', 'fill-opacity': 0.9 },
            },
            // Coastline
            {
              id: 'enc-coast',
              source: 'enc',
              'source-layer': 'enc',
              type: 'line',
              filter: ['==', ['get', '_layer'], 'kystkontur'],
              paint: { 'line-color': '#2a4020', 'line-width': 0.5 },
            },
            // Depth contours
            {
              id: 'enc-depth-contour',
              source: 'enc',
              'source-layer': 'enc',
              type: 'line',
              filter: ['==', ['get', '_layer'], 'dybdekurve'],
              paint: {
                'line-color': 'rgba(100,160,220,0.15)',
                'line-width': 0.3,
              },
            },
            // Danger areas (fareomrade)
            {
              id: 'enc-danger',
              source: 'enc',
              'source-layer': 'enc',
              type: 'fill',
              filter: ['==', ['get', '_layer'], 'fareomrade'],
              paint: { 'fill-color': '#F85149', 'fill-opacity': 0.2 },
            },
            // Shoals/rocks (grunne = point)
            {
              id: 'enc-shoal',
              source: 'enc',
              'source-layer': 'enc',
              type: 'circle',
              filter: ['in', ['get', '_layer'], ['literal', ['grunne', 'skjer']]],
              paint: {
                'circle-radius': 2.5,
                'circle-color': '#F26B6B',
                'circle-opacity': 0.6,
              },
            },
            // Piers/quays (kaibrygge)
            {
              id: 'enc-pier',
              source: 'enc',
              'source-layer': 'enc',
              type: 'line',
              filter: ['==', ['get', '_layer'], 'kaibrygge'],
              paint: {
                'line-color': '#8A9AAD',
                'line-width': 1,
                'line-opacity': 0.5,
              },
            },
          ],
        },
        center: CENTER,
        zoom: ZOOM,
        attributionControl: false,
      });

      map.addControl(new maplibregl.NavigationControl(), 'bottom-right');

      map.on('load', () => {
        // Load custom sprites
        const imgOwn = new Image();
        imgOwn.onload = () => { if (!map.hasImage('ship-own')) map.addImage('ship-own', imgOwn); };
        imgOwn.src = OWN_SHIP_SPRITE;

        const imgTarget = new Image();
        imgTarget.onload = () => { if (!map.hasImage('ship-target')) map.addImage('ship-target', imgTarget); };
        imgTarget.src = TARGET_SHIP_SPRITE;

        // Add empty GeoJSON sources (populated by data hooks in Task 6)
        map.addSource('own-ship-src', {
          type: 'geojson',
          data: { type: 'FeatureCollection', features: [] },
        });
        map.addSource('target-ships-src', {
          type: 'geojson',
          data: { type: 'FeatureCollection', features: [] },
        });
        map.addSource('cpa-ring-src', {
          type: 'geojson',
          data: { type: 'FeatureCollection', features: [] },
        });
        map.addSource('cog-vector-src', {
          type: 'geojson',
          data: { type: 'FeatureCollection', features: [] },
        });

        // Own-ship symbol layer (top-most layer)
        map.addLayer({
          id: 'own-ship',
          type: 'symbol',
          source: 'own-ship-src',
          layout: {
            'icon-image': 'ship-own',
            'icon-rotate': ['get', 'heading_deg'],
            'icon-rotation-alignment': 'map',
            'icon-size': 0.07,
            'icon-allow-overlap': true,
          },
        });

        // Target ships symbol layer
        map.addLayer({
          id: 'target-ships',
          type: 'symbol',
          source: 'target-ships-src',
          layout: {
            'icon-image': 'ship-target',
            'icon-rotate': ['get', 'heading_deg'],
            'icon-rotation-alignment': 'map',
            'icon-size': 0.05,
            'icon-allow-overlap': true,
          },
          paint: {
            'icon-opacity': [
              'case',
              ['>=', ['get', 'confidence'], 0.7], 1,
              0,
            ],
          },
        });

        // CPA ring circle layer
        map.addLayer({
          id: 'cpa-ring',
          type: 'circle',
          source: 'cpa-ring-src',
          paint: {
            'circle-radius': 60,
            'circle-color': '#F26B6B',
            'circle-opacity': 0.25,
            'circle-stroke-width': 1,
            'circle-stroke-color': '#F26B6B',
            'circle-stroke-opacity': 0.5,
          },
        });

        // COG vector line layer
        map.addLayer({
          id: 'cog-vector',
          type: 'line',
          source: 'cog-vector-src',
          paint: {
            'line-color': '#5BC0BE',
            'line-width': 1.5,
            'line-dasharray': [4, 2],
            'line-opacity': 0.7,
          },
        });

        setLoaded(true);
      });

    } catch (_e) {
      // WebGL not available (headless browser, no GPU) — show fallback overlay
      setWebglError(true);
      setLoaded(true); // dismiss loading overlay
      return;
    }

    internalMapRef.current = map;
    if (mapRef) mapRef.current = map;

    return () => {
      if (mapRef) mapRef.current = null;
      internalMapRef.current?.remove();
      internalMapRef.current = null;
    };
  }, []);

  return (
    <div style={{ position: 'relative', width: '100%', height: '100%', background: '#0a1628' }}>
      <div ref={mapContainer} style={{ width: '100%', height: '100%' }} />
      {/* Scale/Location indicator */}
      <div style={{
        position: 'absolute',
        bottom: '12px',
        left: '12px',
        padding: '4px 10px',
        background: 'rgba(11,19,32,0.75)',
        backdropFilter: 'blur(4px)',
        border: '1px solid var(--line-1)',
        fontFamily: 'var(--fnt-mono)',
        fontSize: '10px',
        color: 'var(--txt-2)',
        borderRadius: 'var(--radius-none)',
        zIndex: 10,
      }}>
        Z{ZOOM} · 63.43°N 10.40°E · Trondheim Fjord
      </div>
      {/* Loading overlay */}
      {!loaded && !webglError && (
        <div style={{
          position: 'absolute',
          inset: 0,
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          background: 'rgba(7,12,19,0.85)',
          zIndex: 99,
        }}>
          <div style={{
            fontFamily: 'var(--fnt-disp)',
            fontSize: '14px',
            color: 'var(--txt-2)',
            letterSpacing: '0.12em',
          }}>
            Loading ENC Charts...
          </div>
        </div>
      )}
      {/* WebGL unavailable fallback */}
      {webglError && (
        <div style={{
          position: 'absolute',
          inset: 0,
          display: 'flex',
          flexDirection: 'column',
          alignItems: 'center',
          justifyContent: 'center',
          background: '#0a1628',
          gap: '12px',
          zIndex: 50,
        }}>
          <div style={{
            fontFamily: 'var(--fnt-disp)',
            fontSize: '18px',
            color: 'var(--color-phos)',
            letterSpacing: '0.08em',
          }}>
            TRONDHEIM FJORD
          </div>
          <div style={{
            fontFamily: 'var(--fnt-mono)',
            fontSize: '12px',
            color: 'var(--txt-2)',
          }}>
            63.43°N 10.40°E · Z{ZOOM} · ENC Charts require WebGL
          </div>
          <div style={{
            fontFamily: 'var(--fnt-mono)',
            fontSize: '10px',
            color: 'var(--txt-3)',
          }}>
            Open in GPU-enabled browser for full MapLibre GL rendering
          </div>
        </div>
      )}
    </div>
  );
}

/** Update own-ship position, heading, and COG vector on the map */
export function updateOwnShipOnMap(map: maplibregl.Map | null, nav: NavState | null) {
  if (!map || !nav || !map.loaded()) return;

  const src = map.getSource('own-ship-src') as maplibregl.GeoJSONSource;
  if (!src) return;

  src.setData({
    type: 'FeatureCollection',
    features: [{
      type: 'Feature',
      geometry: { type: 'Point', coordinates: [nav.lon, nav.lat] },
      properties: { heading_deg: nav.heading_deg, sog_kn: nav.sog_kn },
    }],
  });

  // COG vector: line from own-ship to predicted position (SOG x 60s)
  const R = 6371000;
  const headingRad = (nav.cog_deg * Math.PI) / 180;
  const distM = nav.sog_kn * 0.514444 * 60;
  const dLat = (distM * Math.cos(headingRad)) / R;
  const dLng = (distM * Math.sin(headingRad)) / (R * Math.cos((nav.lat * Math.PI) / 180));

  const cogSrc = map.getSource('cog-vector-src') as maplibregl.GeoJSONSource;
  if (cogSrc) {
    cogSrc.setData({
      type: 'FeatureCollection',
      features: [{
        type: 'Feature',
        geometry: {
          type: 'LineString',
          coordinates: [
            [nav.lon, nav.lat],
            [nav.lon + (dLng * 180) / Math.PI, nav.lat + (dLat * 180) / Math.PI],
          ],
        },
        properties: {},
      }],
    });
  }
}

/** Update target ships and CPA ring on the map */
export function updateTargetsOnMap(map: maplibregl.Map | null, targets: TargetState[]) {
  if (!map || !map.loaded()) return;

  const tgtSrc = map.getSource('target-ships-src') as maplibregl.GeoJSONSource;
  const cpaSrc = map.getSource('cpa-ring-src') as maplibregl.GeoJSONSource;

  if (tgtSrc) {
    tgtSrc.setData({
      type: 'FeatureCollection',
      features: targets
        .filter(t => t.confidence >= 0.7)
        .map(t => ({
          type: 'Feature' as const,
          geometry: { type: 'Point' as const, coordinates: [t.lon, t.lat] },
          properties: {
            heading_deg: t.heading_deg,
            colreg_role: t.colreg_role,
            confidence: t.confidence,
            mmsi: t.mmsi,
          },
        })),
    });
  }

  // CPA ring on closest threat
  if (cpaSrc && targets.length > 0) {
    const closest = targets.reduce((a, b) => a.cpa_nm < b.cpa_nm ? a : b);
    cpaSrc.setData({
      type: 'FeatureCollection',
      features: [{
        type: 'Feature',
        geometry: { type: 'Point', coordinates: [closest.lon, closest.lat] },
        properties: { cpa_nm: closest.cpa_nm },
      }],
    });
  }
}

/** Re-export maplibregl type for use by data hooks */
export type { maplibregl };
