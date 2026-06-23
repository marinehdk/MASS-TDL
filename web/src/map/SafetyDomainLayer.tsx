import React, { useCallback, useEffect, useMemo, useState } from 'react';
import { createPortal } from 'react-dom';
import type maplibregl from 'maplibre-gl';
import type { OwnShipState } from '../types';
import type { ThreatRiskStateData } from '../store/telemetryStore';

interface SafetyDomainLayerProps {
  mapRef: React.MutableRefObject<maplibregl.Map | null>;
  ownShip: OwnShipState | null;
  visible: boolean;
  observationNm?: number;
  actionNm?: number;
  criticalNm?: number;
  threatState?: ThreatRiskStateData | null;
}

const SOURCE_ID = 'safety-domain';
const NM_TO_DEG = 1 / 60;
const LAYER_IDS = [
  'safety-observation-fill',
  'safety-action-fill',
  'safety-critical-fill',
  'safety-colregs-fill',
  'safety-colregs-line',
  'safety-observation',
  'safety-action',
  'safety-critical',
  'safety-colregs-label',
] as const;

const METRICS = {
  observation: { fore: 2.2, starboard: 1.5, port: 1.2, aft: 0.8 },
  action: { fore: 1.2, starboard: 0.8, port: 0.6, aft: 0.4 },
  critical: { fore: 0.3, starboard: 0.25, port: 0.18, aft: 0.10 },
};

type Tier = 'observation' | 'action' | 'critical' | 'warning' | 'danger';
type Sector = 'HEAD-ON' | 'GIVE-WAY' | 'STAND-ON' | 'OVERTAKING';
type PolygonFeature = GeoJSON.Feature<GeoJSON.Polygon, { tier: Tier | 'colregs-sector'; sector?: Sector }>;
type LabelFeature = GeoJSON.Feature<GeoJSON.Point, { tier: 'colregs-label'; label: Sector }>;

const DOMAIN_STYLE: Record<Tier, { fill: string; fillOpacity: number; stroke: string; strokeOpacity: number; width: number; dash?: string }> = {
  observation: { fill: '#94a3b8', fillOpacity: 0.06, stroke: '#94a3b8', strokeOpacity: 0.5, width: 1, dash: '4 4' },
  action: { fill: '#fbbf24', fillOpacity: 0.08, stroke: '#fbbf24', strokeOpacity: 0.72, width: 1.5, dash: '2 2' },
  critical: { fill: '#f87171', fillOpacity: 0.14, stroke: '#f87171', strokeOpacity: 0.9, width: 2 },
  warning: { fill: '#fbbf24', fillOpacity: 0.08, stroke: '#fbbf24', strokeOpacity: 0.72, width: 1.5, dash: '2 2' },
  danger: { fill: '#f87171', fillOpacity: 0.14, stroke: '#f87171', strokeOpacity: 0.9, width: 2 },
};

const SECTOR_STYLE: Record<Sector, { fill: string; fillOpacity: number; stroke: string }> = {
  'HEAD-ON': { fill: '#f87171', fillOpacity: 0.18, stroke: '#f87171' },
  'GIVE-WAY': { fill: '#60a5fa', fillOpacity: 0.14, stroke: '#60a5fa' },
  'STAND-ON': { fill: '#3b82f6', fillOpacity: 0.14, stroke: '#3b82f6' },
  OVERTAKING: { fill: '#a3e635', fillOpacity: 0.12, stroke: '#a3e635' },
};

function domainRadiusAt(alpha: number, fore: number, starboard: number, port: number, aft: number) {
  const deg = (((alpha * 180) / Math.PI) + 360) % 360;
  if (deg >= 0 && deg < 90) {
    const t = deg / 90;
    return (1 - t) * fore + t * starboard;
  }
  if (deg >= 90 && deg < 180) {
    const t = (deg - 90) / 90;
    return (1 - t) * starboard + t * aft;
  }
  if (deg >= 180 && deg < 270) {
    const t = (deg - 180) / 90;
    return (1 - t) * aft + t * port;
  }
  const t = (deg - 270) / 90;
  return (1 - t) * port + t * fore;
}

function offsetPoint(
  lon: number,
  lat: number,
  headingRad: number,
  alpha: number,
  radiusNm: number,
): [number, number] {
  const rDeg = radiusNm * NM_TO_DEG;
  const psi = headingRad + alpha;
  const lonCorrection = rDeg / Math.cos((lat * Math.PI) / 180);
  return [
    lon + lonCorrection * Math.sin(psi),
    lat + rDeg * Math.cos(psi),
  ];
}

