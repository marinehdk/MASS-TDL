import React from 'react';
import { useTelemetryStore } from '../../store';

const COMM_LINKS = [
  'DDS-Bus', 'L4↓', 'L2↑', 'Param-DB', 'ROC-Lnk', 'ASDR',
];

const STATE_COLORS: Record<string, string> = {
  ok: 'var(--c-stbd)', warn: 'var(--c-warn)', fail: 'var(--c-danger)', disabled: 'var(--txt-3)',
};

export const CommLinkStatusRow: React.FC = () => {
  const commLinks = useTelemetryStore((s) => (s as any).commLinks ?? []);

  return (
    <div data-testid="preflight-commlinks">
      <div style={{
        fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)',
        letterSpacing: '0.18em', textTransform: 'uppercase', marginBottom: 6,
      }}>COMM LINKS</div>
      <div style={{ display: 'flex', gap: 8, flexWrap: 'wrap' }}>
        {COMM_LINKS.map((name) => {
          const data = commLinks.find((x: any) => x.id === name);
          const state = data?.state ?? 'disabled';
          const color = STATE_COLORS[state] ?? 'var(--txt-3)';
          return (
            <div key={name} style={{
              display: 'flex', alignItems: 'center', gap: 5,
              padding: '4px 8px', background: 'var(--bg-1)',
              border: `1px solid var(--line-1)`,
              borderLeft: `2px solid ${color}`,
            }}>
              <div style={{
                width: 7, height: 7, borderRadius: '50%', background: color,
                boxShadow: state === 'ok' ? `0 0 5px ${color}` : 'none',
              }} />
              <span style={{
                fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-1)',
                letterSpacing: '0.08em',
              }}>{name}</span>
            </div>
          );
        })}
      </div>
    </div>
  );
};
