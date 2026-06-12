import React, { useEffect, useRef, useState } from 'react';
import type maplibregl from 'maplibre-gl';
import type { AisTarget } from '../api/aisTwinApi';

interface AisTargetLayerProps {
  mapRef: React.MutableRefObject<maplibregl.Map | null>;
  targets: AisTarget[];
  visible: boolean;
}

const SOURCE_ID = 'ais-targets';
const CIRCLE_LAYER_ID = 'ais-targets-circle';
const ARROW_LAYER_ID = 'ais-targets-arrow';

interface PopupInfo {
  x: number;
  y: number;
  target: AisTarget & {
    marker_color?: string;
    label?: string;
  };
}

const SHIP_TYPE_COLOR: Record<string, string> = {
  cargo: '#34d399',
  tanker: '#ef4444',
  passenger: '#60a5fa',
  fishing: '#22d3ee',
  tug: '#f59e0b',
  service: '#a78bfa',
  other: '#06b6d4',
};

function normalizeShipType(value?: string | null): string {
  const text = (value || 'other').toLowerCase();
  if (text.includes('cargo')) return 'cargo';
  if (text.includes('tanker')) return 'tanker';
  if (text.includes('passenger')) return 'passenger';
  if (text.includes('fishing')) return 'fishing';
  if (text.includes('tug')) return 'tug';
  if (text.includes('service')) return 'service';
  return 'other';
}

function markerColor(target: AisTarget): string {
  return SHIP_TYPE_COLOR[normalizeShipType(target.ship_type)] ?? SHIP_TYPE_COLOR.other;
}

function markerKind(target: AisTarget): 'arrow' | 'circle' {
  const course = target.heading_deg ?? target.cog_deg;
  const isMoving = Number.isFinite(target.sog_kn) && Number(target.sog_kn) > 0.5 && Number.isFinite(course);
  return isMoving ? 'arrow' : 'circle';
}

function markerSize(target: AisTarget): number {
  const length = Number(target.vessel_length_m);
  if (Number.isFinite(length) && length > 0) {
    return Math.max(12, Math.min(24, 10 + length / 18));
  }
  const speed = Number(target.sog_kn);
  if (Number.isFinite(speed) && speed > 0) {
    return Math.max(11, Math.min(20, 10 + speed / 2));
  }
  return 8;
}

function displayName(target: Partial<AisTarget>): string {
  return target.ship_name || `AIS ${target.target_id}`;
}

function titleCase(value?: string | null): string {
  if (!value) return 'Unknown';
  return value.slice(0, 1).toUpperCase() + value.slice(1).replace(/_/g, ' ');
}

function speedCourseText(target: Partial<AisTarget>): string {
  const speed = Number.isFinite(target.sog_kn) ? `${Number(target.sog_kn).toFixed(1)} kn` : 'N/A';
  const course = Number.isFinite(target.cog_deg) ? `${Number(target.cog_deg).toFixed(0)} deg` : 'N/A';
  return `${speed} / ${course}`;
}

function receivedText(value?: string | null): string {
  if (!value) return 'N/A';
  const received = Date.parse(value);
  if (!Number.isFinite(received)) return value;
  const ageMin = Math.max(0, Math.round((Date.now() - received) / 60000));
  if (ageMin < 60) return `${ageMin} min ago`;
  const hours = Math.floor(ageMin / 60);
  const mins = ageMin % 60;
  return `${hours} h ${mins} min ago`;
}

