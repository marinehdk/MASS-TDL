import { memo } from 'react';
import type { SilDebugData } from '../types/sil';
import { CpaPanel } from './CpaPanel';
import { TcpaPanel } from './TcpaPanel';
import { RulePanel } from './RulePanel';
import { DecisionPanel } from './DecisionPanel';
import { ModulePulseBar } from './ModulePulseBar';
import { AsdrLog } from './AsdrLog';

interface Props {
  data: SilDebugData | null;
}

export const SidePanel = memo(function SidePanel({ data }: Props) {
  const d = data;

  return (
    <div style={{
      width: '320px',
      background: 'var(--bg-1)',
      borderLeft: '1px solid var(--line-1)',
      display: 'flex',
      flexDirection: 'column',
      overflowY: 'auto',
      flexShrink: 0,
    }}>
      {/* CPA + TCPA row */}
      <div style={{
        padding: '14px',
        borderBottom: '1px solid var(--line-1)',
      }}>
        <div style={{ display: 'flex', gap: '8px' }}>
          <CpaPanel cpaNm={d?.cpa_nm ?? 0} />
          <TcpaPanel tcpaS={d?.tcpa_s ?? 0} />
        </div>
      </div>

      <RulePanel ruleText={d?.rule_text ?? ''} />
      <DecisionPanel decisionText={d?.decision_text ?? ''} />
      <ModulePulseBar moduleStatus={d?.module_status ?? Array(8).fill(false)} />
      <AsdrLog events={d?.asdr_events ?? []} />
    </div>
  );
});
