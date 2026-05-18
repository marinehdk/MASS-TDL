import { useState } from 'react';
import type { GateSSEEvent } from '../../types/gateStream';
import {
  useRestartServicesMutation, useRestartNodeMutation, useSyncTimeMutation,
  useClearHashCacheMutation, useEnsureAsdrDirMutation,
} from '../../api/silApi';

interface QuickFixPanelProps {
  focusedGateId: number | null;
  gates: GateSSEEvent[];
  scenarioId: string | null;
  runId: string | null;
  onFixApplied: () => void;
}

export function QuickFixPanel({ focusedGateId, gates, scenarioId, runId, onFixApplied }: QuickFixPanelProps) {
  const [restartServices] = useRestartServicesMutation();
  const [restartNode] = useRestartNodeMutation();
  const [syncTime] = useSyncTimeMutation();
  const [clearHashCache] = useClearHashCacheMutation();
  const [ensureAsdrDir] = useEnsureAsdrDirMutation();
  const [runningAction, setRunningAction] = useState<string | null>(null);
  const [lastResult, setLastResult] = useState<string | null>(null);

  const failedGateIds = gates.filter(g => !g.passed).map(g => g.gate_id);

  async function execute(label: string, fn: () => Promise<unknown>) {
    setRunningAction(label);
    setLastResult(null);
    try {
      const res = await fn();
      setLastResult(typeof res === 'object' && res !== null && 'data' in res
        ? JSON.stringify((res as any).data) : 'OK');
      onFixApplied();
    } catch (e) {
      setLastResult(`FAILED: ${(e as Error).message}`);
    } finally {
      setRunningAction(null);
    }
  }

  if (!focusedGateId || failedGateIds.length === 0) return null;

  return (
    <div style={{ padding: '12px 16px', borderTop: '1px solid var(--line-2)', background: 'var(--bg-1)' }}>
      <div style={{ fontFamily: 'var(--f-disp)', fontSize: 12, color: 'var(--txt-0)', marginBottom: 8 }}>QUICK FIX</div>
      <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>

        {focusedGateId === 1 && (
          <>
            <FixButton label={'\u21BB Restart All SIL Services'} running={runningAction}
              onClick={() => execute('Restart Services', () => restartServices())} />
            <FixButton label={'\u21BB Restart Foxglove Bridge'} running={runningAction}
              onClick={() => execute('Restart Foxglove', () => restartNode('foxglove-bridge'))} />
          </>
        )}

        {focusedGateId === 2 && (
          <FixButton label={`\u21BB Restart All Module Containers`} running={runningAction}
            onClick={() => execute('Restart Modules', () => restartServices())} />
        )}

        {focusedGateId === 3 && scenarioId && (
          <FixButton label={'\uD83D\uDDD1 Clear Hash Cache'} running={runningAction}
            onClick={() => execute('Clear Cache', () => clearHashCache(scenarioId!))} />
        )}

        {focusedGateId === 4 && (
          <FixButton label={'\u21BB Reload M1 ODD Config'} running={runningAction}
            onClick={() => execute('Reload M1', () => restartNode('m1_*'))} />
        )}

        {focusedGateId === 5 && (
          <>
            <FixButton label={'\u26A1 Force Sync PTP Clock'} running={runningAction}
              onClick={() => execute('Sync Time', () => syncTime())} />
            <FixButton label={'\uD83D\uDD27 Create ASDR Directory'} running={runningAction}
              onClick={() => execute('Ensure ASDR', () => ensureAsdrDir(runId ?? 'unknown'))} />
          </>
        )}

        {focusedGateId === 6 && (
          <FixButton label={'\u21BB Restart M7 Isolated'} running={runningAction}
            onClick={() => execute('Restart M7', () => restartNode('m7_*'))} />
        )}

        <div style={{ marginTop: 8, borderTop: '1px solid var(--line-2)', paddingTop: 8 }}>
          <FixButton label={'\uD83D\uDED1 Global Reconfigure'} running={runningAction} variant="danger"
            onClick={() => execute('Global Reconfigure', () => restartServices())} />
        </div>

        {lastResult && (
          <div style={{ marginTop: 4, padding: '4px 8px', background: 'var(--bg-2)', borderRadius: 3, fontSize: 10, fontFamily: 'var(--f-mono)', color: 'var(--txt-2)' }}>
            {lastResult}
          </div>
        )}
      </div>
    </div>
  );
}

function FixButton({ label, running, onClick, variant }: {
  label: string; running: string | null; onClick: () => void; variant?: 'danger';
}) {
  const isActive = running === label;
  return (
    <button onClick={onClick} disabled={running !== null} style={{
      padding: '6px 10px', border: 'none', borderRadius: 4, cursor: running ? 'not-allowed' : 'pointer',
      background: variant === 'danger' ? 'rgba(248,81,73,0.15)' : 'var(--bg-2)',
      color: variant === 'danger' ? 'var(--c-danger)' : 'var(--txt-1)',
      fontFamily: 'var(--f-body)', fontSize: 11, textAlign: 'left', opacity: running ? 0.5 : 1,
    }}>
      {isActive ? '\u27F3 Working...' : label}
    </button>
  );
}
