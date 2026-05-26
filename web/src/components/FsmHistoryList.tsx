import { memo } from 'react';
import type { FsmTransition } from '../store/fsmStore';

interface FsmHistoryListProps {
  transitions: FsmTransition[];
}

const ARROW = ' → ';

export const FsmHistoryList = memo(function FsmHistoryList({ transitions }: FsmHistoryListProps) {
  return (
    <div style={{
      display: 'flex',
      flexDirection: 'column',
      gap: 4,
    }}>
      {transitions.map((t, i) => (
        <div key={i} style={{
          padding: '4px 6px',
          background: 'var(--bg-2)',
          borderRadius: 2,
          fontSize: 9,
          fontFamily: 'var(--f-mono)',
          color: 'var(--c-text-2)',
          whiteSpace: 'nowrap',
          overflow: 'hidden',
          textOverflow: 'ellipsis',
        }}>
          <span style={{ color: 'var(--c-info)' }}>{t.from}</span>
          <span style={{ color: 'var(--c-text-2)' }}>{ARROW}</span>
          <span style={{ color: 'var(--c-warn)' }}>{t.to}</span>
          <span style={{ color: 'var(--c-text-3)', marginLeft: 4 }}>
            ({t.reason.substring(0, 30)})
          </span>
        </div>
      ))}
    </div>
  );
});
