import React from 'react';

interface ColregsSectorsProps {
  ownShipFraction: [number, number];
  headingDeg: number;
  outerRadiusPx: number;
}

interface SectorDef {
  label: string;
  startDeg: number;
  endDeg: number;
  color: string;
}

const SECTORS: SectorDef[] = [
  { label: 'HEAD-ON',    startDeg: -6,  endDeg: 6,    color: 'rgba(248,113,113,0.06)' },
  { label: 'GIVE-WAY',   startDeg: 6,   endDeg: 112.5, color: 'rgba(96,165,250,0.05)' },
  { label: 'STAND-ON',   startDeg: 247.5, endDeg: 354, color: 'rgba(96,165,250,0.05)' },
  { label: 'OVERTAKING', startDeg: 112.5, endDeg: 247.5, color: 'rgba(163,230,53,0.04)' },
];

export const ColregsSectors: React.FC<ColregsSectorsProps> = React.memo(({
  ownShipFraction, headingDeg, outerRadiusPx,
}) => {
  const r = outerRadiusPx;

  return (
    <svg
      data-testid="colregs-sectors"
      style={{
        position: 'absolute', top: 0, left: 0,
        width: '100%', height: '100%',
        pointerEvents: 'none', zIndex: 4,
      }}
      viewBox={`-${r} -${r} ${r * 2} ${r * 2}`}
    >
      <g transform={`translate(0,0) rotate(${headingDeg})`}>
        {SECTORS.map((sec) => {
          const startRad = (sec.startDeg * Math.PI) / 180;
          const endRad = (sec.endDeg * Math.PI) / 180;
          const x1 = r * Math.sin(startRad);
          const y1 = -r * Math.cos(startRad);
          const x2 = r * Math.sin(endRad);
          const y2 = -r * Math.cos(endRad);
          const largeArc = sec.endDeg - sec.startDeg > 180 ? 1 : 0;
          const d = `M 0 0 L ${x1} ${y1} A ${r} ${r} 0 ${largeArc} 1 ${x2} ${y2} Z`;

          return (
            <g key={sec.label}>
              <path d={d} fill={sec.color} stroke={sec.color.replace('0.0', '0.12')} strokeWidth="0.5" />
              <text
                x={r * 0.55 * Math.sin((startRad + endRad) / 2)}
                y={-r * 0.55 * Math.cos((startRad + endRad) / 2)}
                textAnchor="middle"
                fill={sec.color.replace('0.0', '0.4')}
                fontSize="7"
              >
                {sec.label}
              </text>
            </g>
          );
        })}
      </g>
    </svg>
  );
});
ColregsSectors.displayName = 'ColregsSectors';