function featureTarget(feature: GeoJSON.Feature | undefined): PopupInfo['target'] | null {
  const props = feature?.properties as any;
  if (!props) return null;
  return {
    target_id: Number(props.target_id),
    lat: Number(props.lat),
    lon: Number(props.lon),
    sog_kn: props.sog_kn == null ? null : Number(props.sog_kn),
    cog_deg: props.cog_deg == null ? null : Number(props.cog_deg),
    heading_deg: props.heading_deg == null ? null : Number(props.heading_deg),
    source_sensor: 'ais',
    ship_name: props.ship_name || null,
    ship_type: props.ship_type || null,
    destination: props.destination || null,
    nav_status: props.nav_status || null,
    received_at_utc: props.received_at_utc || null,
    vessel_length_m: props.vessel_length_m == null ? null : Number(props.vessel_length_m),
    vessel_beam_m: props.vessel_beam_m == null ? null : Number(props.vessel_beam_m),
    marker_color: props.marker_color,
    label: props.label,
  };
}

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
        lat: target.lat,
        lon: target.lon,
        sog_kn: target.sog_kn,
        cog_deg: target.cog_deg,
        heading_deg: target.heading_deg ?? null,
        ship_name: target.ship_name ?? null,
        ship_type: normalizeShipType(target.ship_type),
        destination: target.destination ?? null,
        nav_status: target.nav_status ?? null,
        received_at_utc: target.received_at_utc ?? null,
        vessel_length_m: target.vessel_length_m ?? null,
        vessel_beam_m: target.vessel_beam_m ?? null,
        label: displayName(target),
        marker_kind: markerKind(target),
        marker_color: markerColor(target),
        marker_size: markerSize(target),
        heading_display_deg: target.heading_deg ?? target.cog_deg ?? 0,
      },
    })),
  };
}

