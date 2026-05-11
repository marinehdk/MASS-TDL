import { memo } from 'react';

interface Props {
  moduleStatus: boolean[];
}

const MODULE_LABELS = [
  'M1\nODD', 'M2\nWorld', 'M3\nMiss', 'M4\nArb',
  'M5\nPlan', 'M6\nCOLR', 'M7\nSafe', 'M8\nHMI',
];

export const ModulePulseBar = memo(function ModulePulseBar({ moduleStatus }: Props) {
  return (
    <div style={{
      padding: '10px 14px',
      borderBottom: '1px solid var(--line-1)',
    }}>
      <div style={{
        fontFamily: 'var(--fnt-disp)',
        fontSize: '10px',
        letterSpacing: '0.22em',
        color: 'var(--txt-3)',
        textTransform: 'uppercase',
        marginBottom: '8px',
      }}>Module Pipeline Status</div>
      <div style={{ display: 'flex', gap: '4px', justifyContent: 'space-between' }}>
        {MODULE_LABELS.map((label, i) => {
          const active = moduleStatus[i] === true;
          return (
            <div key={i} style={{ textAlign: 'center', flex: 1 }}>
              <div style={{
                width: '18px',
                height: '18px',
                borderRadius: '50%',
                margin: '0 auto 3px',
                border: '1px solid var(--line-2)',
                background: active ? 'var(--color-stbd)' : 'var(--bg-2)',
                boxShadow: active ? '0 0 8px rgba(63,185,80,0.4)' : 'none',
                transition: 'all 0.4s',
              }} />
              <div style={{
                fontFamily: 'var(--fnt-mono)',
                fontSize: '9px',
                color: 'var(--txt-3)',
                whiteSpace: 'pre-line',
                lineHeight: 1.3,
              }}>{label}</div>
            </div>
          );
        })}
      </div>
    </div>
  );
});
