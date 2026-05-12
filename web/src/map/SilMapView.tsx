import { useEffect, useRef } from 'react';
import maplibregl from 'maplibre-gl';
import 'maplibre-gl/dist/maplibre-gl.css';
import { s57Source, s57Layers } from './layers';

interface SilMapViewProps {
  followOwnShip?: boolean;
  viewMode?: 'captain' | 'god';
}

export function SilMapView({ followOwnShip = true, viewMode = 'captain' }: SilMapViewProps) {
  const mapContainer = useRef<HTMLDivElement>(null);
  const mapRef = useRef<maplibregl.Map | null>(null);

  useEffect(() => {
    if (!mapContainer.current || mapRef.current) return;

    const map = new maplibregl.Map({
      container: mapContainer.current,
      style: {
        version: 8,
        sources: { s57: s57Source as any },
        layers: s57Layers as any,
      },
      center: [10.4, 63.4], // Trondheim default
      zoom: 12,
    });

    map.addControl(new maplibregl.NavigationControl(), 'top-right');
    mapRef.current = map;

    return () => {
      map.remove();
      mapRef.current = null;
    };
  }, []);

  // Phase 2: add vessel markers via GeoJSON sources from telemetryStore

  return (
    <div
      ref={mapContainer}
      style={{ width: '100%', height: '100%' }}
      data-testid="sil-map-view"
    />
  );
}
