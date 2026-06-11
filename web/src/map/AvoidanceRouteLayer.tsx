import React, { useEffect, useRef, useState } from 'react';
import type maplibregl from 'maplibre-gl';
import type { AvoidancePlanData } from '../store/telemetryStore';
import { useTelemetryStore } from '../store';

interface AvoidanceRouteLayerProps {
  mapRef: React.MutableRefObject<maplibregl.Map | null>;
  avoidancePlan?: AvoidancePlanData | null;
  visible: boolean;
}

const SOURCE_ID = 'avoidance-route';
const LAYER_ID = 'avoidance-route-line';

function buildGeoJSON(avoidancePlan: AvoidancePlanData | null): GeoJSON.FeatureCollection {
  const waypoints = avoidancePlan?.waypoints ?? [];
  if (waypoints.length < 2) {
    return { type: 'FeatureCollection', features: [] };
  }

  return {
    type: 'FeatureCollection',
    features: [{
      type: 'Feature',
      geometry: {
        type: 'LineString',
        coordinates: waypoints.map((wp) => [wp.lon, wp.lat]),
      },
      properties: {
        status: avoidancePlan?.status ?? '',
        confidence: avoidancePlan?.confidence ?? 0,
        horizon_s: avoidancePlan?.horizonS ?? 0,
        rationale: avoidancePlan?.rationale ?? '',
      },
    }],
  };
}

export const AvoidanceRouteLayer: React.FC<AvoidanceRouteLayerProps> = React.memo(({
  mapRef,
  avoidancePlan,
  visible,
}) => {
  const addedRef = useRef(false);
  const [storeAvoidancePlan, setStoreAvoidancePlan] = useState(
    () => useTelemetryStore.getState().avoidancePlan
  );
  const resolvedPlan = avoidancePlan === undefined ? storeAvoidancePlan : avoidancePlan;

  useEffect(() => (
    useTelemetryStore.subscribe((state) => {
      setStoreAvoidancePlan(state.avoidancePlan);
    })
  ), []);

  useEffect(() => {
    const map = mapRef.current;
    if (!map) return;

    function setup() {
      if (!map) return;
      const data = buildGeoJSON(resolvedPlan);
      const opacity = visible && data.features.length > 0 ? 0.95 : 0;

      if (!addedRef.current) {
        map.addSource(SOURCE_ID, { type: 'geojson', data });
        map.addLayer({
          id: LAYER_ID,
          type: 'line',
          source: SOURCE_ID,
          layout: { 'line-cap': 'round', 'line-join': 'round' },
          paint: {
            'line-color': '#fb923c',
            'line-width': 3,
            'line-opacity': opacity,
            'line-dasharray': [4, 2],
          },
        });
        addedRef.current = true;
      } else {
        (map.getSource(SOURCE_ID) as any)?.setData(data);
        if (map.getLayer(LAYER_ID)) {
          map.setPaintProperty(LAYER_ID, 'line-opacity', opacity);
        }
      }
    }

    if (!map.isStyleLoaded()) {
      map.once('style.load', setup);
    } else {
      setup();
    }
  }, [mapRef, resolvedPlan, visible]);

  return null;
});

AvoidanceRouteLayer.displayName = 'AvoidanceRouteLayer';
