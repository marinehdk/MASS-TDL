import { useState, useEffect, useCallback, useRef } from 'react';
import { useScenarioStore } from '../store';
import {
  useConfigureLifecycleMutation,
  useActivateLifecycleMutation,
  useCleanupLifecycleMutation,
  useProbeSelfCheckMutation,
} from '../api/silApi';
import { ModuleReadinessGrid } from './shared/ModuleReadinessGrid';
import { SensorStatusRow } from './shared/SensorStatusRow';
import { CommLinkStatusRow } from './shared/CommLinkStatusRow';
import { LiveLogStream } from './shared/LiveLogStream';

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

  // Derived state for GO/NO-GO
  const allPassed = phase === 'passed';

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
    <div style={{ display: 'grid', gridTemplateColumns: '1fr 380px', height: '100%' }}>
      {/* LEFT: Checks area */}
      <div style={{
        padding: 16, overflowY: 'auto',
        display: 'flex', flexDirection: 'column', gap: 14,
      }}>
        {/* Header + actions */}
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-end' }}>
          <div>
            <div style={{
              fontFamily: 'var(--f-disp)', fontSize: 16, color: 'var(--txt-0)',
              fontWeight: 700, letterSpacing: '0.14em', textTransform: 'uppercase',
            }}>PRE-FLIGHT CHECK</div>
            <div style={{
              fontFamily: 'var(--f-mono)', fontSize: 10, color: 'var(--txt-3)', marginTop: 2,
            }}>
              scenario · {scenarioId?.slice(0, 8) || 'N/A'} · checking module readiness
            </div>
          </div>
          <div style={{ display: 'flex', gap: 8 }}>
            <button onClick={runChecks} disabled={phase === 'checking'} style={{
              background: 'transparent', border: '1px solid var(--line-3)', color: 'var(--txt-1)',
              padding: '8px 16px', fontFamily: 'var(--f-disp)', fontSize: 10.5,
              letterSpacing: '0.18em', fontWeight: 600, cursor: 'pointer',
              opacity: phase === 'checking' ? 0.5 : 1,
            }}>
              {phase === 'checking' ? 'CHECKING…' : allPassed ? 'RE-RUN' : 'RUN CHECKS'}
            </button>
            <button
              data-testid="go-nogo-gate"
              disabled={!allPassed}
              onClick={() => {
                if (scenarioId) window.location.hash = `#/bridge/${scenarioId}`;
              }}
              style={{
                background: allPassed ? 'var(--c-phos)' : 'transparent',
                border: `1px solid ${allPassed ? 'var(--c-phos)' : 'var(--line-2)'}`,
                color: allPassed ? 'var(--bg-0)' : 'var(--txt-3)',
                padding: '8px 16px', fontFamily: 'var(--f-disp)', fontSize: 10.5,
                letterSpacing: '0.18em', fontWeight: 700,
                cursor: allPassed ? 'pointer' : 'not-allowed',
              }}>
              {allPassed ? 'GO → BRIDGE' : 'NO-GO'}
            </button>
          </div>
        </div>

        {/* Module Readiness Grid */}
        <ModuleReadinessGrid />

        {/* Sensor Status */}
        <SensorStatusRow />

        {/* Comm-link Status */}
        <CommLinkStatusRow />

        {/* Countdown and Error display */}
        {phase === 'checking' && checks.length > 0 && (
          <div style={{
            padding: 10, background: 'rgba(91,192,190,0.08)',
            border: '1px solid var(--c-phos)',
            fontFamily: 'var(--f-mono)', fontSize: 10, color: 'var(--c-phos)',
          }}>
            Running checks… {checks.filter(c => c.passed).length}/{checks.length} passed
          </div>
        )}

        {errorMsg && (
          <div style={{
            padding: 10, background: 'rgba(248,81,73,0.08)',
            border: '1px solid var(--c-danger)',
            fontFamily: 'var(--f-mono)', fontSize: 10, color: 'var(--c-danger)',
          }}>
            {errorMsg}
          </div>
        )}

        {/* Legacy check items (from current probe flow) */}
        {checks.length > 0 && (
          <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
            {checks.map((c, i) => (
              <div key={i} style={{
                display: 'flex', alignItems: 'center', gap: 8,
                padding: '6px 10px', background: 'var(--bg-1)',
                borderLeft: `2px solid ${c.passed ? 'var(--c-stbd)' : 'var(--c-danger)'}`,
                fontFamily: 'var(--f-mono)', fontSize: 10,
              }}>
                <span style={{ color: c.passed ? 'var(--c-stbd)' : 'var(--c-danger)' }}>
                  {c.passed ? '✓' : '✗'}
                </span>
                <span style={{ color: 'var(--txt-1)' }}>{c.name}</span>
                <span style={{ color: 'var(--txt-3)', fontSize: 9, marginLeft: 'auto' }}>{c.detail}</span>
              </div>
            ))}
          </div>
        )}

        {/* Bottom: navigate buttons */}
        <div style={{ display: 'flex', justifyContent: 'space-between', marginTop: 'auto' }}>
          <button onClick={handleBack} style={{
            background: 'transparent', border: '1px solid var(--line-3)', color: 'var(--txt-1)',
            padding: '8px 18px', fontFamily: 'var(--f-disp)', fontSize: 10,
            letterSpacing: '0.18em', fontWeight: 600, cursor: 'pointer',
          }}>{'←'} SCENARIO</button>
        </div>
      </div>

      {/* RIGHT: Live Log Stream */}
      <div style={{ borderLeft: '1px solid var(--line-2)', overflow: 'hidden' }}>
        <LiveLogStream />
      </div>
    </div>
  );
}
