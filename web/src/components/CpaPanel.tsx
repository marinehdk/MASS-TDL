import { memo } from 'react';

interface Props {
  cpaNm: number;
}

function cpaColor(cpa: number): string {
  if (cpa === 0) return 'var(--txt-3)';
  if (cpa < 0.3) return 'var(--color-port)';
  if (cpa < 0.5) return 'var(--color-warn)';
  return 'var(--color-stbd)';
}

export const CpaPanel = memo(function CpaPanel({ cpaNm }: Props) {
  return (
    <div style={{
      flex: 1,
      background: 'var(--bg-2)',
      borderRadius: 'var(--radius-none)',
      border: '1px solid var(--line-1)',
      padding: '10px 12px',
    }}>
      <div style={{
        fontFamily: 'var(--fnt-disp)',
        fontSize: '10px',
        letterSpacing: '0.18em',
        color: 'var(--txt-3)',
        textTransform: 'uppercase',
        fontWeight: 500,
        marginBottom: '2px',
      }}>CPA · Closest Point of Approach</div>
      <div style={{
        fontFamily: 'var(--fnt-mono)',
        fontSize: '28px',
        fontWeight: 500,
        color: cpaColor(cpaNm),
        lineHeight: 1.2,
      }}>
        {cpaNm === 0 ? '—' : cpaNm.toFixed(2)}
      </div>
      <div style={{
        fontFamily: 'var(--fnt-mono)',
        fontSize: '10px',
        color: 'var(--txt-3)',
        marginTop: '2px',
      }}>nm · safe &ge; 0.50 nm</div>
    </div>
  );
});
