import { useState, useEffect, useCallback, useRef } from 'react';
import { useScenarioStore } from '../store';
import {
  useConfigureLifecycleMutation,
  useActivateLifecycleMutation,
  useCleanupLifecycleMutation,
  useProbeSelfCheckMutation,
} from '../api/silApi';

interface CheckItem {
  name: string;
  passed: boolean;
  detail: string;
}

type CheckPhase = 'idle' | 'checking' | 'passed' | 'failed';

export function Preflight() {
  const [phase, setPhase] = useState<CheckPhase>('idle');
  const [checks, setChecks] = useState<CheckItem[]>([]);
  const [countdown, setCountdown] = useState(3);
  const [errorMsg, setErrorMsg] = useState<string | null>(null);
  const scenarioId = useScenarioStore((s) => s.scenarioId);
  const setRunId = useScenarioStore((s) => s.setRunId);
  const [configure] = useConfigureLifecycleMutation();
  const [activate] = useActivateLifecycleMutation();
  const [cleanup] = useCleanupLifecycleMutation();
  const [probe] = useProbeSelfCheckMutation();
  // Guard against React.StrictMode double-invoke producing duplicate check rows
  const startedRef = useRef(false);
  const activatedRef = useRef(false);

  const runChecks = useCallback(async () => {
    setPhase('checking');
    setErrorMsg(null);
    setChecks([]);

    try {
      // Reset lifecycle to UNCONFIGURED in case a previous run left it elsewhere
      await cleanup().unwrap().catch(() => undefined);
      if (scenarioId) {
        const cfg = await configure(scenarioId).unwrap();
        if (!cfg.success) {
          throw new Error(cfg.error || 'configure failed');
        }
      }

      // Real selfcheck/probe — 5 items expected
      const probeResult = await probe().unwrap();
      // Display each check sequentially for UX
      for (const item of probeResult.items) {
        await new Promise((r) => setTimeout(r, 400));
        setChecks((prev) => [...prev, item]);
      }

      if (!probeResult.all_clear) {
        setPhase('failed');
        setErrorMsg('One or more checks did not pass');
        return;
      }
      // Real run_id is assigned by orchestrator on activate (see countdown effect)
      setPhase('passed');
    } catch (e: any) {
      setPhase('failed');
      setErrorMsg(e?.message || String(e));
    }
  }, [scenarioId, configure, cleanup, probe, setRunId]);

  useEffect(() => {
    if (startedRef.current) return;
    startedRef.current = true;
    runChecks();
  }, [runChecks]);

  // Countdown after checks pass — activate lifecycle, capture run_id from backend
  useEffect(() => {
    if (phase !== 'passed') return;
    if (countdown <= 0) {
      if (activatedRef.current) return;
      activatedRef.current = true;
      activate().unwrap().then((result) => {
        if (result.run_id) setRunId(result.run_id);
        window.location.hash = `#/bridge/${scenarioId}`;
      }).catch((e: any) => {
        setPhase('failed');
        setErrorMsg(e?.data?.error || e?.message || 'activate failed');
      });
      return;
    }
    const timer = setTimeout(() => setCountdown((c) => c - 1), 1000);
    return () => clearTimeout(timer);
  }, [phase, countdown, activate, scenarioId, setRunId]);

  const handleBack = () => {
    window.location.hash = '#/builder';
  };

  return (
    <div style={{
      display: 'flex', flexDirection: 'column',
      alignItems: 'center', justifyContent: 'center',
      height: '100vh', background: '#0b1320', color: '#e6edf3',
    }} data-testid="preflight">
      <h2>Pre-flight Check</h2>

      {checks.map((item, i) => (
        <div key={i} style={{
          margin: 4, fontFamily: 'monospace', fontSize: 14,
          color: item.passed ? '#34d399' : '#f87171',
        }}>
          {item.passed ? '✓' : '✗'} {item.name} — {item.detail}
        </div>
      ))}

      {phase === 'checking' && (
        <div style={{ marginTop: 16, color: '#888' }}>Running checks…</div>
      )}

      {phase === 'failed' && errorMsg && (
        <div style={{ marginTop: 16, color: '#f87171', fontFamily: 'monospace', fontSize: 13 }}>
          ✗ {errorMsg}
        </div>
      )}

      {phase === 'passed' && (
        <div style={{ fontSize: 48, marginTop: 32, fontWeight: 'bold', color: '#2dd4bf' }}>
          Starting in {countdown}...
        </div>
      )}

      <button onClick={handleBack} style={{ marginTop: 24, padding: '8px 24px' }}>
        ← Back
      </button>
    </div>
  );
}
