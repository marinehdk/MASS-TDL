import React from 'react';

interface CompassRoseProps {
  bearing: number;
  relativeMode: boolean;
}

export const CompassRose: React.FC<CompassRoseProps> = ({ bearing, relativeMode }) => {
  const cx = 48;
  const cy = 48;

  // Generate ticks every 30 degrees
  const ticks = Array.from({ length: 12 }, (_, i) => i * 30);

  return (
    <div
      data-testid="compass-rose"
      style={{
        width: 96,
        height: 96,
        background: 'rgba(11, 23, 38, 0.85)',
        backdropFilter: 'blur(4px)',
        border: '1px solid rgba(45, 212, 191, 0.3)',
        borderRadius: '50%',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        boxShadow: '0 4px 20px rgba(0, 0, 0, 0.5), inset 0 0 12px rgba(45, 212, 191, 0.05)',
        userSelect: 'none',
      }}
    >
      <svg
        width={96}
        height={96}
        viewBox="0 0 96 96"
        style={{
          transform: relativeMode ? `rotate(${-bearing}deg)` : 'none',
          transition: 'transform 0.4s cubic-bezier(0.25, 1, 0.5, 1)',
        }}
      >
        {/* Ticks and outer rings */}
        <circle cx={cx} cy={cy} r={42} fill="none" stroke="rgba(91, 192, 190, 0.2)" strokeWidth="0.5" strokeDasharray="2 2" />
        <circle cx={cx} cy={cy} r={33} fill="none" stroke="rgba(91, 192, 190, 0.15)" strokeWidth="0.5" />

        {ticks.map((angle) => {
          // Avoid rendering ticks on cardinal directions to keep it extremely clean
          if (angle % 90 === 0) return null;
          const rad = (angle * Math.PI) / 180;
          const x1 = cx + Math.sin(rad) * 36;
          const y1 = cy - Math.cos(rad) * 36;
          const x2 = cx + Math.sin(rad) * 41;
          const y2 = cy - Math.cos(rad) * 41;
          return (
            <line
              key={angle}
              x1={x1}
              y1={y1}
              x2={x2}
              y2={y2}
              stroke="rgba(91, 192, 190, 0.4)"
              strokeWidth="0.75"
            />
          );
        })}

        {/* Cardinal Directions */}
        <text x={48} y={20} textAnchor="middle" fill="#ef4444" fontSize="11" fontWeight="bold" fontFamily="var(--f-mono)" letterSpacing="0">N</text>
        <text x={48} y={82} textAnchor="middle" fill="var(--txt-2)" fontSize="10" fontFamily="var(--f-mono)" letterSpacing="0">S</text>
        <text x={80} y={51.5} textAnchor="middle" fill="var(--txt-3)" fontSize="10" fontFamily="var(--f-mono)" letterSpacing="0">E</text>
        <text x={16} y={51.5} textAnchor="middle" fill="var(--txt-3)" fontSize="10" fontFamily="var(--f-mono)" letterSpacing="0">W</text>

        {/* 3D split-shaded metallic needle */}
        {/* North pointer */}
        <polygon points="48,22 44,48 48,48" fill="#f87171" />
        <polygon points="48,22 52,48 48,48" fill="#dc2626" />

        {/* South pointer */}
        <polygon points="48,74 44,48 48,48" fill="#cbd5e1" />
        <polygon points="48,74 52,48 48,48" fill="#64748b" />

        {/* Center Hub */}
        <circle cx={cx} cy={cy} r={4} fill="var(--bg-1)" stroke="var(--c-phos)" strokeWidth="1" />
        <circle cx={cx} cy={cy} r={1.5} fill="var(--c-phos)" />
      </svg>
    </div>
  );
};
