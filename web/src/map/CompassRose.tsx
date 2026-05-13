import React from 'react';

interface CompassRoseProps {
  bearing: number;
  relativeMode: boolean;
}

export const CompassRose: React.FC<CompassRoseProps> = ({ bearing, relativeMode }) => {
  const radius = 30;
  const cx = 36;
  const cy = 36;

  return (
    <div
      data-testid="compass-rose"
      style={{
        width: 72, height: 72,
        background: 'rgba(11,19,32,0.82)',
        border: '1px solid var(--line-2)',
        borderRadius: '50%',
        display: 'flex', alignItems: 'center', justifyContent: 'center',
      }}
    >
      <svg width={72} height={72} viewBox="0 0 72 72"
           style={{ transform: relativeMode ? `rotate(${bearing}deg)` : 'none' }}>
        <circle cx={cx} cy={cy} r={radius} fill="none" stroke="var(--line-1)" strokeWidth="0.5" />
        <line x1={cx} y1={cy - radius + 4} x2={cx} y2={cy - radius + 12} stroke="var(--c-danger)" strokeWidth="1.5" />
        <line x1={cx} y1={cy + radius - 4} x2={cx} y2={cy + radius - 12} stroke="var(--txt-3)" strokeWidth="0.5" />
        <line x1={cx - radius + 4} y1={cy} x2={cx - radius + 12} y2={cy} stroke="var(--txt-3)" strokeWidth="0.5" />
        <line x1={cx + radius - 4} y1={cy} x2={cx + radius - 12} y2={cy} stroke="var(--txt-3)" strokeWidth="0.5" />
        <circle cx={cx} cy={cy} r={2} fill="var(--c-phos)" />
      </svg>
    </div>
  );
};
