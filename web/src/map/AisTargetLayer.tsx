import React, { useEffect, useRef } from 'react';
import type maplibregl from 'maplibre-gl';
import type { AisTarget } from '../api/aisTwinApi';

interface AisTargetLayerProps {
  mapRef: React.MutableRefObject<maplibregl.Map | null>;
  targets: AisTarget[];
  visible: boolean;
}

const SOURCE_ID = 'ais-targets';
const CIRCLE_LAYER_ID = 'ais-targets-circle';
const LABEL_LAYER_ID = 'ais-targets-label';

export function buildAisTargetGeoJSON(targets: AisTarget[]): GeoJSON.FeatureCollection {
  const validTargets = (targets || []).filter(
    (target) => Number.isFinite(target.lat) && Number.isFinite(target.lon),
  );

  return {
    type: 'FeatureCollection',
    features: validTargets.map((target) => ({
      type: 'Feature',
      geometry: {
        type: 'Point',
        coordinates: [target.lon, target.lat],
      },
      properties: {
        target_id: target.target_id,
        sog_kn: target.sog_kn,
        cog_deg: target.cog_deg,
        heading_deg: target.heading_deg ?? null,
        label: `AIS ${target.target_id}`,
      },
    })),
  };
}

export const AisTargetLayer: React.FC<AisTargetLayerProps> = React.memo(({
  mapRef, targets, visible,
}) => {
  const addedRef = useRef(false);
  const fittedRef = useRef(false);

  useEffect(() => {
    const map = mapRef.current;
    if (!map) return;

    function setup() {
      if (!map) return;
      const data = buildAisTargetGeoJSON(targets);

      if (!addedRef.current) {
        map.addSource(SOURCE_ID, { type: 'geojson', data });
        map.addLayer({
          id: CIRCLE_LAYER_ID,
          type: 'circle',
          source: SOURCE_ID,
          paint: {
            'circle-radius': 5,
            'circle-color': '#38bdf8',
            'circle-opacity': visible ? 0.9 : 0,
            'circle-stroke-color': '#0f172a',
            'circle-stroke-width': 1,
          },
        });
        map.addLayer({
          id: LABEL_LAYER_ID,
          type: 'symbol',
          source: SOURCE_ID,
          layout: {
            'text-field': ['get', 'label'],
            'text-size': 11,
            'text-offset': [0, 1.2],
            'text-anchor': 'top',
          },
          paint: {
            'text-color': '#e0f2fe',
            'text-halo-color': '#0f172a',
            'text-halo-width': 1,
            'text-opacity': visible ? 0.9 : 0,
          },
        });
        addedRef.current = true;
      } else {
        (map.getSource(SOURCE_ID) as any)?.setData(data);
        (map as any).setPaintProperty?.(CIRCLE_LAYER_ID, 'circle-opacity', visible ? 0.9 : 0);
        (map as any).setPaintProperty?.(LABEL_LAYER_ID, 'text-opacity', visible ? 0.9 : 0);
      }

      if (!fittedRef.current && visible && data.features.length > 0) {
        const coordinates = data.features.map((feature) => {
          const geometry = feature.geometry as GeoJSON.Point;
          return geometry.coordinates;
        });
        const lons = coordinates.map(([lon]) => lon);
        const lats = coordinates.map(([, lat]) => lat);
        map.fitBounds(
          [
            [Math.min(...lons), Math.min(...lats)],
            [Math.max(...lons), Math.max(...lats)],
          ],
          { padding: 140, maxZoom: 10, duration: 0 },
        );
        fittedRef.current = true;
      }
    }

    if (!map.isStyleLoaded()) {
      map.once('style.load', setup);
    } else {
      setup();
    }
  }, [mapRef, targets, visible]);

  return null;
});
AisTargetLayer.displayName = 'AisTargetLayer';
