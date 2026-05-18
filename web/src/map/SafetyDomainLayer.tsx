import React, { useEffect, useRef } from 'react';
import type maplibregl from 'maplibre-gl';
import type { OwnShipState } from '../types';

interface SafetyDomainLayerProps {
  mapRef: React.MutableRefObject<maplibregl.Map | null>;
  ownShip: OwnShipState | null;
  visible: boolean;
  observationNm?: number;
  actionNm?: number;
  criticalNm?: number;
}

const SOURCE_ID = 'safety-domain';
const NM_TO_DEG = 1 / 60;

function circleFeature(
  lon: number, lat: number, radiusNm: number,
  properties: Record<string, unknown>
): GeoJSON.Feature<GeoJSON.Polygon> {
  const steps = 64;
  const radiusDeg = radiusNm * NM_TO_DEG;
  const coords: [number, number][] = [];
  for (let i = 0; i <= steps; i++) {
    const angle = (i / steps) * 2 * Math.PI;
    const lonCorrection = radiusDeg / Math.cos((lat * Math.PI) / 180);
    coords.push([lon + lonCorrection * Math.sin(angle), lat + radiusDeg * Math.cos(angle)]);
  }
  return { type: 'Feature', geometry: { type: 'Polygon', coordinates: [coords] }, properties };
}

export const SafetyDomainLayer: React.FC<SafetyDomainLayerProps> = React.memo(({
  mapRef, ownShip, visible,
  observationNm = 2.0, actionNm = 1.0, criticalNm = 0.3,
}) => {
  const addedRef = useRef(false);

  function buildFeatureCollection(ship: OwnShipState): GeoJSON.FeatureCollection {
    const lon = ship.pose?.lon ?? 0;
    const lat = ship.pose?.lat ?? 0;
    return {
      type: 'FeatureCollection',
      features: [
        circleFeature(lon, lat, observationNm, { tier: 'observation', radiusNm: observationNm }),
        circleFeature(lon, lat, actionNm,      { tier: 'action',      radiusNm: actionNm }),
        circleFeature(lon, lat, criticalNm,    { tier: 'critical',    radiusNm: criticalNm }),
      ],
    };
  }

  useEffect(() => {
    const map = mapRef.current;
    if (!map) return;

    function setup() {
      if (!map) return;
      const data = ownShip ? buildFeatureCollection(ownShip) : { type: 'FeatureCollection' as const, features: [] };

      if (!addedRef.current) {
        map.addSource(SOURCE_ID, { type: 'geojson', data });
        map.addLayer({
          id: 'safety-observation',
          type: 'line',
          source: SOURCE_ID,
          filter: ['==', ['get', 'tier'], 'observation'],
          paint: { 'line-color': '#6b7280', 'line-width': 1, 'line-dasharray': [4, 4], 'line-opacity': 0.5 },
        });
        map.addLayer({
          id: 'safety-action',
          type: 'line',
          source: SOURCE_ID,
          filter: ['==', ['get', 'tier'], 'action'],
          paint: { 'line-color': '#fbbf24', 'line-width': 1.5, 'line-dasharray': [2, 2], 'line-opacity': 0.7 },
        });
        map.addLayer({
          id: 'safety-critical',
          type: 'line',
          source: SOURCE_ID,
          filter: ['==', ['get', 'tier'], 'critical'],
          paint: { 'line-color': '#f87171', 'line-width': 2, 'line-opacity': 0.9 },
        });
        addedRef.current = true;
      } else {
        (map.getSource(SOURCE_ID) as any)?.setData(data);
      }
    }

    if (!map.isStyleLoaded()) {
      map.once('style.load', setup);
    } else {
      setup();
    }
  }, [mapRef, ownShip, visible, observationNm, actionNm, criticalNm]);

  return null;
});
SafetyDomainLayer.displayName = 'SafetyDomainLayer';
