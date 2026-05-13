import React from 'react';

type RunState = 'IDLE' | 'READY' | 'ACTIVE' | 'PAUSED' | 'COMPLETED' | 'ABORTED';

interface RunStatePillProps {
  state: RunState;
}

const STATE_COLORS: Record<RunState, { bg: string; border: string; text: string; blink: boolean }> = {
  IDLE:       { bg: 'transparent', border: 'var(--line-2)', text: 'var(--txt-3)', blink: false },
  READY:      { bg: 'rgba(240,183,47,0.12)', border: 'var(--c-warn)', text: 'var(--c-warn)', blink: false },
  ACTIVE:     { bg: 'rgba(63,185,80,0.12)', border: 'var(--c-stbd)', text: 'var(--c-stbd)', blink: true },
  PAUSED:     { bg: 'rgba(240,183,47,0.12)', border: 'var(--c-warn)', text: 'var(--c-warn)', blink: false },
  COMPLETED:  { bg: 'rgba(121,192,255,0.12)', border: 'var(--c-info)', text: 'var(--c-info)', blink: false },
  ABORTED:    { bg: 'rgba(248,81,73,0.12)', border: 'var(--c-danger)', text: 'var(--c-danger)', blink: false },
};

const STATE_LABELS: Record<RunState, string> = {
  IDLE:       '后台闲置 · IDLE',
  READY:      '引擎就绪 · READY',
  ACTIVE:     '仿真运行 · ACTIVE',
  PAUSED:     '仿真暂停 · PAUSED',
  COMPLETED:  '仿真完成 · COMPLETED',
  ABORTED:    '意外中止 · ABORTED',
};

export const RunStatePill: React.FC<RunStatePillProps> = ({ state }) => {
  const c = STATE_COLORS[state] ?? STATE_COLORS.IDLE;
  const label = STATE_LABELS[state] ?? STATE_LABELS.IDLE;
  return (
    <div
      data-testid="run-state-pill"
      style={{
        display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 10,
        width: 175,
        padding: '4px 12px',
        borderRadius: 4,
        border: `1px solid ${c.border}`,
        background: c.bg,
        fontFamily: 'var(--f-mono)', fontSize: 12, letterSpacing: '0.05em',
        color: c.text, fontWeight: 600, textTransform: 'uppercase',
      }}
    >
      <div style={{
        width: 8, height: 8, borderRadius: '50%', background: c.text, flexShrink: 0,
        boxShadow: `0 0 8px ${c.text}`,
        animation: c.blink ? 'phos-pulse 1.2s infinite' : 'phos-pulse 2.4s infinite',
      }} />
      <span style={{ paddingTop: 1 }}>{label}</span>
    </div>
  );
};