function asymmetricDomainFeature(
  lon: number,
  lat: number,
  headingRad: number,
  fore: number,
  starboard: number,
  port: number,
  aft: number,
  tier: Tier,
): PolygonFeature {
  const steps = 64;
  const coords: [number, number][] = [];
  for (let i = 0; i <= steps; i++) {
    const alpha = (i / steps) * 2 * Math.PI;
    coords.push(offsetPoint(lon, lat, headingRad, alpha, domainRadiusAt(alpha, fore, starboard, port, aft)));
  }
  return { type: 'Feature', geometry: { type: 'Polygon', coordinates: [coords] }, properties: { tier } };
}

function asymmetricSectorFeature(
  lon: number,
  lat: number,
  headingRad: number,
  startDeg: number,
  endDeg: number,
  fore: number,
  starboard: number,
  port: number,
  aft: number,
  sector: Sector,
): PolygonFeature {
  const steps = 32;
  const coords: [number, number][] = [[lon, lat]];
  const startRad = (startDeg * Math.PI) / 180;
  const endRad = (endDeg * Math.PI) / 180;

  for (let i = 0; i <= steps; i++) {
    const alpha = startRad + (i / steps) * (endRad - startRad);
    coords.push(offsetPoint(lon, lat, headingRad, alpha, domainRadiusAt(alpha, fore, starboard, port, aft)));
  }

  coords.push([lon, lat]);
  return { type: 'Feature', geometry: { type: 'Polygon', coordinates: [coords] }, properties: { tier: 'colregs-sector', sector } };
}

function sectorLabelFeature(
  lon: number,
  lat: number,
  headingRad: number,
  startDeg: number,
  endDeg: number,
  fore: number,
  starboard: number,
  port: number,
  aft: number,
  label: Sector,
): LabelFeature {
  const midRad = ((startDeg + endDeg) / 2) * (Math.PI / 180);
  const radiusNm = domainRadiusAt(midRad, fore, starboard, port, aft) * 0.62;
  return {
    type: 'Feature',
    geometry: { type: 'Point', coordinates: offsetPoint(lon, lat, headingRad, midRad, radiusNm) },
    properties: { tier: 'colregs-label', label },
  };
}

function cleanupMapLibreSafetyLayers(map: maplibregl.Map) {
  for (const layerId of [...LAYER_IDS].reverse()) {
    try {
      if (map.getLayer(layerId)) map.removeLayer(layerId);
    } catch {
      // Ignore stale style-layer cleanup races during map teardown.
    }
  }
  try {
    if (map.getSource(SOURCE_ID)) map.removeSource(SOURCE_ID);
  } catch {
    // Ignore stale source cleanup races during map teardown.
  }
}

