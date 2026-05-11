import { memo } from 'react';

interface Props {
  decisionText: string;
}

export const DecisionPanel = memo(function DecisionPanel({ decisionText }: Props) {
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
        marginBottom: '4px',
      }}>L3 System Decision</div>
      <div style={{
        fontFamily: 'var(--fnt-mono)',
        fontSize: '13px',
        color: decisionText ? 'var(--color-warn)' : 'var(--txt-3)',
      }}>
        {decisionText || 'Awaiting run'}
      </div>
    </div>
  );
});
