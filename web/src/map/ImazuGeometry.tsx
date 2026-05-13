import React, { useEffect, useRef } from 'react';
import type maplibregl from 'maplibre-gl';

interface ImazuGeometryProps {
  mapRef: React.MutableRefObject<maplibregl.Map | null>;
  geometry: GeoJSON.FeatureCollection | null;
}

const SOURCE_ID = 'imazu-geometry';
const LAYER_LINE = 'imazu-geometry-line';
const LAYER_LABEL = 'imazu-geometry-label';

export const ImazuGeometry: React.FC<ImazuGeometryProps> = React.memo(({ mapRef, geometry }) => {
  const addedRef = useRef(false);

  useEffect(() => {
    const map = mapRef.current;
    if (!map) return;
    if (!map.isStyleLoaded()) {
      const onLoad = () => {
        if (geometry) addOrUpdate(map, geometry);
      };
      map.once('style.load', onLoad);
      return;
    }
    addOrUpdate(map, geometry);
  }, [mapRef, geometry]);

  function addOrUpdate(map: maplibregl.Map, geo: GeoJSON.FeatureCollection | null) {
    if (!addedRef.current) {
      map.addSource(SOURCE_ID, {
        type: 'geojson',
        data: geo ?? { type: 'FeatureCollection', features: [] },
      });
      map.addLayer({
        id: LAYER_LINE,
        type: 'line',
        source: SOURCE_ID,
        paint: {
          'line-color': ['case', ['==', ['get', 'type'], 'trajectory'], '#f87171', '#2dd4bf'],
          'line-width': 1.5,
          'line-dasharray': [4, 3],
          'line-opacity': 0.5,
        },
      });
      map.addLayer({
        id: LAYER_LABEL,
        type: 'symbol',
        source: SOURCE_ID,
        layout: {
          'text-field': ['get', 'title'],
          'text-font': ['Open Sans Regular'],
          'text-size': 10,
          'text-offset': [0, -1.2],
          'text-anchor': 'bottom',
        },
        paint: {
          'text-color': 'var(--c-phos)',
          'text-opacity': 0.6,
        },
      });
      addedRef.current = true;
    } else if (geo) {
      (map.getSource(SOURCE_ID) as any)?.setData(geo);
    }
  }

  return null;
});
ImazuGeometry.displayName = 'ImazuGeometry';
