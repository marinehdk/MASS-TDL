import { memo } from 'react';

interface Props {
  tcpaS: number;
}

export const TcpaPanel = memo(function TcpaPanel({ tcpaS }: Props) {
  const color = tcpaS === 0 ? 'var(--txt-3)' : tcpaS <= 600 ? 'var(--color-warn)' : 'var(--txt-0)';
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
      }}>TCPA · Time to CPA</div>
      <div style={{
        fontFamily: 'var(--fnt-mono)',
        fontSize: '20px',
        fontWeight: 500,
        color,
        lineHeight: 1.2,
      }}>
        {tcpaS === 0 ? '—' : String(tcpaS)}
      </div>
      <div style={{
        fontFamily: 'var(--fnt-mono)',
        fontSize: '10px',
        color: 'var(--txt-3)',
        marginTop: '2px',
      }}>s · alert &le; 600 s</div>
    </div>
  );
});