export const AisTargetLayer: React.FC<AisTargetLayerProps> = React.memo(({
  mapRef, targets, visible,
}) => {
  const addedRef = useRef(false);
  const fittedRef = useRef(false);
  const [hoverInfo, setHoverInfo] = useState<PopupInfo | null>(null);
  const [selectedInfo, setSelectedInfo] = useState<PopupInfo | null>(null);

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
          filter: ['==', ['get', 'marker_kind'], 'circle'],
          paint: {
            'circle-radius': ['get', 'marker_size'],
            'circle-color': ['get', 'marker_color'],
            'circle-opacity': visible ? 0.9 : 0,
            'circle-stroke-color': '#0f172a',
            'circle-stroke-width': 2,
          },
        });
        map.addLayer({
          id: ARROW_LAYER_ID,
          type: 'symbol',
          source: SOURCE_ID,
          filter: ['==', ['get', 'marker_kind'], 'arrow'],
          layout: {
            'text-field': '▲',
            'text-size': ['get', 'marker_size'],
            'text-rotate': ['get', 'heading_display_deg'],
            'text-rotation-alignment': 'map',
            'text-allow-overlap': true,
            'text-ignore-placement': true,
          },
          paint: {
            'text-color': ['get', 'marker_color'],
            'text-halo-color': '#0f172a',
            'text-halo-width': 1,
            'text-opacity': visible ? 0.9 : 0,
          },
        });
        addedRef.current = true;
      } else {
        (map.getSource(SOURCE_ID) as any)?.setData(data);
        (map as any).setPaintProperty?.(CIRCLE_LAYER_ID, 'circle-opacity', visible ? 0.9 : 0);
        (map as any).setPaintProperty?.(ARROW_LAYER_ID, 'text-opacity', visible ? 0.9 : 0);
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

  useEffect(() => {
    const map = mapRef.current as any;
    if (!map?.on || !map?.off) return;

    const interactiveLayers = [ARROW_LAYER_ID, CIRCLE_LAYER_ID];
    const handleMove = (event: any) => {
      if (!visible) return;
      const target = featureTarget(event.features?.[0]);
      if (!target) return;
      map.getCanvas?.().style && (map.getCanvas().style.cursor = 'pointer');
      setHoverInfo({ x: event.point?.x ?? 0, y: event.point?.y ?? 0, target });
    };
    const handleLeave = () => {
      map.getCanvas?.().style && (map.getCanvas().style.cursor = '');
      setHoverInfo(null);
    };
    const handleClick = (event: any) => {
      if (!visible) return;
      const target = featureTarget(event.features?.[0]);
      if (!target) return;
      setSelectedInfo({ x: event.point?.x ?? 0, y: event.point?.y ?? 0, target });
    };

    for (const layer of interactiveLayers) {
      map.on('mousemove', layer, handleMove);
      map.on('mouseleave', layer, handleLeave);
      map.on('click', layer, handleClick);
    }

    return () => {
      for (const layer of interactiveLayers) {
        map.off('mousemove', layer, handleMove);
        map.off('mouseleave', layer, handleLeave);
        map.off('click', layer, handleClick);
      }
    };
  }, [mapRef, visible]);

  useEffect(() => {
    if (!visible) {
      setHoverInfo(null);
      setSelectedInfo(null);
    }
  }, [visible]);

  return (
    <div style={{ position: 'absolute', inset: 0, pointerEvents: 'none', zIndex: 25 }}>
      {hoverInfo && !selectedInfo && (
        <div style={{
          position: 'absolute',
          left: hoverInfo.x + 14,
          top: Math.max(8, hoverInfo.y - 70),
          minWidth: 230,
          padding: '8px 10px',
          borderRadius: 4,
          background: 'rgba(226, 232, 240, 0.94)',
          color: '#111827',
          boxShadow: '0 8px 24px rgba(0,0,0,0.35)',
          fontFamily: 'var(--f-mono)',
          fontSize: 12,
          lineHeight: 1.35,
        }}>
          <div style={{ fontWeight: 900 }}>{displayName(hoverInfo.target)} at {speedCourseText(hoverInfo.target)}</div>
          <div>Destination: <b>{hoverInfo.target.destination || 'N/A'}</b></div>
          <div>Position received: <b>{receivedText(hoverInfo.target.received_at_utc)}</b></div>
        </div>
      )}
      {selectedInfo && (
        <div style={{
          position: 'absolute',
          left: Math.max(12, selectedInfo.x + 16),
          top: Math.max(12, selectedInfo.y - 48),
          width: 300,
          borderRadius: 6,
          background: 'rgba(248, 250, 252, 0.98)',
          color: '#111827',
          boxShadow: '0 16px 40px rgba(0,0,0,0.42)',
          pointerEvents: 'auto',
          overflow: 'hidden',
          fontFamily: 'var(--f-mono)',
        }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 8, padding: '10px 12px', borderBottom: '1px solid #d1d5db' }}>
            <span style={{ width: 14, height: 14, borderRadius: 3, background: selectedInfo.target.marker_color || '#06b6d4' }} />
            <div style={{ flex: 1, minWidth: 0 }}>
              <div style={{ fontSize: 16, fontWeight: 900, whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>
                {displayName(selectedInfo.target)}
              </div>
              <div style={{ fontSize: 11, color: '#6b7280' }}>{titleCase(selectedInfo.target.ship_type)}</div>
            </div>
            <button
              type="button"
              aria-label="Close AIS details"
              onClick={() => setSelectedInfo(null)}
              style={{ border: 'none', background: 'transparent', color: '#64748b', cursor: 'pointer', fontSize: 18 }}
            >
              x
            </button>
          </div>
          <div style={{ padding: 12, display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 10, fontSize: 12 }}>
            <div>
              <div style={{ color: '#6b7280' }}>Destination</div>
              <b>{selectedInfo.target.destination || 'N/A'}</b>
            </div>
            <div>
              <div style={{ color: '#6b7280' }}>Speed/Course</div>
              <b>{speedCourseText(selectedInfo.target)}</b>
            </div>
            <div>
              <div style={{ color: '#6b7280' }}>Nav status</div>
              <b>{selectedInfo.target.nav_status || 'N/A'}</b>
            </div>
            <div>
              <div style={{ color: '#6b7280' }}>Length</div>
              <b>{Number.isFinite(selectedInfo.target.vessel_length_m) ? `${selectedInfo.target.vessel_length_m} m` : 'N/A'}</b>
            </div>
            <div style={{ gridColumn: '1 / 3' }}>
              <div style={{ color: '#6b7280' }}>Received</div>
              <b>{receivedText(selectedInfo.target.received_at_utc)}</b>
            </div>
            <div style={{ gridColumn: '1 / 3', color: '#6b7280' }}>
              MMSI: {selectedInfo.target.target_id}
            </div>
          </div>
          <div style={{ padding: '8px 12px 12px' }}>
            <button
              type="button"
              style={{
                height: 32,
                padding: '0 12px',
                border: 'none',
                borderRadius: 4,
                background: '#2563eb',
                color: '#fff',
                fontWeight: 800,
                cursor: 'pointer',
              }}
            >
              Vessel details
            </button>
          </div>
        </div>
      )}
    </div>
  );
});
AisTargetLayer.displayName = 'AisTargetLayer';
