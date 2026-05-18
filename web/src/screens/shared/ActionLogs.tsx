import type { GateSSEEvent } from '../../types/gateStream';
import { LiveLogStream } from './LiveLogStream';
import { QuickFixPanel } from './QuickFixPanel';

const GATE_FILTER_MAP: Record<number, string> = {
  1: 'foxglove|docker',
  2: 'm7_safety',
  3: 'scenario|odd',
  4: 'scenario|odd',
  5: 'clock|chrony',
  6: 'm7|cgroup',
};

interface ActionLogsProps {
  focusedGateId: number | null;
  gates: GateSSEEvent[];
  scenarioId: string | null;
  runId: string | null;
  onRerun: () => void;
  onAbort: () => void;
  onFixApplied: () => void;
}

export function ActionLogs({ focusedGateId, gates, scenarioId, runId, onRerun, onAbort, onFixApplied }: ActionLogsProps) {
  const nodeFilter = focusedGateId ? GATE_FILTER_MAP[focusedGateId] : undefined;
  const failedCount = gates.filter(g => !g.passed).length;

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', background: 'var(--bg-1)', borderLeft: '1px solid var(--line-2)' }}>
      <div style={{ padding: '12px 16px', borderBottom: '1px solid var(--line-2)', display: 'flex', gap: 8, alignItems: 'center' }}>
        <span style={{ fontFamily: 'var(--f-disp)', fontSize: 13, color: 'var(--txt-0)' }}>ACTIONS &amp; LOGS</span>
        {failedCount > 0 && (
          <span style={{ fontSize: 10, fontFamily: 'var(--f-mono)', padding: '2px 6px', borderRadius: 3, background: 'rgba(248,81,73,0.15)', color: 'var(--c-danger)' }}>
            {failedCount} FAIL
          </span>
        )}
      </div>

      <div style={{ flex: '1 1 60%', overflow: 'hidden', borderBottom: '1px solid var(--line-2)' }}>
        <LiveLogStream nodeFilter={nodeFilter} maxLines={200} />
      </div>

      <div style={{ flex: '0 0 auto' }}>
        <QuickFixPanel focusedGateId={focusedGateId} gates={gates}
          scenarioId={scenarioId} runId={runId} onFixApplied={onFixApplied} />
      </div>

      <div style={{ padding: '8px 16px', display: 'flex', gap: 8, borderTop: '1px solid var(--line-2)' }}>
        <button onClick={onRerun} style={{
          flex: 1, padding: '6px 12px', border: 'none', borderRadius: 4, cursor: 'pointer',
          background: 'var(--c-phos)', color: '#000', fontFamily: 'var(--f-disp)', fontSize: 12,
        }}>
          {'\u21BB'} Re-run Checks
        </button>
        <button onClick={onAbort} style={{
          flex: 1, padding: '6px 12px', border: '1px solid var(--c-danger)', borderRadius: 4, cursor: 'pointer',
          background: 'transparent', color: 'var(--c-danger)', fontFamily: 'var(--f-disp)', fontSize: 12,
        }}>
          {'\u2715'} ABORT
        </button>
      </div>
    </div>
  );
}
