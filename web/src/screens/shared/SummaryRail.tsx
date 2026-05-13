import React from 'react';

interface SummaryRailProps {
  summary: {
    name: string;
    imazuId: number;
    targets: number;
    duration: number;
    seed: number;
    odd: string;
    daypart: string;
  };
  validationStatus: 'red' | 'yellow' | 'green';
  onValidate: () => void;
  onRunPreflight: () => void;
}

export const SummaryRail: React.FC<SummaryRailProps> = ({
  summary, validationStatus, onValidate, onRunPreflight,
}) => {
  const statusColor = validationStatus === 'red' ? 'var(--c-danger)'
    : validationStatus === 'yellow' ? 'var(--c-warn)' : 'var(--c-stbd)';

  return (
    <div data-testid="summary-rail" style={{
      width: 280, background: 'var(--bg-1)', borderLeft: '1px solid var(--line-2)',
      padding: 14, display: 'flex', flexDirection: 'column', gap: 10, flexShrink: 0,
    }}>
      <div style={{
        fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--c-phos)',
        letterSpacing: '0.18em', textTransform: 'uppercase',
      }}>
        SCENARIO SUMMARY
      </div>

      <div style={{
        background: 'var(--bg-0)', border: '1px solid var(--line-2)', padding: 10,
      }}>
        <div style={{
          fontFamily: 'var(--f-disp)', fontSize: 12, color: 'var(--txt-0)',
          fontWeight: 700, letterSpacing: '0.10em',
        }}>{summary.name}</div>
        <div style={{
          fontFamily: 'var(--f-mono)', fontSize: 8.5, color: 'var(--txt-3)', marginTop: 2,
        }}>
          SHA: 7f3a · v1.1.2
        </div>
        <div style={{ height: 1, background: 'var(--line-1)', margin: '8px 0' }} />
        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 6, rowGap: 6 }}>
          <Field label="Imazu" value={`IM${String(summary.imazuId).padStart(2, '0')}`} />
          <Field label="Targets" value={String(summary.targets)} />
          <Field label="Duration" value={`${summary.duration}s`} />
          <Field label="Seed" value={String(summary.seed)} />
          <Field label="ODD" value={summary.odd} />
          <Field label="Day" value={summary.daypart} />
        </div>
      </div>

      {/* Validation status */}
      <div style={{ marginTop: 4 }}>
        <div style={{
          fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)',
          letterSpacing: '0.16em', textTransform: 'uppercase', marginBottom: 4,
        }}>VALIDATION</div>
        <div style={{ display: 'flex', flexDirection: 'column', gap: 3 }}>
          {[
            ['几何参数完整', true],
            ['ODD 包络一致', true],
            ['评估指标已配置', true],
            ['故障剧本已审核', false],
          ].map((row, i) => (
            <div key={i} style={{
              display: 'flex', justifyContent: 'space-between', alignItems: 'center',
              padding: '3px 6px', background: 'var(--bg-0)',
              fontFamily: 'var(--f-body)', fontSize: 10, color: 'var(--txt-1)',
            }}>
              <span>{row[0]}</span>
              <span style={{
                fontFamily: 'var(--f-mono)', fontSize: 9,
                color: row[1] ? 'var(--c-stbd)' : 'var(--c-warn)', fontWeight: 700,
              }}>
                {row[1] ? '✓ OK' : '⚠ 待确认'}
              </span>
            </div>
          ))}
        </div>
      </div>

      <div style={{ flex: 1 }} />

      {/* Action buttons */}
      <button onClick={onValidate} style={{
        background: 'transparent', border: `1px solid var(--line-3)`,
        color: 'var(--txt-1)', padding: '10px 0',
        fontFamily: 'var(--f-disp)', fontSize: 11, letterSpacing: '0.20em',
        fontWeight: 600, cursor: 'pointer',
      }}>
        SAVE AS PRESET
      </button>
      <button onClick={onRunPreflight} data-testid="validate-cta" style={{
        background: 'var(--c-phos)', border: `1px solid var(--c-phos)`, color: 'var(--bg-0)',
        padding: '14px 0', fontFamily: 'var(--f-disp)', fontSize: 12, letterSpacing: '0.22em', fontWeight: 700, cursor: 'pointer',
      }}>
        VALIDATE & PRE-FLIGHT →
      </button>
    </div>
  );
};

function Field({ label, value }: { label: string; value: string }) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
      <span style={{ fontFamily: 'var(--f-disp)', fontSize: 7.5, color: 'var(--txt-3)', textTransform: 'uppercase' }}>{label}</span>
      <span style={{ fontFamily: 'var(--f-mono)', fontSize: 10, color: 'var(--txt-0)' }}>{value}</span>
    </div>
  );
}
