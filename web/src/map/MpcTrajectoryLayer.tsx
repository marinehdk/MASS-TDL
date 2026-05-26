import React, { useEffect, useRef } from 'react';
import type maplibregl from 'maplibre-gl';
import type { TrajectoryCandidate } from '../types/sat';

interface MpcTrajectoryLayerProps {
  mapRef: React.MutableRefObject<maplibregl.Map | null>;
  candidates: TrajectoryCandidate[];
  visible: boolean;
}

const SOURCE_ID = 'mpc-trajectories';
const LAYER_ID  = 'mpc-trajectories-line';

function costToColor(cost: number): string {
  const r = Math.round(248 * cost + 52 * (1 - cost));
  const g = Math.round(113 * cost + 211 * (1 - cost));
  const b = Math.round(113 * cost + 153 * (1 - cost));
  return `#${r.toString(16).padStart(2, '0')}${g.toString(16).padStart(2, '0')}${b.toString(16).padStart(2, '0')}`;
}

function buildGeoJSON(candidates: TrajectoryCandidate[]): GeoJSON.FeatureCollection {
  const validCandidates = (candidates || []).filter((c) => c && Array.isArray(c.points));
  return {
    type: 'FeatureCollection',
    features: validCandidates.map((c) => ({
      type: 'Feature',
      geometry: {
        type: 'LineString',
        coordinates: c.points.map((p) => [p.lon, p.lat]),
      },
      properties: {
        id: c.id,
        cost: c.cost,
        is_optimal: c.is_optimal,
        type: c.type,
        color: c.is_optimal && c.type === 'mid_mpc' ? '#34d399' : costToColor(c.cost),
        line_width: c.is_optimal ? 3 : 1.5,
      },
    })),
  };
}

export const MpcTrajectoryLayer: React.FC<MpcTrajectoryLayerProps> = React.memo(({
  mapRef, candidates, visible,
}) => {
  const addedRef = useRef(false);

  useEffect(() => {
    const map = mapRef.current;
    if (!map) return;

    function setup() {
      if (!map) return;
      const data = buildGeoJSON(candidates);

      if (!addedRef.current) {
        map.addSource(SOURCE_ID, { type: 'geojson', data });
        map.addLayer({
          id: LAYER_ID,
          type: 'line',
          source: SOURCE_ID,
          layout: { 'line-cap': 'round', 'line-join': 'round' },
          paint: {
            'line-color': ['get', 'color'],
            'line-width': ['get', 'line_width'],
            'line-opacity': visible ? 0.85 : 0,
          },
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
  }, [mapRef, candidates, visible]);

  return null;
});
MpcTrajectoryLayer.displayName = 'MpcTrajectoryLayer';
