import { memo } from 'react';

interface OddData {
  zone: string;
  envelope_state: string;
  conformance_score: number;
  confidence: number;
}

interface Props {
  odd: OddData | null;
}

const ZONE_COLORS: Record<string, string> = {
  'A': 'var(--color-stbd)',
  'B': '#f59e0b',
  'C': 'var(--color-port)',
  'D': '#ef4444',
};

export const OddPanel = memo(function OddPanel({ odd }: Props) {
  if (!odd) return null;
  const zoneKey = odd.zone?.charAt(0) ?? 'A';
  return (
    <div style={{
      padding: '10px 14px',
      borderBottom: '1px solid var(--line-1)',
      display: 'flex',
      alignItems: 'center',
      gap: '8px',
    }}>
      <span style={{
        fontFamily: 'var(--fnt-mono)',
        fontSize: '9px',
        color: 'var(--text-2)',
        textTransform: 'uppercase',
        letterSpacing: '1px',
      }}>ODD</span>
      <span style={{
        fontFamily: 'var(--fnt-mono)',
        fontSize: '12px',
        fontWeight: 600,
        color: ZONE_COLORS[zoneKey] ?? 'var(--text-1)',
        padding: '2px 6px',
        background: 'var(--bg-2)',
        border: '1px solid var(--line-1)',
        borderRadius: 'var(--radius-none)',
      }}>{odd.zone}</span>
      <span style={{
        fontFamily: 'var(--fnt-mono)',
        fontSize: '10px',
        color: odd.envelope_state === 'IN' ? 'var(--color-stbd)' : 'var(--color-port)',
      }}>{odd.envelope_state}</span>
      <span style={{
        fontFamily: 'var(--fnt-mono)',
        fontSize: '9px',
        color: 'var(--text-2)',
        marginLeft: 'auto',
      }}>{(odd.conformance_score * 100).toFixed(0)}%</span>
    </div>
  );
});
