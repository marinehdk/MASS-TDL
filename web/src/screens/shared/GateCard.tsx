import { useState } from 'react';
import { LucideCheckCircle2, LucideXCircle, LucideChevronDown, LucideChevronRight } from 'lucide-react';
import type { GateCheckResult } from '../../api/silApi';

interface GateCardProps {
  gate: GateCheckResult;
  defaultExpanded?: boolean;
}

export function GateCard({ gate, defaultExpanded = false }: GateCardProps) {
  const [expanded, setExpanded] = useState(defaultExpanded);
  const isPassed = gate.passed;
  const nOk = gate.checks.filter(c => c.startsWith('[ok]')).length;
  const nTotal = gate.checks.length;

  return (
    <div style={{
      border: `1px solid ${isPassed ? 'var(--c-stbd)' : 'var(--c-danger)'}`,
      borderLeft: `4px solid ${isPassed ? 'var(--c-stbd)' : 'var(--c-danger)'}`,
      background: isPassed ? 'rgba(0,227,179,0.03)' : 'rgba(248,81,73,0.05)',
      borderRadius: 4,
      overflow: 'hidden',
    }}    >
      <button
        onClick={() => setExpanded(!expanded)}
        style={{
          width: '100%', display: 'flex', alignItems: 'center', justifyContent: 'space-between',
          padding: '10px 14px', background: 'transparent', border: 'none', cursor: 'pointer',
          color: 'var(--txt-0)', fontFamily: 'inherit',
        }}
      >
        <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
          {isPassed
            ? <LucideCheckCircle2 size={20} color="var(--c-stbd)" />
            : <LucideXCircle size={20} color="var(--c-danger)" />
          }
          <span style={{ fontFamily: 'var(--f-disp)', fontSize: 14, letterSpacing: '0.05em' }}>
            GATE {gate.gate_id} · {gate.label}
          </span>
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
          <span style={{
            fontFamily: 'var(--f-mono)', fontSize: 11,
            color: isPassed ? 'var(--c-stbd)' : 'var(--c-danger)',
          }}>
            [{nOk}/{nTotal}] {isPassed ? 'PASS' : 'FAIL'}
          </span>
          <span style={{ color: 'var(--txt-3)', fontSize: 10 }}>
            {gate.duration_ms.toFixed(0)}ms
          </span>
          {expanded ? <LucideChevronDown size={14} color="var(--txt-3)" /> : <LucideChevronRight size={14} color="var(--txt-3)" />}
        </div>
      </button>
      {expanded && (
        <div style={{ padding: '0 14px 10px', borderTop: '1px solid var(--line-1)' }}>
          {gate.checks.map((check, i) => {
            const isOk = check.startsWith('[ok]');
            const isFail = check.startsWith('[fail]');
            const isWarn = check.startsWith('[warn]');
            const isInfo = check.startsWith('[info]');
            return (
              <div key={i} style={{
                display: 'flex', gap: 8, padding: '3px 0',
                fontFamily: 'var(--f-mono)', fontSize: 9, lineHeight: 1.6,
                color: isOk ? 'var(--c-stbd)' : isFail ? 'var(--c-danger)' : isWarn ? 'var(--c-warn)' : 'var(--txt-2)',
              }}>
                <span style={{ flexShrink: 0, width: 16 }}>
                  {isOk ? '\u2713' : isFail ? '\u2717' : isWarn ? '\u26A0' : '\u2139'}
                </span>
                <span>{check.replace(/^\[(ok|fail|warn|info)\]\s*/, '')}</span>
              </div>
            );
          })}
          <div style={{ marginTop: 6, fontSize: 9, color: 'var(--txt-3)', fontStyle: 'italic' }}>
            {gate.rationale}
          </div>
        </div>
      )}
    </div>
  );
}
