import React from 'react';

interface ImazuCase {
  id: number;
  name: string;
  rule: string;
  ships: Array<{ x: number; y: number; h: number; role: string }>;
}

interface ImazuGridProps {
  cases: ImazuCase[];
  selected: number | null;
  onSelect: (id: number) => void;
}

export const ImazuGrid: React.FC<ImazuGridProps> = ({ cases, selected, onSelect }) => {
  return (
    <div data-testid="imazu-grid">
      <div style={{
        display: 'flex', justifyContent: 'space-between', alignItems: 'baseline', marginBottom: 8,
      }}>
        <span style={{
          fontFamily: 'var(--f-disp)', fontSize: 12, color: 'var(--txt-1)',
          letterSpacing: '0.14em', fontWeight: 600, textTransform: 'uppercase',
        }}>22 IMAZU CASES</span>
        <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)' }}>
          Imazu 1987 + COLREGs R9/R13/R14/R15/R17
        </span>
      </div>
      <div style={{
        display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(150px, 1fr))',
        gap: 8,
      }}>
        {cases.map((c) => {
          const sel = selected === c.id;
          const accent = sel ? 'var(--c-warn)' : 'var(--line-2)';
          return (
            <div key={c.id}
              data-testid={`imazu-cell-${c.id}`}
              onClick={() => onSelect(c.id)}
              style={{
                background: sel ? 'rgba(240,183,47,0.06)' : 'var(--bg-1)',
                border: `1px solid ${accent}`,
                borderLeft: sel ? `2px solid var(--c-warn)` : `1px solid ${accent}`,
                padding: '8px 10px 6px', cursor: 'pointer',
                display: 'flex', flexDirection: 'column', gap: 4,
              }}
            >
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'baseline' }}>
                <span style={{
                  fontFamily: 'var(--f-disp)', fontSize: 10.5, fontWeight: 600,
                  color: sel ? 'var(--c-warn)' : 'var(--txt-1)',
                  letterSpacing: '0.10em',
                }}>
                  IM{String(c.id).padStart(2, '0')}
                </span>
                <span style={{ fontFamily: 'var(--f-mono)', fontSize: 8.5, color: sel ? 'var(--c-warn)' : 'var(--txt-3)' }}>
                  {c.rule}
                </span>
              </div>
              {/* Mini SVG */}
              <svg viewBox="0 0 100 100" style={{ width: '100%', height: 70, background: '#050810' }}>
                <circle cx="50" cy="75" r="28" fill="none" stroke="var(--line-1)" strokeWidth="0.4" strokeDasharray="1.5 2" opacity="0.6" />
                <circle cx="50" cy="75" r="56" fill="none" stroke="var(--line-1)" strokeWidth="0.4" strokeDasharray="1.5 2" opacity="0.4" />
                <line x1="50" y1="75" x2="50" y2="20" stroke="var(--c-phos)" strokeWidth="0.5" opacity="0.5" />
                {c.ships.map((s, i) => {
                  const color = s.role === 'give-way' ? 'var(--c-danger)' : s.role === 'stand-on' ? 'var(--c-warn)' : 'var(--c-info)';
                  const rad = (s.h - 90) * Math.PI / 180;
                  const vx = Math.cos(rad) * 10;
                  const vy = Math.sin(rad) * 10;
                  return (
                    <g key={i}>
                      <line x1={s.x} y1={s.y} x2={s.x + vx} y2={s.y + vy} stroke={color} strokeWidth="0.6" opacity="0.7" />
                      <g transform={`translate(${s.x},${s.y}) rotate(${s.h})`}>
                        <path d="M 0 -3 L 2 2 L 0 1 L -2 2 Z" fill="none" stroke={color} strokeWidth="0.7" />
                      </g>
                    </g>
                  );
                })}
                <g transform="translate(50,75)">
                  <path d="M 0 -3.5 L 2.5 2.5 L 0 1 L -2.5 2.5 Z" fill="var(--c-phos)" />
                </g>
              </svg>
              <div style={{
                fontFamily: 'var(--f-body)', fontSize: 9.5, color: sel ? 'var(--txt-0)' : 'var(--txt-2)',
                lineHeight: 1.1, minHeight: 18,
              }}>
                {c.name}
              </div>
            </div>
          );
        })}
      </div>
    </div>
  );
};
