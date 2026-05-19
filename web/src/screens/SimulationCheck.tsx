import { useState, useEffect, useCallback, useRef } from 'react';
import { useGateStream } from '../hooks/useGateStream';
import { useHotkeys } from '../hooks/useHotkeys';
import { useScenarioStore } from '../store';
import { useGetScenarioQuery, useConfigureLifecycleMutation, useActivateLifecycleMutation, useCleanupLifecycleMutation } from '../api/silApi';
import { GateSequencer } from './shared/GateSequencer';
import { DiagnosticCanvas } from './shared/DiagnosticCanvas';
import { ActionLogs } from './shared/ActionLogs';

const IS_DEV = typeof import.meta !== 'undefined' && (import.meta as any).env?.DEV;

export function SimulationCheck() {
  const scenarioId = window.location.hash.match(/^#\/check\/([^/?#]+)/)?.[1] ?? null;
  const { runId } = useScenarioStore();
  const { data: scenarioDetail } = useGetScenarioQuery(scenarioId ?? '', { skip: !scenarioId });
  const [configureLifecycle] = useConfigureLifecycleMutation();
  const [activateLifecycle] = useActivateLifecycleMutation();
  const [cleanupLifecycle] = useCleanupLifecycleMutation();

  const { gates, verdict, streaming, error, start, abort } = useGateStream(scenarioId, true);
  const [focusedGateId, setFocusedGateId] = useState<number | null>(null);
  const [countdown, setCountdown] = useState(0);
  const [devSkipReason, setDevSkipReason] = useState('');
  const countdownRef = useRef(0);

  // useCallback definitions MUST come before effects that reference them
  const handleProceed = useCallback(async () => {
    if (!scenarioId) return;
    try {
      // ROS2 lifecycle requires CONFIGURE → ACTIVATE in sequence.
      // cleanup first so re-runs don't get "already configured" rejection.
      await cleanupLifecycle();
      const cfgResult = await configureLifecycle(scenarioId).unwrap();
      if (!cfgResult.success) {
        console.error('configure failed:', cfgResult.error);
        return;
      }
      const actResult = await activateLifecycle().unwrap();
      if (!actResult.success) {
        console.error('activate failed:', actResult.error);
        return;
      }
      window.location.hash = `#/monitor/${scenarioId}`;
    } catch (e) { console.error('lifecycle launch failed:', e); }
  }, [scenarioId, cleanupLifecycle, configureLifecycle, activateLifecycle]);

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

  useEffect(() => {
    const lastFail = [...gates].reverse().find(g => !g.passed);
    if (lastFail) setFocusedGateId(lastFail.gate_id);
  }, [gates]);

  // GO countdown timer — separated from side-effect
  useEffect(() => {
    if (verdict !== 'GO') return;
    setCountdown(3);
    countdownRef.current = 3;
    const timer = setInterval(() => {
      countdownRef.current -= 1;
      setCountdown(countdownRef.current);
      if (countdownRef.current <= 0) clearInterval(timer);
    }, 1000);
    return () => clearInterval(timer);
  }, [verdict]);

  // GO path: when countdown reaches 0, trigger proceed
  useEffect(() => {
    if (countdown === 0 && verdict === 'GO') {
      handleProceed();
    }
  }, [countdown, verdict, handleProceed]);

  useHotkeys({
    onTor: verdict !== 'GO' ? () => start() : undefined,
    onFault: () => handleAbort(),
    onMrc: IS_DEV && verdict === 'NO-GO' ? () => handleDevSkip() : undefined,
  });

  if (!scenarioId) {
    return <div style={{ padding: 40, color: 'var(--c-danger)', fontFamily: 'var(--f-body)' }}>No scenario selected</div>;
  }

  return (
    <div style={{ display: 'grid', gridTemplateColumns: '300px 1fr 400px', height: '100%', overflow: 'hidden', background: 'var(--bg-0)' }}>
      <GateSequencer gates={gates} streaming={streaming} focusedGateId={focusedGateId}
        onGateSelect={setFocusedGateId} verdict={verdict} />

      <DiagnosticCanvas focusedGateId={focusedGateId} gates={gates}
        scenarioYaml={scenarioDetail?.yaml_content ?? ''}
        storedYaml={scenarioDetail?.yaml_content ?? ''}
        verdict={verdict} countdown={countdown} />

      <ActionLogs focusedGateId={focusedGateId} gates={gates}
        scenarioId={scenarioId} runId={runId ?? 'unknown'}
        onRerun={start} onAbort={handleAbort} onFixApplied={() => {}}
        isDev={IS_DEV && verdict === 'NO-GO'}
        devSkipReason={devSkipReason}
        onDevSkipReasonChange={setDevSkipReason}
        onDevSkip={handleDevSkip} />

      {error && (
        <div style={{ position: 'fixed', top: 8, right: 320, zIndex: 100, padding: '8px 16px', background: 'rgba(248,81,73,0.15)', border: '1px solid var(--c-danger)', borderRadius: 4, fontFamily: 'var(--f-mono)', fontSize: 11, color: 'var(--c-danger)' }}>
          SSE Error: {error}
        </div>
      )}
    </div>
  );
}
