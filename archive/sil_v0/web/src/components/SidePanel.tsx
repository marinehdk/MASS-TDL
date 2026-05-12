import { memo } from 'react';
import type { SilDebugData } from '../types/sil';
import { CpaPanel } from './CpaPanel';
import { TcpaPanel } from './TcpaPanel';
import { RulePanel } from './RulePanel';
import { DecisionPanel } from './DecisionPanel';
import { ModulePulseBar } from './ModulePulseBar';
import { AsdrLog } from './AsdrLog';
import { SatView } from './SatView';
import { ScenarioSelector } from './ScenarioSelector';
import { PlaybackControls } from './PlaybackControls';
import { OddPanel } from './OddPanel';

interface Props {
  data: SilDebugData | null;
  scenarioId: string;
  onScenarioChange: (id: string) => void;
}

export const SidePanel = memo(function SidePanel({ data, scenarioId, onScenarioChange }: Props) {
  const d = data;

  const sat1Cards = (
    <>
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
    </>
  );

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
      <SatView sat1Content={sat1Cards} />
      <ScenarioSelector value={scenarioId} onChange={onScenarioChange} />
      <PlaybackControls scenarioId={scenarioId} disabled={false} />
      <OddPanel odd={d?.odd ?? null} />
    </div>
  );
});
