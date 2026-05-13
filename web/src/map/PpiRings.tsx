import React from 'react';

interface PpiRingsProps {
  centerFraction: [number, number];
  radiiPx: number[];
}

export const PpiRings: React.FC<PpiRingsProps> = React.memo(({ centerFraction, radiiPx }) => {
  return (
    <svg
      data-testid="ppi-rings"
      style={{
        position: 'absolute', top: 0, left: 0,
        width: '100%', height: '100%',
        pointerEvents: 'none', zIndex: 5,
      }}
    >
      {radiiPx.map((r, i) => (
        <circle
          key={i}
          cx={`${centerFraction[0]}%`}
          cy={`${centerFraction[1]}%`}
          r={r}
          fill="none"
          stroke="var(--c-phos)"
          strokeWidth="0.5"
          strokeOpacity={0.12 - i * 0.03}
        />
      ))}
    </svg>
  );
});
PpiRings.displayName = 'PpiRings';
