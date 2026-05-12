import { memo } from 'react';

interface Props {
  ruleText: string;
}

export const RulePanel = memo(function RulePanel({ ruleText }: Props) {
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
      }}>M6 COLREGs Classification</div>
      <div style={{
        fontFamily: 'var(--fnt-mono)',
        fontSize: '14px',
        fontWeight: 500,
        color: ruleText ? 'var(--color-phos)' : 'var(--txt-3)',
      }}>
        {ruleText || 'Standby'}
      </div>
    </div>
  );
});
