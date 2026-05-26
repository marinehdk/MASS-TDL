import { memo } from 'react';
import type { FsmState } from '../store/fsmStore';

interface FsmStateBadgeProps {
  state: FsmState;
  size?: 'sm' | 'md' | 'lg';
}

const FSM_COLORS: Record<FsmState, string> = {
  TRANSIT: '#34d399',
  COLREG_AVOIDANCE: '#fbbf24',
  TOR: '#f87171',
  OVERRIDE: '#f87171',
  MRC: '#8b0000',
  HANDBACK: '#06b6d4',
};

export const FsmStateBadge = memo(function FsmStateBadge({ state, size = 'md' }: FsmStateBadgeProps) {
  const color = FSM_COLORS[state];
  const padding = size === 'sm' ? '2px 6px' : size === 'lg' ? '6px 12px' : '4px 8px';
  const fontSize = size === 'sm' ? 10 : size === 'lg' ? 14 : 12;

  return (
    <span
      style={{
        display: 'inline-block',
        padding,
        background: color + '20',
        border: `1px solid ${color}`,
        borderRadius: 3,
        color,
        fontWeight: 'bold',
        fontSize,
        fontFamily: 'var(--f-mono)',
        textTransform: 'uppercase',
      }}
    >
      {state}
    </span>
  );
});
