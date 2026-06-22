import React from 'react';
import type { SAT2Data, SotifMetrics } from '../../types/sat';
import type { AvoidancePhaseState } from './avoidancePhase';

interface DecisionProcessPanelProps {
  phaseState: AvoidancePhaseState;
  sat2: SAT2Data | null;
  sotifMetrics: SotifMetrics | null;
  safetyAlert: { recommendedMrm?: string | null } | null;
}

export const DecisionProcessPanel: React.FC<DecisionProcessPanelProps> = ({ phaseState }) => {
  return (
    <div
      data-testid="decision-process-panel"
      style={{
        display: 'flex',
        flexDirection: 'column',
        background: 'rgba(10, 15, 24, 0.96)',
        border: '1px solid var(--line-2)',
        borderRadius: 8,
        overflow: 'hidden',
        color: 'var(--txt-1)',
        width: '100%',
      }}
    >
      <header style={{ padding: '12px 14px', borderBottom: '1px solid var(--line-2)', background: 'rgba(91,192,190,0.08)' }}>
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', gap: 12 }}>
          <span style={{ color: 'var(--c-phos)', fontFamily: 'var(--f-disp)', fontSize: 13, fontWeight: 900, letterSpacing: '0.08em' }}>
            避碰过程
          </span>
          <span data-testid="decision-process-phase" style={{ color: 'var(--txt-1)', fontFamily: 'var(--f-mono)', fontSize: 10, fontWeight: 800 }}>
            {phaseState.phaseLabel}
          </span>
        </div>
        <div style={{ color: 'var(--txt-3)', fontFamily: 'var(--f-mono)', fontSize: 10, marginTop: 5 }}>
          {phaseState.phaseReason}
        </div>
      </header>

      <div
        style={{
          padding: '18px 14px',
          minHeight: 220,
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          textAlign: 'center',
          color: 'var(--txt-3)',
          fontFamily: 'var(--f-mono)',
          fontSize: 11,
          lineHeight: 1.7,
        }}
      >
        仿真过程链路待重新映射
      </div>
    </div>
  );
};
