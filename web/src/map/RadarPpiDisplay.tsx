import React, { useMemo, useState } from 'react';
import type { OwnShipState } from '../types';
import type { TargetVesselState } from '../types';
import { computeCpaTcpa } from '../screens/shared/navMath';

interface RadarPpiDisplayProps {
  ownShip: OwnShipState | null;
  targets: TargetVesselState[];
  relativeMode: boolean; // true = Heading-up, false = North-up
  size?: number;
  maxRangeNM?: number;
  rangeRingsNM?: number[];
}

type TargetWithCpa = TargetVesselState & {
  cpaM?: number;
  tcpaS?: number;
};

export const RadarPpiDisplay: React.FC<RadarPpiDisplayProps> = ({
  ownShip,
  targets,
  relativeMode,
  size = 240,
  maxRangeNM = 3.0,
  rangeRingsNM = [1, 2, 3],
}) => {
  const [hoveredTarget, setHoveredTarget] = useState<{
    mmsi: number;
    rangeNM: number;
    bearingDeg: number;
    sog: number;
    cogDeg: number;
    px: number;
    py: number;
  } | null>(null);
  const cx = size / 2;
  const cy = size / 2;
  const radarRadius = size / 2 - 20;
  const markerScale = size / 240;
  const vesselDotRadius = Math.max(3, 3.2 * markerScale);
  const arrowTipOffset = vesselDotRadius + 7 * markerScale;
  const arrowBaseOffset = vesselDotRadius + 1.5 * markerScale;
  const arrowHalfWidth = Math.max(3, 3.4 * markerScale);
  const clampedRangeRingsNM = rangeRingsNM.filter((ring) => ring > 0 && ring <= maxRangeNM);

  const ownHeadingDeg = ownShip?.pose
    ? ((ownShip.pose.heading ?? 0) * 180) / Math.PI
    : 0;

  // Generate azimuth bearing dial scale ticks and numbers (000 to 330)
  const bearingDialElements = useMemo(() => {
    const elements = [];
    for (let angle = 0; angle < 360; angle += 5) {
      const rad = (angle * Math.PI) / 180;
      const sin = Math.sin(rad);
      const cos = Math.cos(rad);

      let rStart = radarRadius + 5;
      let rEnd = radarRadius + 8;
      let strokeColor = 'rgba(16, 185, 129, 0.3)';
      let strokeWidth = 0.5;

      if (angle % 30 === 0) {
        rStart = radarRadius;
        rEnd = radarRadius + 8;
        strokeColor = 'rgba(16, 185, 129, 0.85)';
        strokeWidth = 1.2;
      } else if (angle % 10 === 0) {
        rStart = radarRadius + 3;
        rEnd = radarRadius + 8;
        strokeColor = 'rgba(16, 185, 129, 0.5)';
        strokeWidth = 0.75;
      }

      elements.push(
        <line
          key={`tick-${angle}`}
          x1={cx + sin * rStart}
          y1={cy - cos * rStart}
          x2={cx + sin * rEnd}
          y2={cy - cos * rEnd}
          stroke={strokeColor}
          strokeWidth={strokeWidth}
        />
      );

      // Major numbers every 30 degrees
      if (angle % 30 === 0) {
        const textRadius = radarRadius + 14;
        const tx = cx + sin * textRadius;
        const ty = cy - cos * textRadius + 3.0; // Vertically center text
        const angleStr = angle.toString().padStart(3, '0');
        const isNorth = angle === 0;

        elements.push(
          <text
            key={`num-${angle}`}
            x={tx}
            y={ty}
            textAnchor="middle"
            fill={isNorth ? '#f87171' : 'rgba(16, 185, 129, 0.8)'}
            fontSize="8"
            fontWeight={isNorth ? 'bold' : 'normal'}
            fontFamily="var(--f-mono)"
            letterSpacing="0"
          >
            {angleStr}
          </text>
        );
      }
    }
    return elements;
  }, [cx, cy, radarRadius]);

  // Map and calculate relative target positions inside active radar grid
  const plottedTargets = useMemo(() => {
    if (!ownShip?.pose) return [];
    const ownPose = ownShip.pose;
    const ownLat = ownPose.lat;
    const ownLon = ownPose.lon;

    return targets
      .map((t) => {
        if (!t.pose) return null;
        const targetWithCpa = t as TargetWithCpa;
        const tgtLat = t.pose.lat;
        const tgtLon = t.pose.lon;

        // Cartesian distance difference
        const dLat = tgtLat - ownLat;
        const dLon = (tgtLon - ownLon) * Math.cos((ownLat * Math.PI) / 180);

        // Nautical Miles calculation: 1 deg lat = 60 NM
        const xNM = dLon * 60;
        const yNM = dLat * 60;

        const rangeNM = Math.sqrt(xNM * xNM + yNM * yNM);
        const bearingRad = Math.atan2(xNM, yNM);
        const bearingDeg = (bearingRad * 180 / Math.PI + 360) % 360;

        // Coordinate positioning on the SVG
        // North-up mode: uses true bearing
        // Heading-up mode: rotates coordinates by -ownHeadingDeg
        const angleDeg = relativeMode ? (bearingDeg - ownHeadingDeg) : bearingDeg;
        const alpha = (angleDeg * Math.PI) / 180;
        const distPx = (rangeNM / maxRangeNM) * radarRadius;

        const px = cx + Math.sin(alpha) * distPx;
        const py = cy - Math.cos(alpha) * distPx;

        // Relative course direction of targets
        // North-up mode: uses true course (COG)
        // Heading-up mode: rotates course vector by -ownHeadingDeg
        const cogRad = t.kinematics?.cog ?? 0;
        const cogDeg = (cogRad * 180) / Math.PI;
        const screenCogDeg = relativeMode ? (cogDeg - ownHeadingDeg) : cogDeg;

        const cpaMetrics = computeCpaTcpa({
          own: {
            lat: ownLat,
            lon: ownLon,
            sogMps: ownShip.kinematics?.sog ?? 0,
            cogRad: ownShip.kinematics?.cog ?? ownPose.heading ?? 0,
          },
          target: {
            lat: tgtLat,
            lon: tgtLon,
            sogMps: t.kinematics?.sog ?? 0,
            cogRad: t.kinematics?.cog ?? t.pose.heading ?? 0,
          },
        });
        const cpaXNM = cpaMetrics?.cpaPointNM.x ?? xNM;
        const cpaYNM = cpaMetrics?.cpaPointNM.y ?? yNM;
        const cpaRangeNM = Math.sqrt(cpaXNM * cpaXNM + cpaYNM * cpaYNM);
        const cpaBearingDeg = (Math.atan2(cpaXNM, cpaYNM) * 180 / Math.PI + 360) % 360;
        const cpaAngleDeg = relativeMode ? (cpaBearingDeg - ownHeadingDeg) : cpaBearingDeg;
        const cpaAlpha = (cpaAngleDeg * Math.PI) / 180;
        const cpaDistPx = (cpaRangeNM / maxRangeNM) * radarRadius;

        return {
          mmsi: t.mmsi,
          rangeNM,
          bearingDeg,
          sog: t.kinematics?.sog ?? 0,
          cogDeg,
          screenCogDeg,
          px,
          py,
          cpaM: targetWithCpa.cpaM ?? cpaMetrics?.cpaM,
          tcpaS: targetWithCpa.tcpaS ?? cpaMetrics?.tcpaS,
          cpaPx: cx + Math.sin(cpaAlpha) * cpaDistPx,
          cpaPy: cy - Math.cos(cpaAlpha) * cpaDistPx,
          cpaRangeNM,
        };
      })
      .filter((t): t is NonNullable<typeof t> => t !== null && t.rangeNM <= maxRangeNM);
  }, [ownShip, targets, relativeMode, ownHeadingDeg, cx, cy, maxRangeNM, radarRadius]);

  const nearestCpaTarget = useMemo(() => {
    return plottedTargets
      .filter((target) => target.cpaRangeNM <= maxRangeNM)
      .sort((a, b) => (a.cpaM ?? a.rangeNM * 1852) - (b.cpaM ?? b.rangeNM * 1852))[0];
  }, [plottedTargets, maxRangeNM]);

  // Handle own ship rotation in center
  const ownShipRotation = relativeMode ? 0 : ownHeadingDeg;

  return (
    <div
      data-testid="radar-ppi-display"
      style={{
        position: 'relative',
        width: size,
        height: size,
        background: 'rgba(5, 15, 10, 0.88)',
        backdropFilter: 'blur(8px)',
        border: '1.5px solid rgba(16, 185, 129, 0.4)',
        borderRadius: '50%',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        boxShadow: '0 8px 32px rgba(0, 0, 0, 0.7), inset 0 0 24px rgba(16, 185, 129, 0.08)',
        userSelect: 'none',
        overflow: 'hidden',
      }}
    >
      {/* Dynamic Keyframes injected locally */}
      <style>{`
        @keyframes radar-sweep {
          from { transform: rotate(0deg); }
          to { transform: rotate(360deg); }
        }
        @keyframes blip-glow {
          0% { opacity: 0.6; filter: drop-shadow(0 0 2px #10b981); }
          50% { opacity: 1; filter: drop-shadow(0 0 6px #10b981); }
          100% { opacity: 0.6; filter: drop-shadow(0 0 2px #10b981); }
        }
      `}</style>

      {/* Sweeping radar scanner overlay */}
      {ownShip && (
        <div
          style={{
            position: 'absolute',
            width: radarRadius * 2,
            height: radarRadius * 2,
            top: cy - radarRadius,
            left: cx - radarRadius,
            borderRadius: '50%',
            background: 'conic-gradient(from 0deg, transparent 180deg, rgba(16, 185, 129, 0.04) 240deg, rgba(16, 185, 129, 0.3) 360deg)',
            animation: 'radar-sweep 4s linear infinite',
            pointerEvents: 'none',
            zIndex: 5,
          }}
        >
          {/* Leading edge glow line */}
          <div
            style={{
              position: 'absolute',
              top: 0,
              left: '50%',
              width: 1.2,
              height: '50%',
              background: 'linear-gradient(to top, rgba(16, 185, 129, 0.05), rgba(16, 185, 129, 0.9))',
              transform: 'translateX(-50%)',
            }}
          />
        </div>
      )}

      {/* Main Radar Vector Screen */}
      <svg
        width={size}
        height={size}
        viewBox={`0 0 ${size} ${size}`}
        style={{
          position: 'absolute',
          inset: 0,
          zIndex: 10,
        }}
      >
        {/* Outer Bearing scale dial (rotates in Head-up relativeMode) */}
        <g
          transform={relativeMode ? `rotate(${-ownHeadingDeg} ${cx} ${cy})` : undefined}
          style={{ transition: 'transform 0.5s cubic-bezier(0.25, 1, 0.5, 1)' }}
        >
          {bearingDialElements}
        </g>

        {/* Concentric Range Rings (always static circular grids) */}
        {clampedRangeRingsNM.map((ringNM) => {
          const r = (ringNM / maxRangeNM) * radarRadius;
          const isEmergencyRing = ringNM <= 2;
          return (
            <g key={`ring-${ringNM}`}>
              <circle
                data-testid={`radar-range-ring-${ringNM}`}
                cx={cx}
                cy={cy}
                r={r}
                fill="none"
                stroke={isEmergencyRing ? 'rgba(248, 81, 73, 0.65)' : ringNM === maxRangeNM ? 'rgba(16, 185, 129, 0.3)' : 'rgba(16, 185, 129, 0.16)'}
                strokeWidth={ringNM === maxRangeNM ? 1 : 0.75}
                strokeDasharray={ringNM === maxRangeNM ? undefined : '3 4'}
              />
            </g>
          );
        })}

        {/* Radar Center Own Ship representation */}
        {ownShip ? (
          <g>
            {/* Own Ship marker: same point + heading vector grammar as target ships */}
            <g
              transform={!relativeMode ? `rotate(${ownHeadingDeg} ${cx} ${cy})` : undefined}
              style={{ transition: 'transform 0.5s cubic-bezier(0.25, 1, 0.5, 1)' }}
            >
              <polygon
                points={`${cx},${cy - arrowTipOffset} ${cx - arrowHalfWidth},${cy - arrowBaseOffset} ${cx + arrowHalfWidth},${cy - arrowBaseOffset}`}
                fill="#38bdf8"
                stroke="rgba(5, 15, 10, 0.72)"
                strokeWidth={0.45 * markerScale}
              />
              <circle
                cx={cx}
                cy={cy}
                r={vesselDotRadius}
                fill="#38bdf8"
                stroke="rgba(5, 15, 10, 0.95)"
                strokeWidth={0.8 * markerScale}
              />
            </g>
          </g>
        ) : (
          /* Awaiting Telemetry visual status indicator */
          <g>
            <text
              x={cx}
              y={cy - 15}
              textAnchor="middle"
              fill="#f87171"
              fontSize="9"
              fontWeight="bold"
              fontFamily="var(--f-mono)"
              style={{ letterSpacing: '0.12em', animation: 'blip-glow 2s infinite' }}
            >
              NO TELEMETRY
            </text>
            <text
              x={cx}
              y={cy + 5}
              textAnchor="middle"
              fill="rgba(16, 185, 129, 0.4)"
              fontSize="7"
              fontFamily="var(--f-mono)"
            >
              STANDBY SCANNER
            </text>
            <circle cx={cx} cy={cy} r={4} fill="#f87171" style={{ animation: 'blip-glow 1.5s infinite' }} />
          </g>
        )}

        {nearestCpaTarget && (
          <g>
            <line
              x1={nearestCpaTarget.px}
              y1={nearestCpaTarget.py}
              x2={nearestCpaTarget.cpaPx}
              y2={nearestCpaTarget.cpaPy}
              stroke="#f59e0b"
              strokeWidth={1.1 * markerScale}
              strokeDasharray="5 4"
              opacity="0.85"
            />
            <circle
              data-testid="radar-cpa-point"
              cx={nearestCpaTarget.cpaPx}
              cy={nearestCpaTarget.cpaPy}
              r={3.2 * markerScale}
              fill="#f87171"
              stroke="rgba(255,255,255,0.75)"
              strokeWidth={0.8 * markerScale}
            />
            <text
              x={nearestCpaTarget.cpaPx + 7 * markerScale}
              y={nearestCpaTarget.cpaPy + 3 * markerScale}
              fill="#fca5a5"
              fontSize={Math.max(7, 7.5 * markerScale)}
              fontFamily="var(--f-mono)"
              fontWeight="bold"
              style={{ textShadow: '0 0 4px rgba(5, 15, 10, 0.95)' }}
            >
              CPA
            </text>
          </g>
        )}

        {/* Target Blips */}
        {plottedTargets.map((t) => (
          <g
            key={t.mmsi}
            onMouseEnter={() => setHoveredTarget(t)}
            onMouseLeave={() => setHoveredTarget(null)}
            style={{
              animation: 'blip-glow 3s infinite ease-in-out',
              cursor: 'pointer',
            }}
          >
            <polygon
              points={`${t.px},${t.py - arrowTipOffset} ${t.px - arrowHalfWidth},${t.py - arrowBaseOffset} ${t.px + arrowHalfWidth},${t.py - arrowBaseOffset}`}
              transform={`rotate(${t.screenCogDeg} ${t.px} ${t.py})`}
              fill="#10b981"
              stroke="rgba(5, 15, 10, 0.72)"
              strokeWidth={0.45 * markerScale}
            />

            <circle
              data-testid={`radar-target-${t.mmsi}`}
              cx={t.px}
              cy={t.py}
              r={vesselDotRadius}
              fill="#10b981"
              stroke="rgba(5, 15, 10, 0.95)"
              strokeWidth={0.8 * markerScale}
            />

            {/* Pulse glow circle around blip */}
            <circle cx={t.px} cy={t.py} r={5 * markerScale} fill="none" stroke="rgba(16, 185, 129, 0.25)" strokeWidth={0.5 * markerScale} />

            {/* Alphanumeric target label */}
            <text
              x={t.px}
              y={t.py - 9 * markerScale}
              textAnchor="middle"
              fill="#10b981"
              fontSize={Math.max(7.5, 7.5 * markerScale)}
              fontFamily="var(--f-mono)"
              fontWeight="bold"
              style={{
                textShadow: '0px 0px 3px rgba(5,15,10,0.95)',
              }}
            >
              TS {t.mmsi.toString().slice(-3)}
            </text>
          </g>
        ))}
      </svg>

      {/* Hover Tooltip Card showing target details */}
      {hoveredTarget && (
        <div
          style={{
            position: 'absolute',
            left: hoveredTarget.px > cx ? hoveredTarget.px - 105 : hoveredTarget.px + 10,
            top: hoveredTarget.py > cy ? hoveredTarget.py - 80 : hoveredTarget.py + 10,
            background: 'rgba(7, 20, 15, 0.95)',
            border: '1px solid rgba(16, 185, 129, 0.8)',
            borderRadius: '4px',
            padding: '6px 8px',
            pointerEvents: 'none',
            zIndex: 30,
            boxShadow: '0 4px 12px rgba(0, 0, 0, 0.6), 0 0 8px rgba(16, 185, 129, 0.2)',
            fontFamily: 'var(--f-mono)',
            fontSize: '9.5px',
            color: '#e2e8f0',
            display: 'flex',
            flexDirection: 'column',
            gap: '3px',
            minWidth: '95px',
            boxSizing: 'border-box',
          }}
        >
          <div style={{ color: '#10b981', fontWeight: 'bold', borderBottom: '1px solid rgba(16, 185, 129, 0.3)', paddingBottom: '2px', marginBottom: '2px', fontSize: '10px' }}>
            TS {hoveredTarget.mmsi.toString().slice(-3)}
          </div>
          <div style={{ display: 'flex', justifyContent: 'space-between' }}>
            <span style={{ color: 'rgba(16, 185, 129, 0.7)' }}>距离:</span>
            <span>{hoveredTarget.rangeNM.toFixed(2)} NM</span>
          </div>
          <div style={{ display: 'flex', justifyContent: 'space-between' }}>
            <span style={{ color: 'rgba(16, 185, 129, 0.7)' }}>方位:</span>
            <span>{hoveredTarget.bearingDeg.toFixed(1)}°</span>
          </div>
          <div style={{ display: 'flex', justifyContent: 'space-between' }}>
            <span style={{ color: 'rgba(16, 185, 129, 0.7)' }}>航速:</span>
            <span>{hoveredTarget.sog.toFixed(1)} kn</span>
          </div>
          <div style={{ display: 'flex', justifyContent: 'space-between' }}>
            <span style={{ color: 'rgba(16, 185, 129, 0.7)' }}>航向:</span>
            <span>{hoveredTarget.cogDeg.toFixed(1)}°</span>
          </div>
        </div>
      )}

      {/* Relative Mode Tag Overlay */}
      {ownShip && (
        <div
          style={{
            position: 'absolute',
            bottom: 12,
            left: '50%',
            transform: 'translateX(-50%)',
            background: 'rgba(5, 15, 10, 0.75)',
            border: '1px solid rgba(16, 185, 129, 0.3)',
            borderRadius: '3px',
            padding: '2px 5px',
            fontSize: 7.5,
            color: 'rgba(16, 185, 129, 0.85)',
            fontFamily: 'var(--f-mono)',
            fontWeight: 'bold',
            letterSpacing: '0.05em',
            zIndex: 15,
          }}
        >
          {relativeMode ? 'H-UP (REL)' : 'N-UP (TRUE)'}
        </div>
      )}
    </div>
  );
};
