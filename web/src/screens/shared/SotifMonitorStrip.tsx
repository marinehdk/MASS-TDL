import React from 'react';
import type { SotifMetrics } from '../../types/sat';

interface SotifMonitorStripProps {
  metrics: SotifMetrics | null;
  recommendedMrm?: string;
}

function buildRows(m: SotifMetrics) {
  return [
    {
      testId: 'ais', label: 'AIS/雷达一致性',
      value: m.ais_radar_consistency_sigma, displayValue: `${m.ais_radar_consistency_sigma.toFixed(1)}σ`,
      maxValue: 4, threshold: 2.0, violated: m.ais_radar_consistency_sigma > 2.0,
    },
    {
      testId: 'predictability', label: '目标可预测性 RMS',
      value: m.target_predictability_rms_m, displayValue: `${m.target_predictability_rms_m.toFixed(0)}m`,
      maxValue: 100, threshold: 50, violated: m.target_predictability_rms_m > 50,
    },
    {
      testId: 'coverage', label: '感知覆盖充分性',
      value: m.perception_coverage_pct, displayValue: `${m.perception_coverage_pct.toFixed(0)}%`,
      maxValue: 100, threshold: 80, violated: m.perception_coverage_pct < 80,
    },
    {
      testId: 'colregs', label: 'COLREGs解析失败',
      value: m.colregs_parse_failures, displayValue: `${m.colregs_parse_failures}次`,
      maxValue: 10, threshold: 3, violated: m.colregs_parse_failures > 3,
    },
    {
      testId: 'comm', label: '通信链路质量',
      value: m.comm_link_rtt_ms, displayValue: `${m.comm_link_rtt_ms}ms`,
      maxValue: 3000, threshold: 2000, violated: m.comm_link_rtt_ms > 2000,
    },
    {
      testId: 'checker', label: 'Checker否决率',
      value: m.checker_veto_rate_pct, displayValue: `${m.checker_veto_rate_pct.toFixed(0)}%`,
      maxValue: 40, threshold: 20, violated: m.checker_veto_rate_pct > 20,
    },
  ];
}

const MRM_LABELS: Record<string, string> = {
  'MRM-01': '减速至安全速度，保持航向',
  'MRM-02': '紧急转向避让',
  'MRM-03': '立即停车',
  'MRM-04': '全速倒车',
};

export const SotifMonitorStrip: React.FC<SotifMonitorStripProps> = ({ metrics, recommendedMrm }) => {
  if (!metrics) return null;

  const rows = buildRows(metrics);
  const anyViolated = rows.some((r) => r.violated);
  const mrm = recommendedMrm ?? 'MRM-01';
  const mrmDesc = MRM_LABELS[mrm] ?? '减速至安全速度，保持航向';

  return (
    <div data-testid="sotif-metrics-panel" style={{ fontFamily: 'var(--f-mono)', fontSize: 10, color: 'var(--txt-1)' }}>
      <div style={{
        padding: '6px 12px', background: 'var(--bg-0)',
        borderBottom: '1px solid var(--line-2)',
        display: 'flex', justifyContent: 'space-between',
      }}>
        <span style={{ color: 'var(--c-phos)', fontWeight: 700, letterSpacing: '0.1em', fontSize: 9, textTransform: 'uppercase' }}>
          M7 SOTIF 假设监控
        </span>
        <span style={{
          fontSize: 9,
          color: anyViolated ? '#f87171' : '#34d399',
          animation: anyViolated ? 'sotif-pulse 1s ease-in-out infinite' : 'none',
        }}>
          {anyViolated ? '⚠ VIOLATION' : '✓ NOMINAL'}
        </span>
      </div>

      {rows.map((row) => {
        const barPct = Math.min(100, (row.value / row.maxValue) * 100);
        const barColor = row.violated ? '#f87171' : barPct > 79 ? '#fbbf24' : '#34d399';

        return (
          <div
            key={row.testId}
            data-testid={`sotif-row-${row.testId}`}
            data-violated={String(row.violated)}
            style={{
              padding: '5px 12px',
              borderBottom: '1px solid var(--line-2)',
              background: row.violated ? 'rgba(248,81,73,0.08)' : 'transparent',
              animation: row.violated ? 'sotif-pulse 1s ease-in-out infinite' : 'none',
            }}
          >
            <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 3 }}>
              <span style={{ color: row.violated ? '#f87171' : 'var(--txt-2)', fontSize: 9 }}>
                {row.label}
              </span>
              <span style={{ color: row.violated ? '#f87171' : 'var(--txt-1)', fontWeight: row.violated ? 700 : 400 }}>
                {row.displayValue}
                {row.violated ? ' 🔴' : ' 🟢'}
              </span>
            </div>
            <div style={{ height: 3, background: 'var(--bg-0)', borderRadius: 1 }}>
              <div style={{
                width: `${barPct}%`, height: '100%',
                background: barColor, borderRadius: 1, transition: 'width 0.5s',
              }} />
            </div>
          </div>
        );
      })}

      {anyViolated && (
        <div style={{
          margin: '8px 12px', padding: '6px 8px',
          background: 'rgba(248,81,73,0.1)', border: '1px solid #f87171',
          fontSize: 9, color: '#f87171',
        }}>
          推荐: {mrm}（{mrmDesc}）· 等待 M1 仲裁
        </div>
      )}

      <style>{`
        @keyframes sotif-pulse {
          0%, 100% { opacity: 1; }
          50% { opacity: 0.6; }
        }
      `}</style>
    </div>
  );
};
