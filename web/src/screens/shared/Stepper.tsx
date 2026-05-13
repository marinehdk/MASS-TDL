import React from 'react';

interface StepperProps {
  steps: string[];
  current: number;
  onJump?: (i: number) => void;
}

export const Stepper: React.FC<StepperProps> = ({ steps, current, onJump }) => {
  return (
    <div style={{
      display: 'flex', borderBottom: '1px solid var(--line-2)',
      background: 'var(--bg-1)', flexShrink: 0,
    }}>
      {steps.map((label, i) => {
        const active = i === current;
        const done = i < current;
        return (
          <div key={label}
            data-testid={`builder-step-${i + 1}`}
            onClick={() => onJump?.(i)}
            style={{
              display: 'flex', alignItems: 'center', gap: 10,
              padding: '10px 18px', cursor: 'pointer',
              borderRight: '1px solid var(--line-1)',
              borderBottom: active ? '2px solid var(--c-phos)' : '2px solid transparent',
              background: active ? 'var(--bg-0)' : 'transparent',
            }}
          >
            <div style={{
              width: 22, height: 22,
              border: `1px solid ${active || done ? 'var(--c-phos)' : 'var(--line-2)'}`,
              color: active || done ? 'var(--c-phos)' : 'var(--txt-3)',
              display: 'flex', alignItems: 'center', justifyContent: 'center',
              fontFamily: 'var(--f-disp)', fontSize: 11, fontWeight: 700,
              background: done ? 'rgba(91,192,190,0.10)' : 'transparent',
            }}>
              {done ? '✓' : String.fromCharCode(65 + i)}
            </div>
            <div>
              <div style={{
                fontFamily: 'var(--f-disp)', fontSize: 11,
                color: active ? 'var(--txt-0)' : done ? 'var(--txt-1)' : 'var(--txt-2)',
                fontWeight: 600, letterSpacing: '0.12em', textTransform: 'uppercase',
              }}>{label}</div>
            </div>
          </div>
        );
      })}
    </div>
  );
};
