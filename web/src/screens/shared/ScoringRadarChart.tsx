import React from 'react';

interface RadarProps {
  kpis: Record<string, number> | null;
}

const AXES = [
  { key: 'safety',        label: 'Safety',        angle: -90 },
  { key: 'ruleCompliance',label: 'Rule',           angle: -30 },
  { key: 'delay',         label: 'Delay',          angle: 30 },
  { key: 'magnitude',     label: 'Magnitude',      angle: 90 },
  { key: 'phase',         label: 'Phase',          angle: 150 },
  { key: 'plausibility',  label: 'Plausibility',    angle: 210 },
];

const R = 80;
const CX = 100;
const CY = 100;

export const ScoringRadarChart: React.FC<RadarProps> = ({ kpis }) => {
  const dataPoints = AXES.map(({ key, angle }) => {
    const val = kpis?.[key] ?? 0;
    const r = (R * Math.min(val, 1));
    const rad = (angle * Math.PI) / 180;
    return `${CX + r * Math.cos(rad)},${CY + r * Math.sin(rad)}`;
  }).join(' ');

  const gridRings = [0.25, 0.5, 0.75, 1.0].map((frac) => {
    const pts = AXES.map(({ angle }) => {
      const rad = (angle * Math.PI) / 180;
      return `${CX + R * frac * Math.cos(rad)},${CY + R * frac * Math.sin(rad)}`;
    }).join(' ');
    return <polygon key={frac} points={pts} fill="none" stroke="var(--line-1)" strokeWidth="0.5" />;
  });

  return (
    <div data-testid="scoring-radar" style={{ display: 'flex', flexDirection: 'column', alignItems: 'center' }}>
      <div style={{ color: 'var(--txt-3)', fontSize: 9, letterSpacing: 1, marginBottom: 6 }}>
        SCORING RADAR
      </div>
      <svg width={200} height={200} viewBox="0 0 200 200">
        {gridRings}
        {AXES.map(({ angle, label }) => {
          const rad = (angle * Math.PI) / 180;
          const lx = CX + (R + 16) * Math.cos(rad);
          const ly = CY + (R + 16) * Math.sin(rad);
          return (
            <g key={label}>
              <line x1={CX} y1={CY} x2={CX + R * Math.cos(rad)} y2={CY + R * Math.sin(rad)}
                    stroke="var(--line-1)" strokeWidth="0.5" />
              <text x={lx} y={ly} textAnchor="middle" fill="var(--txt-3)" fontSize="7">
                {label} {kpis?.[AXES.find(a=>a.label===label)?.key ?? ''] != null
                  ? (kpis?.[AXES.find(a=>a.label===label)?.key ?? ''] ?? 0).toFixed(2)
                  : '—'}
              </text>
            </g>
          );
        })}
        <polygon points={dataPoints} fill="var(--c-stbd)" fillOpacity="0.2" stroke="var(--c-stbd)" strokeWidth="1.5" />
      </svg>
    </div>
  );
};
