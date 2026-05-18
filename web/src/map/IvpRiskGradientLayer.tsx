import React from 'react';
import type { IvpContribution } from '../types/sat';

interface IvpRiskGradientLayerProps {
  contributions: IvpContribution[];
  activeBehavior: string | null;
  activeBehaviorWeight: number;
  ownShipScreenPos: [number, number];
  headingDeg: number;
}

const MAX_ARROW_PX = 60;

function costToColor(cost: number): string {
  if (cost < 0.4) return '#34d399';
  if (cost < 0.7) return '#fbbf24';
  return '#f87171';
}

export const IvpRiskGradientLayer: React.FC<IvpRiskGradientLayerProps> = React.memo(({
  contributions, activeBehavior, activeBehaviorWeight, ownShipScreenPos, headingDeg,
}) => {
  if (contributions.length === 0) return null;

  const [cx, cy] = ownShipScreenPos;

  return (
    <svg
      style={{ position: 'absolute', inset: 0, pointerEvents: 'none', zIndex: 8, overflow: 'visible' }}
      width="100%" height="100%"
    >
      {contributions.map((c) => {
        const length = c.cost * MAX_ARROW_PX;
        const angleDeg = c.direction_deg - headingDeg;
        const angleRad = (angleDeg - 90) * (Math.PI / 180);
        const x2 = cx + length * Math.cos(angleRad);
        const y2 = cy + length * Math.sin(angleRad);
        const color = costToColor(c.cost);

        return (
          <g key={c.direction_deg}>
            <line
              data-testid={`ivp-arrow-${c.direction_deg}`}
              x1={cx} y1={cy} x2={x2} y2={y2}
              stroke={color} strokeWidth={2.5} strokeLinecap="round"
              fill={color}
            />
            <polygon
              fill={color}
              points={`${x2},${y2} ${x2 - 4 * Math.cos(angleRad - 0.4)},${y2 - 4 * Math.sin(angleRad - 0.4)} ${x2 - 4 * Math.cos(angleRad + 0.4)},${y2 - 4 * Math.sin(angleRad + 0.4)}`}
            />
          </g>
        );
      })}

      {activeBehavior && (
        <text
          x={cx} y={cy - 72}
          textAnchor="middle"
          fill="#fbbf24"
          fontSize={11}
          fontFamily="var(--f-mono)"
          fontWeight="bold"
        >
          {activeBehavior} {activeBehaviorWeight.toFixed(2)}
        </text>
      )}
    </svg>
  );
});
IvpRiskGradientLayer.displayName = 'IvpRiskGradientLayer';
