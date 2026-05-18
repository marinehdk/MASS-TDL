import React, { useState, useEffect, useCallback } from 'react';
import { useGateStream } from '../hooks/useGateStream';
import { useHotkeys } from '../hooks/useHotkeys';
import { useScenarioStore } from '../store';
import { useGetScenarioQuery, useActivateLifecycleMutation, useCleanupLifecycleMutation } from '../api/silApi';
import { GateSequencer } from './shared/GateSequencer';
import { DiagnosticCanvas } from './shared/DiagnosticCanvas';
import { ActionLogs } from './shared/ActionLogs';

const IS_DEV = typeof import.meta !== 'undefined' && (import.meta as any).env?.DEV;

export function SimulationCheck() {
  const scenarioId = window.location.hash.replace('#/check/', '') || null;
  const { runId } = useScenarioStore();
  const { data: scenarioDetail } = useGetScenarioQuery(scenarioId ?? '', { skip: !scenarioId });
  const [activateLifecycle] = useActivateLifecycleMutation();
  const [cleanupLifecycle] = useCleanupLifecycleMutation();

  const { gates, verdict, streaming, error, start, abort } = useGateStream(scenarioId, true);
  const [focusedGateId, setFocusedGateId] = useState<number | null>(null);
  const [countdown, setCountdown] = useState(0);
  const [devSkipReason, setDevSkipReason] = useState('');

  useEffect(() => {
    const lastFail = [...gates].reverse().find(g => !g.passed);
    if (lastFail) setFocusedGateId(lastFail.gate_id);
  }, [gates]);

  useEffect(() => {
    if (verdict === 'GO') {
      setCountdown(3);
      const timer = setInterval(() => {
        setCountdown(prev => {
          if (prev <= 1) { clearInterval(timer); handleProceed(); return 0; }
          return prev - 1;
        });
      }, 1000);
      return () => clearInterval(timer);
    }
  }, [verdict]);

  useHotkeys({
    onTor: verdict !== 'GO' ? () => start() : undefined,
    onFault: () => handleAbort(),
    onMrc: IS_DEV && verdict === 'NO-GO' ? () => handleDevSkip() : undefined,
  });

  const handleProceed = useCallback(async () => {
    if (!scenarioId) return;
    try {
      await activateLifecycle({ scenario_id: scenarioId } as any);
      window.location.hash = `#/monitor/${scenarioId}`;
    } catch (e) { console.error('activate failed:', e); }
  }, [scenarioId, activateLifecycle]);

  const handleAbort = useCallback(async () => {
    abort();
    try { await cleanupLifecycle(); } catch {}
    window.location.hash = '#/scenario';
  }, [abort, cleanupLifecycle]);

  const handleDevSkip = useCallback(async () => {
    if (!scenarioId || !devSkipReason.trim()) return;
    try {
      await fetch('/api/v1/selfcheck/skip', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ scenario_id: scenarioId, reason: devSkipReason }),
      });
      window.location.hash = `#/monitor/${scenarioId}`;
    } catch (e) { console.error('dev skip failed:', e); }
  }, [scenarioId, devSkipReason]);

  if (!scenarioId) {
    return <div style={{ padding: 40, color: 'var(--c-danger)', fontFamily: 'var(--f-body)' }}>No scenario selected</div>;
  }

  return (
    <div style={{ display: 'grid', gridTemplateColumns: '240px 1fr 300px', height: '100vh', overflow: 'hidden' }}>
      <GateSequencer gates={gates} streaming={streaming} focusedGateId={focusedGateId}
        onGateSelect={setFocusedGateId} verdict={verdict} />

      <DiagnosticCanvas focusedGateId={focusedGateId} gates={gates}
        scenarioYaml={scenarioDetail?.yaml_content ?? ''}
        storedYaml={scenarioDetail?.yaml_content ?? ''}
        verdict={verdict} countdown={countdown} />

      <ActionLogs focusedGateId={focusedGateId} gates={gates}
        scenarioId={scenarioId} runId={runId ?? 'unknown'}
        onRerun={start} onAbort={handleAbort} onFixApplied={() => {}} />

      {IS_DEV && verdict === 'NO-GO' && (
        <div style={{ position: 'fixed', bottom: 40, right: 320, zIndex: 100, padding: '12px 16px', background: 'var(--bg-2)', border: '1px solid var(--c-warn)', borderRadius: 6 }}>
          <div style={{ fontFamily: 'var(--f-body)', fontSize: 11, color: 'var(--c-warn)', marginBottom: 8 }}>DEV MODE: SKIP PREFLIGHT</div>
          <input value={devSkipReason} onChange={e => setDevSkipReason(e.target.value)}
            placeholder="Reason for skip..." style={{ padding: '4px 8px', marginRight: 8, border: '1px solid var(--line-2)', borderRadius: 3, background: 'var(--bg-0)', color: 'var(--txt-0)', fontSize: 11 }} />
          <button onClick={handleDevSkip} disabled={!devSkipReason.trim()}
            style={{ padding: '4px 12px', background: 'var(--c-warn)', color: '#000', border: 'none', borderRadius: 3, cursor: 'pointer', fontFamily: 'var(--f-disp)', fontSize: 11 }}>
            SKIP {'\u2192'} MONITOR
          </button>
        </div>
      )}

      {error && (
        <div style={{ position: 'fixed', top: 8, right: 320, zIndex: 100, padding: '8px 16px', background: 'rgba(248,81,73,0.15)', border: '1px solid var(--c-danger)', borderRadius: 4, fontFamily: 'var(--f-mono)', fontSize: 11, color: 'var(--c-danger)' }}>
          SSE Error: {error}
        </div>
      )}
    </div>
  );
}
