import type { RuntimeVerdict } from '../../api/silApi';

export function EvidenceStrip({ evidencePath, verdict }: { evidencePath?: string; verdict?: RuntimeVerdict }) {
  return (
    <div
      style={{
        display: 'flex',
        justifyContent: 'space-between',
        gap: 12,
        alignItems: 'center',
        border: '1px solid var(--line-1)',
        borderRadius: 6,
        background: 'var(--bg-1)',
        padding: '8px 10px',
      }}
    >
      <span style={{ color: evidencePath ? 'var(--txt-1)' : 'var(--txt-3)', fontFamily: 'var(--f-mono)', fontSize: 11, wordBreak: 'break-all' }}>
        {evidencePath ?? 'No runtime evidence'}
      </span>
      {verdict && (
        <strong style={{ color: verdict === 'GO' ? 'var(--c-stbd)' : verdict === 'NO-GO' ? 'var(--c-danger)' : 'var(--txt-2)', fontSize: 12 }}>
          {verdict}
        </strong>
      )}
    </div>
  );
}
