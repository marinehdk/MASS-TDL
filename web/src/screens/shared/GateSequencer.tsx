import type { GateSSEEvent } from '../../types/gateStream';

const SIX_GATE_LABELS: Record<number, string> = {
  1: 'System Readiness', 2: 'Module Health', 3: 'Scenario Integrity',
  4: 'ODD-Scenario', 5: 'Time Base + Evidence', 6: 'Doer-Checker',
};

interface GateSequencerProps {
  gates: GateSSEEvent[];
  streaming: boolean;
  focusedGateId: number | null;
  onGateSelect: (gateId: number) => void;
  verdict: 'GO' | 'NO-GO' | null;
}

export function GateSequencer({ gates, streaming, focusedGateId, onGateSelect, verdict }: GateSequencerProps) {
  const gateMap = new Map(gates.map(g => [g.gate_id, g]));
  const verdictBg = verdict === 'GO' ? 'var(--c-stbd)' : verdict === 'NO-GO' ? 'var(--c-danger)' : 'var(--txt-3)';
  const verdictLabel = verdict ?? (streaming ? 'CHECKING' : 'IDLE');

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', background: 'var(--bg-1)', borderRight: '1px solid var(--line-2)' }}>
      <div style={{ padding: '12px 16px', fontFamily: 'var(--f-disp)', fontSize: 13, color: 'var(--txt-0)', borderBottom: '1px solid var(--line-2)' }}>
        GATE PROGRESS
      </div>
      <div style={{ flex: 1, overflowY: 'auto', padding: '8px 0' }}>
        {[1, 2, 3, 4, 5, 6].map(gateId => {
          const result = gateMap.get(gateId);
          const isFocused = focusedGateId === gateId;
          const isPending = !result && streaming;
          const isRunning = isPending && (gates.length + 1 === gateId);
          const isPassed = result?.passed === true;
          const isFailed = result?.passed === false;

          let icon = '\u25CB';
          let iconColor = 'var(--txt-3)';
          if (isPassed) { icon = '\u2705'; iconColor = 'var(--c-stbd)'; }
          else if (isFailed) { icon = '\u274C'; iconColor = 'var(--c-danger)'; }
          else if (isRunning) { icon = '\u27F3'; iconColor = 'var(--c-warn)'; }

          return (
            <div key={gateId} onClick={() => onGateSelect(gateId)} style={{
              display: 'flex', alignItems: 'center', gap: 8, padding: '8px 16px',
              cursor: 'pointer', background: isFocused ? 'var(--bg-2)' : 'transparent',
              borderLeft: isFocused ? '3px solid var(--c-phos)' : '3px solid transparent',
              animation: isRunning ? 'pulse 1.5s ease-in-out infinite' : 'none',
            }}>
              <span style={{ fontSize: 14, width: 20, textAlign: 'center', color: iconColor }}>{icon}</span>
              <span style={{ flex: 1, fontSize: 11, fontFamily: 'var(--f-body)', color: 'var(--txt-1)' }}>
                GATE {gateId} \u00B7 {SIX_GATE_LABELS[gateId]}
              </span>
              {result && <span style={{ fontSize: 10, fontFamily: 'var(--f-mono)', color: 'var(--txt-2)' }}>{result.duration_ms}ms</span>}
            </div>
          );
        })}
      </div>
      <div style={{ padding: '12px 16px', borderTop: '1px solid var(--line-2)', textAlign: 'center' }}>
        <span style={{ display: 'inline-block', padding: '4px 12px', borderRadius: 4, background: verdictBg, color: '#000', fontFamily: 'var(--f-disp)', fontSize: 12 }}>
          {verdictLabel}
        </span>
      </div>
    </div>
  );
}
