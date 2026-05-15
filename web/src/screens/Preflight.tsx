import { useState, useEffect, useCallback, useRef } from 'react';
import { useScenarioStore } from '../store';
import {
  useConfigureLifecycleMutation,
  useActivateLifecycleMutation,
  useCleanupLifecycleMutation,
  useProbeSelfCheckMutation,
  useSkipPreflightMutation,
} from '../api/silApi';
import { GateCard } from './shared/GateCard';
import { GoNoGoPanel } from './shared/GoNoGoPanel';
import { LiveLogStream } from './shared/LiveLogStream';
import type { GateCheckResult } from '../api/silApi';

const IS_DEV = (import.meta as any).env?.DEV || new URLSearchParams(window.location.search).get('dev') === '1';

export function Preflight() {
  const [phase, setPhase] = useState<'idle' | 'checking' | 'passed' | 'failed'>('idle');
  const [gates, setGates] = useState<GateCheckResult[]>([]);
  const [goNoGo, setGoNoGo] = useState<'GO' | 'NO-GO' | null>(null);
  const [errorMsg, setErrorMsg] = useState<string | null>(null);
  const [skipReason, setSkipReason] = useState('');
  const [showSkipInput, setShowSkipInput] = useState(false);

  const scenarioIdFromStore = useScenarioStore((s) => s.scenarioId);
  const scenarioIdFromHash = (() => {
    const raw = window.location.hash.replace('#/check/', '').split('/')[0];
    return raw && raw !== '#' && raw !== 'check' ? raw : null;
  })();
  const scenarioId = scenarioIdFromStore || scenarioIdFromHash;
  const setScenario = useScenarioStore((s) => s.setScenario);
  const setRunId = useScenarioStore((s) => s.setRunId);

  useEffect(() => {
    if (!scenarioIdFromStore && scenarioIdFromHash) setScenario(scenarioIdFromHash, '');
  }, [scenarioIdFromStore, scenarioIdFromHash, setScenario]);

  const [configure] = useConfigureLifecycleMutation();
  const [activate] = useActivateLifecycleMutation();
  const [cleanup] = useCleanupLifecycleMutation();
  const [probe] = useProbeSelfCheckMutation();
  const [skipMutation] = useSkipPreflightMutation();

  const startedRef = useRef(false);
  const activatedRef = useRef(false);

  const runChecks = useCallback(async () => {
    setPhase('checking');
    setErrorMsg(null);
    setGates([]);

    try {
      await cleanup().unwrap().catch(() => {});
      if (scenarioId) {
        const cfg = await configure(scenarioId).unwrap();
        if (!cfg.success) throw new Error(cfg.error || 'configure failed');
      }

      const probing = await probe({ scenario_id: scenarioId || undefined }).unwrap();
      setGates(probing.gates);
      setGoNoGo(probing.go_no_go);

      if (probing.go_no_go === 'GO') {
        setPhase('passed');
      } else {
        setPhase('failed');
        const failed = probing.gates.filter(g => !g.passed).map(g => g.gate_id);
        setErrorMsg(`Gates FAILED: ${failed.map((g: number) => `GATE ${g}`).join(', ')}`);
      }
    } catch (e: any) {
      setPhase('failed');
      setErrorMsg(e?.data?.error || e?.message || String(e));
    }
  }, [scenarioId, configure, cleanup, probe]);

  useEffect(() => {
    if (startedRef.current) return;
    startedRef.current = true;
    runChecks();
  }, [runChecks]);

  const handleProceed = useCallback(async () => {
    if (activatedRef.current) return;
    activatedRef.current = true;
    try {
      const result = await activate().unwrap();
      if (result.run_id) setRunId(result.run_id);
      window.location.hash = `#/monitor/${scenarioId}`;
    } catch (e: any) {
      setPhase('failed');
      setErrorMsg(e?.data?.error || e?.message || 'activate failed');
      activatedRef.current = false;
    }
  }, [activate, scenarioId, setRunId]);

  const handleAbort = useCallback(() => {
    window.location.hash = '#/scenario';
  }, []);

  const handleDeactivateReconfigure = useCallback(async () => {
    await cleanup().unwrap().catch(() => {});
    startedRef.current = false;
    activatedRef.current = false;
    setPhase('idle');
    setGates([]);
    setGoNoGo(null);
    window.location.hash = '#/scenario';
  }, [cleanup]);

  const handleSkip = useCallback(async () => {
    if (!scenarioId || !skipReason.trim()) return;
    try {
      await skipMutation({ scenario_id: scenarioId, reason: skipReason }).unwrap();
      window.location.hash = `#/monitor/${scenarioId}`;
    } catch (e: any) {
      setErrorMsg(e?.data?.error || e?.message || 'skip failed');
    }
  }, [skipMutation, scenarioId, skipReason]);

  const failedGates = gates.filter(g => !g.passed).map(g => g.gate_id);

  return (
    <div data-testid="preflight" style={{
      display: 'flex', flexDirection: 'column', height: '100vh',
    }}>
      {/* Header */}
      <div style={{
        display: 'flex', justifyContent: 'space-between', alignItems: 'center',
        padding: '12px 24px', borderBottom: '1px solid var(--line-1)',
        background: 'var(--bg-1)',
      }}>
        <h2 style={{
          fontFamily: 'var(--f-disp)', fontSize: 20, letterSpacing: '0.1em',
          color: 'var(--txt-0)', margin: 0,
        }}>SIMULATION-CHECK</h2>
        <span style={{ fontFamily: 'var(--f-mono)', color: 'var(--txt-3)', fontSize: 12 }}>
          SCENARIO: {scenarioId || 'N/A'}
        </span>
      </div>

      {/* Main content */}
      <div style={{ flex: 1, overflowY: 'auto', padding: '24px' }}>
        <div style={{ maxWidth: 720, margin: '0 auto', display: 'flex', flexDirection: 'column', gap: 10 }}>
          {phase === 'checking' && (
            <div style={{
              textAlign: 'center', padding: 12, fontFamily: 'var(--f-mono)', fontSize: 11,
              color: 'var(--c-phos)',
            }}>
              RUNNING 6-GATE SEQUENCER…
            </div>
          )}

          {gates.map(gate => (
            <GateCard
              key={gate.gate_id}
              gate={gate}
              defaultExpanded={gate.gate_id === 6 || !gate.passed}
            />
          ))}

          {errorMsg && (
            <div style={{
              padding: 12, background: 'rgba(248,81,73,0.1)',
              border: '1px solid var(--c-danger)', borderRadius: 4,
              color: 'var(--c-danger)', fontFamily: 'var(--f-mono)', fontSize: 11,
            }}>
              {errorMsg}
            </div>
          )}

          {goNoGo && (
            <GoNoGoPanel
              goNoGo={goNoGo}
              onProceed={handleProceed}
              onAbort={handleAbort}
              onDeactivateReconfigure={handleDeactivateReconfigure}
              failedGates={failedGates}
            />
          )}

          {/* SKIP PREFLIGHT — DEV ONLY */}
          {IS_DEV && phase === 'failed' && (
            <div style={{
              border: '1px dashed var(--line-2)', borderRadius: 4, padding: 14,
              background: 'var(--bg-1)',
            }}>
              <div style={{
                fontFamily: 'var(--f-disp)', fontSize: 12, color: 'var(--txt-2)',
                marginBottom: 8, letterSpacing: '0.05em',
              }}>
                DEV MODE: SKIP PREFLIGHT
              </div>
              {!showSkipInput ? (
                <button onClick={() => setShowSkipInput(true)} style={{
                  background: 'transparent', border: '1px solid var(--c-warn)',
                  color: 'var(--c-warn)', padding: '6px 16px', cursor: 'pointer',
                  fontFamily: 'var(--f-disp)', fontSize: 11, borderRadius: 4,
                }}>
                  SKIP PREFLIGHT → MONITOR
                </button>
              ) : (
                <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
                  <input
                    placeholder="REQUIRED: reason for skip…"
                    value={skipReason}
                    onChange={e => setSkipReason(e.target.value)}
                    style={{
                      flex: 1, background: 'var(--bg-0)', border: '1px solid var(--line-1)',
                      color: 'var(--txt-1)', padding: '6px 10px', borderRadius: 4,
                      fontFamily: 'var(--f-mono)', fontSize: 11,
                    }}
                  />
                  <button onClick={handleSkip} disabled={!skipReason.trim()} style={{
                    background: skipReason.trim() ? 'var(--c-warn)' : 'var(--bg-2)',
                    border: 'none', color: '#000', padding: '6px 16px',
                    cursor: skipReason.trim() ? 'pointer' : 'not-allowed',
                    fontFamily: 'var(--f-disp)', fontSize: 11, fontWeight: 600, borderRadius: 4,
                  }}>
                    SKIP (ASDR LOGGED)
                  </button>
                </div>
              )}
              <div style={{ fontFamily: 'var(--f-mono)', fontSize: 8, color: 'var(--txt-3)', marginTop: 6 }}>
                Verdict: warning_unverified_run — will be recorded in ASDR
              </div>
            </div>
          )}
        </div>
      </div>

      {/* Bottom: LiveLogStream */}
      <div style={{ height: 200, borderTop: '1px solid var(--line-1)' }}>
        <LiveLogStream />
      </div>
    </div>
  );
}