export const SafetyDomainLayer: React.FC<SafetyDomainLayerProps> = React.memo(({
  mapRef,
  ownShip,
  visible,
  observationNm,
  actionNm,
  criticalNm,
  threatState,
}) => {
  const [projected, setProjected] = useState<{
    container: HTMLElement;
    width: number;
    height: number;
    domains: Array<{ tier: Tier; points: string }>;
    sectors: Array<{ sector: Sector; points: string }>;
    labels: Array<{ label: Sector; x: number; y: number }>;
  } | null>(null);
  const hasBackendAxes = Boolean(threatState?.warningAxes && threatState?.dangerAxes);

  const metrics = useMemo(() => {
    if (hasBackendAxes && threatState?.warningAxes && threatState?.dangerAxes) {
      const mToNm = (valueM: number) => valueM / 1852.0;
      return {
        warning: {
          fore: mToNm(threatState.warningAxes.forwardM),
          starboard: mToNm(threatState.warningAxes.starboardM),
          port: mToNm(threatState.warningAxes.portM),
          aft: mToNm(threatState.warningAxes.asternM),
        },
        danger: {
          fore: mToNm(threatState.dangerAxes.forwardM),
          starboard: mToNm(threatState.dangerAxes.starboardM),
          port: mToNm(threatState.dangerAxes.portM),
          aft: mToNm(threatState.dangerAxes.asternM),
        },
      };
    }

    const scaleObs = observationNm != null ? observationNm / 2.0 : 1.0;
    const scaleAct = actionNm != null ? actionNm / 1.0 : 1.0;
    const scaleCrit = criticalNm != null ? criticalNm / 0.3 : 1.0;

    return {
      observation: {
        fore: METRICS.observation.fore * scaleObs,
        starboard: METRICS.observation.starboard * scaleObs,
        port: METRICS.observation.port * scaleObs,
        aft: METRICS.observation.aft * scaleObs,
      },
      action: {
        fore: METRICS.action.fore * scaleAct,
        starboard: METRICS.action.starboard * scaleAct,
        port: METRICS.action.port * scaleAct,
        aft: METRICS.action.aft * scaleAct,
      },
      critical: {
        fore: METRICS.critical.fore * scaleCrit,
        starboard: METRICS.critical.starboard * scaleCrit,
        port: METRICS.critical.port * scaleCrit,
        aft: METRICS.critical.aft * scaleCrit,
      },
    };
  }, [observationNm, actionNm, criticalNm, hasBackendAxes, threatState]);

  const buildFeatures = useCallback(() => {
    if (!ownShip?.pose) return null;
    const lon = ownShip.pose.lon;
    const lat = ownShip.pose.lat;
    if (typeof lon !== 'number' || typeof lat !== 'number') return null;

    const headingRad = ownShip.pose.heading ?? 0;
    const backendMetrics = 'warning' in metrics && metrics.warning && metrics.danger
      ? { warning: metrics.warning, danger: metrics.danger }
      : null;
    const legacyMetrics = !backendMetrics && 'observation' in metrics && metrics.observation && metrics.action && metrics.critical
      ? { observation: metrics.observation, action: metrics.action, critical: metrics.critical }
      : null;
    if (!backendMetrics && !legacyMetrics) return null;

    const domainFeatures: PolygonFeature[] = backendMetrics
      ? [
        asymmetricDomainFeature(lon, lat, headingRad, backendMetrics.warning.fore, backendMetrics.warning.starboard, backendMetrics.warning.port, backendMetrics.warning.aft, 'warning'),
        asymmetricDomainFeature(lon, lat, headingRad, backendMetrics.danger.fore, backendMetrics.danger.starboard, backendMetrics.danger.port, backendMetrics.danger.aft, 'danger'),
      ]
      : [
        asymmetricDomainFeature(lon, lat, headingRad, legacyMetrics!.observation.fore, legacyMetrics!.observation.starboard, legacyMetrics!.observation.port, legacyMetrics!.observation.aft, 'observation'),
        asymmetricDomainFeature(lon, lat, headingRad, legacyMetrics!.action.fore, legacyMetrics!.action.starboard, legacyMetrics!.action.port, legacyMetrics!.action.aft, 'action'),
        asymmetricDomainFeature(lon, lat, headingRad, legacyMetrics!.critical.fore, legacyMetrics!.critical.starboard, legacyMetrics!.critical.port, legacyMetrics!.critical.aft, 'critical'),
      ];
    const sectorDomain = backendMetrics ? backendMetrics.warning : legacyMetrics!.action;
    const sectorFeatures: PolygonFeature[] = [
      asymmetricSectorFeature(lon, lat, headingRad, -6, 6, sectorDomain.fore, sectorDomain.starboard, sectorDomain.port, sectorDomain.aft, 'HEAD-ON'),
      asymmetricSectorFeature(lon, lat, headingRad, 6, 112.5, sectorDomain.fore, sectorDomain.starboard, sectorDomain.port, sectorDomain.aft, 'GIVE-WAY'),
      asymmetricSectorFeature(lon, lat, headingRad, 247.5, 354, sectorDomain.fore, sectorDomain.starboard, sectorDomain.port, sectorDomain.aft, 'STAND-ON'),
      asymmetricSectorFeature(lon, lat, headingRad, 112.5, 247.5, sectorDomain.fore, sectorDomain.starboard, sectorDomain.port, sectorDomain.aft, 'OVERTAKING'),
    ];
    const labels: LabelFeature[] = [
      sectorLabelFeature(lon, lat, headingRad, -6, 6, sectorDomain.fore, sectorDomain.starboard, sectorDomain.port, sectorDomain.aft, 'HEAD-ON'),
      sectorLabelFeature(lon, lat, headingRad, 6, 112.5, sectorDomain.fore, sectorDomain.starboard, sectorDomain.port, sectorDomain.aft, 'GIVE-WAY'),
      sectorLabelFeature(lon, lat, headingRad, 247.5, 354, sectorDomain.fore, sectorDomain.starboard, sectorDomain.port, sectorDomain.aft, 'STAND-ON'),
      sectorLabelFeature(lon, lat, headingRad, 112.5, 247.5, sectorDomain.fore, sectorDomain.starboard, sectorDomain.port, sectorDomain.aft, 'OVERTAKING'),
    ];

    return { domainFeatures, sectorFeatures, labels };
  }, [metrics, ownShip]);

  const updateProjection = useCallback(() => {
    const map = mapRef.current;
    if (!map || !visible) {
      setProjected(null);
      return;
    }

    cleanupMapLibreSafetyLayers(map);

    const features = buildFeatures();
    if (!features) {
      setProjected(null);
      return;
    }

    const container = map.getContainer();
    const toPointString = (coords: [number, number][]) => coords.map(([lon, lat]) => {
      const p = map.project([lon, lat]);
      return `${p.x.toFixed(1)},${p.y.toFixed(1)}`;
    }).join(' ');
    const toPoint = ([lon, lat]: [number, number]) => {
      const p = map.project([lon, lat]);
      return { x: p.x, y: p.y };
    };

    setProjected({
      container,
      width: container.clientWidth,
      height: container.clientHeight,
      domains: features.domainFeatures.map((feature) => ({
        tier: feature.properties.tier as Tier,
        points: toPointString(feature.geometry.coordinates[0] as [number, number][]),
      })),
      sectors: features.sectorFeatures.map((feature) => ({
        sector: feature.properties.sector!,
        points: toPointString(feature.geometry.coordinates[0] as [number, number][]),
      })),
      labels: features.labels.map((feature) => ({
        label: feature.properties.label,
        ...toPoint(feature.geometry.coordinates as [number, number]),
      })),
    });
  }, [buildFeatures, mapRef, visible]);

  useEffect(() => {
    const map = mapRef.current;
    if (!map) return;

    updateProjection();
    map.on('move', updateProjection);
    map.on('zoom', updateProjection);
    map.on('rotate', updateProjection);
    map.on('resize', updateProjection);
    map.on('styledata', updateProjection);
    map.on('idle', updateProjection);

    return () => {
      map.off('move', updateProjection);
      map.off('zoom', updateProjection);
      map.off('rotate', updateProjection);
      map.off('resize', updateProjection);
      map.off('styledata', updateProjection);
      map.off('idle', updateProjection);
    };
  }, [mapRef, updateProjection]);

  useEffect(() => {
    updateProjection();
  }, [updateProjection]);

  if (!projected) return null;

  return createPortal(
    <>
      <style>{`
        .maplibregl-marker { z-index: 10 !important; }
        .safety-domain-svg { z-index: 4; }
      `}</style>
      <svg
        className="safety-domain-svg"
        data-testid="safety-domain-svg"
        width={projected.width}
        height={projected.height}
        style={{
          position: 'absolute',
          inset: 0,
          pointerEvents: 'none',
          overflow: 'visible',
        }}
      >
        {projected.domains.map(({ tier, points }) => {
          const style = DOMAIN_STYLE[tier];
          return (
            <polygon
              key={`${tier}-fill`}
              data-tier={tier}
              points={points}
              fill={style.fill}
              fillOpacity={style.fillOpacity}
              stroke="none"
            />
          );
        })}

        {projected.sectors.map(({ sector, points }) => {
          const style = SECTOR_STYLE[sector];
          return (
            <polygon
              key={sector}
              points={points}
              fill={style.fill}
              fillOpacity={style.fillOpacity}
              stroke={style.stroke}
              strokeOpacity={0.6}
              strokeWidth={1.25}
            />
          );
        })}

        {projected.domains.map(({ tier, points }) => {
          const style = DOMAIN_STYLE[tier];
          return (
            <polyline
              key={`${tier}-line`}
              data-tier={tier}
              points={points}
              fill="none"
              stroke={style.stroke}
              strokeOpacity={style.strokeOpacity}
              strokeWidth={style.width}
              strokeDasharray={style.dash}
            />
          );
        })}

        {projected.labels.map(({ label, x, y }) => (
          <text
            key={label}
            x={x}
            y={y}
            textAnchor="middle"
            dominantBaseline="middle"
            fill="#e2e8f0"
            fillOpacity={0.85}
            stroke="#070c13"
            strokeWidth={3}
            paintOrder="stroke"
            fontFamily="var(--f-mono)"
            fontSize={10}
          >
            {label}
          </text>
        ))}
      </svg>
    </>,
    projected.container,
  );
});

SafetyDomainLayer.displayName = 'SafetyDomainLayer';
