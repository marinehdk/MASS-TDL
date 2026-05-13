import { useEffect, useRef } from 'react';
import type maplibregl from 'maplibre-gl';
import { useMapStore } from '../store';

/**
 * Syncs MapLibre 'moveend' events → useMapStore.viewport so the chart
 * centre/zoom/bearing/pitch survive unmount/remount across 4-screen navigation.
 *
 * Usage inside SilMapView:
 *   useMapPersistence(mapRef, viewMode);
 */
export function useMapPersistence(
  mapRef: React.MutableRefObject<maplibregl.Map | null>,
  viewMode: 'captain' | 'god',
) {
  const setViewport = useMapStore((s) => s.setViewport);
  const debounceRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  useEffect(() => {
    const map = mapRef.current;
    if (!map) return;

    const handler = () => {
      if (viewMode === 'god') {
        // God view: save all viewport state
        if (debounceRef.current) clearTimeout(debounceRef.current);
        debounceRef.current = setTimeout(() => {
          const c = map.getCenter();
          setViewport({
            center: [c.lng, c.lat],
            zoom: map.getZoom(),
            bearing: map.getBearing(),
            pitch: map.getPitch(),
          });
        }, 200);
      }
      // Captain view: viewport is driven by followOwnShip so don't save
    };

    map.on('moveend', handler);
    return () => {
      map.off('moveend', handler);
      if (debounceRef.current) clearTimeout(debounceRef.current);
    };
  }, [mapRef, viewMode, setViewport]);
}
