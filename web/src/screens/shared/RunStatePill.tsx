import React from 'react';

type RunState = 'IDLE' | 'READY' | 'ACTIVE' | 'PAUSED' | 'COMPLETED' | 'ABORTED';

interface RunStatePillProps {
  state: RunState;
  simTime?: number;
}

const STATE_COLORS: Record<RunState, { bg: string; border: string; text: string; blink: boolean }> = {
  IDLE:       { bg: '#1f2937', border: '#374151', text: '#9ca3af', blink: false },
  READY:      { bg: 'rgba(240,183,47,0.12)', border: 'var(--c-warn)', text: 'var(--c-warn)', blink: false },
  ACTIVE:     { bg: 'rgba(63,185,80,0.12)', border: 'var(--c-stbd)', text: 'var(--c-stbd)', blink: true },
  PAUSED:     { bg: 'rgba(240,183,47,0.12)', border: 'var(--c-warn)', text: 'var(--c-warn)', blink: false },
  COMPLETED:  { bg: 'rgba(121,192,255,0.12)', border: 'var(--c-info)', text: 'var(--c-info)', blink: false },
  ABORTED:    { bg: 'rgba(248,81,73,0.12)', border: 'var(--c-danger)', text: 'var(--c-danger)', blink: false },
};

export const RunStatePill: React.FC<RunStatePillProps> = ({ state, simTime }) => {
  const c = STATE_COLORS[state] ?? STATE_COLORS.IDLE;
  return (
    <div
      data-testid="run-state-pill"
      style={{
        display: 'flex', alignItems: 'center', gap: 8,
        padding: '2px 12px',
        border: `1px solid ${c.border}`,
        background: c.bg,
        fontFamily: 'var(--f-disp)', fontSize: 10, letterSpacing: '0.16em',
        color: c.text, fontWeight: 600, textTransform: 'uppercase',
      }}
    >
      <div style={{
        width: 7, height: 7, borderRadius: '50%', background: c.text,
        boxShadow: `0 0 6px ${c.text}`,
        animation: c.blink ? 'phos-pulse 1.2s infinite' : 'phos-pulse 2.4s infinite',
      }} />
      {state}
      {simTime != null && (
        <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)', marginLeft: 4 }}>
          T+{String(Math.floor((simTime ?? 0) / 60)).padStart(2, '0')}:{String(Math.floor((simTime ?? 0) % 60)).padStart(2, '0')}
        </span>
      )}
    </div>
  );
};
