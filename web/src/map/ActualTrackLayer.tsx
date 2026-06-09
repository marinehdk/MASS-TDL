import React, { useEffect, useRef } from 'react';
import type maplibregl from 'maplibre-gl';

interface Props {
  mapRef: React.MutableRefObject<maplibregl.Map | null>;
  trail: [number, number][];  // [lon, lat] pairs
  visible: boolean;
}
const SRC = 'actual-track-src';
const LYR = 'actual-track-line';

export const ActualTrackLayer: React.FC<Props> = React.memo(({ mapRef, trail, visible }) => {
  const added = useRef(false);
  useEffect(() => {
    const map = mapRef.current;
    if (!map) return;
    const opacity = visible && trail.length >= 2 ? 1 : 0;
    const data = {
      type: 'FeatureCollection',
      features: [{ type: 'Feature',
        geometry: { type: 'LineString', coordinates: trail },
        properties: {} }],
    };
    function setup() {
      if (!map) return;
      if (!added.current) {
        map.addSource(SRC, { type: 'geojson', data: data as any });
        map.addLayer({
          id: LYR, type: 'line', source: SRC,
          layout: { 'line-cap': 'round', 'line-join': 'round' },
          paint: { 'line-color': '#0ea5e9', 'line-width': 3, 'line-opacity': opacity },
        });
        added.current = true;
      } else {
        (map.getSource(SRC) as any)?.setData(data);
        if (map.getLayer(LYR)) map.setPaintProperty(LYR, 'line-opacity', opacity);
      }
    }
    if (!map.isStyleLoaded()) map.once('style.load', setup);
    else setup();
  }, [mapRef, trail, visible]);
  return null;
});
