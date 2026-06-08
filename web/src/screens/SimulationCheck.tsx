import { useState, useEffect, useCallback, useRef } from 'react';
import { useGateStream } from '../hooks/useGateStream';
import { useHotkeys } from '../hooks/useHotkeys';
import { useScenarioStore, useTelemetryStore, useControlStore } from '../store';
import { useGetScenarioQuery, useConfigureLifecycleMutation, useActivateLifecycleMutation, useCleanupLifecycleMutation, useSkipPreflightMutation } from '../api/silApi';
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
  const [skipPreflight] = useSkipPreflightMutation();

  const { gates, verdict, streaming, error, start, abort } = useGateStream(scenarioId, true);
  const [focusedGateId, setFocusedGateId] = useState<number | null>(null);
  const [countdown, setCountdown] = useState(-1);
  const [devSkipReason, setDevSkipReason] = useState('');
  const [lifecycleError, setLifecycleError] = useState('');
  const [transitioning, setTransitioning] = useState(false);
  const countdownRef = useRef(-1);

  // useCallback definitions MUST come before effects that reference them
  const handleProceed = useCallback(async () => {
    if (!scenarioId) return;
    setTransitioning(true);
    // Fresh run: clear residual telemetry/trail from any prior run and reset
    // sim rate to the 1x default (otherwise a previously-selected 10x persists
    // in the control store and the new run plays at 10x from t≈last position).
    useTelemetryStore.getState().reset();
    useControlStore.getState().reset();
    try {
      // ROS2 lifecycle requires CONFIGURE → ACTIVATE in sequence.
      // cleanup first so re-runs don't get "already configured" rejection.
      await cleanupLifecycle();
      const cfgResult = await configureLifecycle(scenarioId).unwrap();
      if (!cfgResult.success) {
        setLifecycleError(`Configure failed: ${cfgResult.error || 'unknown error'}`);
        setTransitioning(false);
        return;
      }
      const actResult = await activateLifecycle().unwrap();
      if (!actResult.success) {
        setLifecycleError(`Activate failed: ${actResult.error || 'unknown error'}`);
        setTransitioning(false);
        return;
      }
      setLifecycleError('');
      window.location.hash = `#/monitor/${scenarioId}`;
    } catch (e) {
      setLifecycleError(`Lifecycle launch failed: ${e instanceof Error ? e.message : String(e)}`);
      setTransitioning(false);
    }
  }, [scenarioId, cleanupLifecycle, configureLifecycle, activateLifecycle]);

  const handleAbort = useCallback(async () => {
    abort();
    try { await cleanupLifecycle(); } catch {}
    window.location.hash = '#/scenario';
  }, [abort, cleanupLifecycle]);

  const handleDevSkip = useCallback(async () => {
    if (!scenarioId || !devSkipReason.trim()) return;
    try {
      await skipPreflight({ scenario_id: scenarioId, reason: devSkipReason }).unwrap();
      window.location.hash = `#/monitor/${scenarioId}`;
    } catch (e) { console.error('dev skip failed:', e); }
  }, [scenarioId, devSkipReason, skipPreflight]);

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
    <div data-testid="preflight" style={{ display: 'grid', gridTemplateColumns: '300px 1fr 400px', height: '100%', overflow: 'hidden', background: 'var(--bg-0)' }}>
      <div data-testid="preflight-status" style={{ position: 'absolute', top: 8, right: 8, padding: '4px 12px', background: 'var(--bg-1)', borderRadius: 4, zIndex: 10, fontFamily: 'var(--f-mono)', fontSize: 11, color: verdict === 'GO' ? 'var(--c-stbd)' : verdict === 'NO-GO' ? 'var(--c-danger)' : 'var(--txt-3)' }}>
        {verdict ?? (streaming ? 'RUNNING' : 'IDLE')}
      </div>
      <GateSequencer gates={gates} streaming={streaming} focusedGateId={focusedGateId}
        onGateSelect={setFocusedGateId} verdict={verdict} />

      <DiagnosticCanvas focusedGateId={focusedGateId} gates={gates}
        scenarioYaml={scenarioDetail?.yaml_content ?? ''}
        storedYaml={scenarioDetail?.yaml_content ?? ''}
        verdict={verdict} countdown={countdown}
        transitioning={transitioning} />

      <ActionLogs focusedGateId={focusedGateId} gates={gates}
        scenarioId={scenarioId} runId={runId ?? 'unknown'}
        onRerun={start} onAbort={handleAbort} onFixApplied={() => {}}
        isDev={IS_DEV && verdict === 'NO-GO'}
        devSkipReason={devSkipReason}
        onDevSkipReasonChange={setDevSkipReason}
        onDevSkip={handleDevSkip} />

      {lifecycleError && (
        <div style={{ position: 'fixed', top: 8, left: '50%', transform: 'translateX(-50%)', zIndex: 101, padding: '10px 20px', background: 'rgba(248,81,73,0.2)', border: '1px solid var(--c-danger)', borderRadius: 6, fontFamily: 'var(--f-mono)', fontSize: 12, color: 'var(--c-danger)', textAlign: 'center', maxWidth: '80vw' }}>
          {lifecycleError}
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
