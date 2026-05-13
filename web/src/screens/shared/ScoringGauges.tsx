import React from 'react';
import { useTelemetryStore } from '../../store';

interface ScoringGaugesProps {
  visible: boolean;
}

const DIMS = [
  { key: 'safety',        label: 'SAF', color: 'var(--c-stbd)' },
  { key: 'ruleCompliance',label: 'RUL', color: 'var(--c-stbd)' },
  { key: 'delay',         label: 'DEL', color: 'var(--c-warn)' },
  { key: 'magnitude',     label: 'MAG', color: 'var(--c-stbd)' },
  { key: 'phase',         label: 'PHA', color: 'var(--c-stbd)' },
  { key: 'plausibility',  label: 'PLA', color: 'var(--c-warn)' },
] as const;

export const ScoringGauges: React.FC<ScoringGaugesProps> = React.memo(({ visible }) => {
  const scoringRow = useTelemetryStore((s) => s.scoringRow as any);

  if (!visible) return null;

  return (
    <div
      data-testid="scoring-gauges"
      style={{
        position: 'absolute', bottom: 58, right: 16, zIndex: 15,
        background: 'rgba(11,19,32,0.92)',
        border: '1px solid var(--line-2)',
        borderRadius: 6, padding: '6px 8px',
        fontFamily: 'var(--f-mono)', fontSize: 9,
        minWidth: 90,
      }}
    >
      <div style={{ color: 'var(--c-stbd)', fontSize: 8, marginBottom: 4, letterSpacing: 1 }}>
        SCORING
      </div>
      {DIMS.map(({ key, label, color }) => {
        const val = scoringRow?.[key];
        const disp = typeof val === 'number' ? val.toFixed(2) : '—';
        const numVal = typeof val === 'number' ? val : 0;
        const barColor = numVal >= 0.8 ? 'var(--c-stbd)' : numVal >= 0.6 ? 'var(--c-warn)' : 'var(--c-danger)';
        return (
          <div key={key} style={{ display: 'flex', alignItems: 'center', gap: 6, marginBottom: 2 }}>
            <span style={{ color: 'var(--txt-3)', width: 22, fontSize: 7 }}>{label}</span>
            <div style={{ flex: 1, height: 4, background: 'var(--bg-2)', borderRadius: 2 }}>
              <div style={{
                width: `${Math.min(numVal * 100, 100)}%`, height: '100%',
                background: barColor, borderRadius: 2,
              }} />
            </div>
            <span style={{ color, fontWeight: 600, width: 30, textAlign: 'right', fontSize: 8 }}>{disp}</span>
          </div>
        );
      })}
    </div>
  );
});
ScoringGauges.displayName = 'ScoringGauges';
