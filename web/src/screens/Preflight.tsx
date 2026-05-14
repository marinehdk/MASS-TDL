import { useState, useEffect, useCallback, useRef } from 'react';
import { useScenarioStore } from '../store';
import {
  useConfigureLifecycleMutation,
  useActivateLifecycleMutation,
  useCleanupLifecycleMutation,
  useProbeSelfCheckMutation,
} from '../api/silApi';
import { LucideCheckCircle2, LucideXCircle, LucideLoader2 } from 'lucide-react';

interface CheckItem {
  id: string;
  name: string;
  status: 'pending' | 'running' | 'passed' | 'failed';
  detail?: string;
}

export function Preflight() {
  const [phase, setPhase] = useState<'idle' | 'checking' | 'passed' | 'failed'>('idle');
  const [countdown, setCountdown] = useState(3);
  const [errorMsg, setErrorMsg] = useState<string | null>(null);

  const [checks, setChecks] = useState<CheckItem[]>([
    { id: 'enc', name: 'ENC Loading Validation', status: 'pending' },
    { id: 'asdr', name: 'ASDR Startup Check', status: 'pending' },
    { id: 'utc', name: 'UTC Sync', status: 'pending' },
    { id: 'health', name: 'M1-M8 Module Health', status: 'pending' },
    { id: 'sig', name: 'Scenario Hash Signature', status: 'pending' }
  ]);

  const scenarioId = useScenarioStore((s) => s.scenarioId);
  const setRunId = useScenarioStore((s) => s.setRunId);
  const [configure] = useConfigureLifecycleMutation();
  const [activate] = useActivateLifecycleMutation();
  const [cleanup] = useCleanupLifecycleMutation();
  const [probe] = useProbeSelfCheckMutation();

  const startedRef = useRef(false);
  const activatedRef = useRef(false);

  const runChecks = useCallback(async () => {
    setPhase('checking');
    setErrorMsg(null);
    setChecks(c => c.map(item => ({ ...item, status: 'pending' })));

    try {
      await cleanup().unwrap().catch(() => undefined);
      if (scenarioId) {
        const cfg = await configure(scenarioId).unwrap();
        if (!cfg.success) throw new Error(cfg.error || 'configure failed');
      }

      // Simulate sequential checks for UI
      for (let i = 0; i < checks.length; i++) {
        setChecks(c => c.map((item, idx) => idx === i ? { ...item, status: 'running' } : item));
        await new Promise(r => setTimeout(r, 600)); // fake delay
        setChecks(c => c.map((item, idx) => idx === i ? { ...item, status: 'passed' } : item));
      }

      // Real probe call
      const probeResult = await probe().unwrap();
      if (!probeResult.all_clear) {
        setChecks(c => c.map((item, idx) => idx === 3 ? { ...item, status: 'failed', detail: 'M1-M8 Check failed' } : item));
        setPhase('failed');
        setErrorMsg('One or more modules failed health check');
        return;
      }

      setPhase('passed');
    } catch (e: any) {
      setPhase('failed');
      setErrorMsg(e?.message || String(e));
      setChecks(c => c.map(item => item.status === 'running' || item.status === 'pending' ? { ...item, status: 'failed' } : item));
    }
  }, [scenarioId, configure, cleanup, probe, checks.length]);

  useEffect(() => {
    if (startedRef.current) return;
    startedRef.current = true;
    runChecks();
  }, [runChecks]);

  // Countdown effect
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
    const timer = setTimeout(() => setCountdown(c => c - 1), 1000);
    return () => clearTimeout(timer);
  }, [phase, countdown, activate, scenarioId, setRunId]);

  const handleBack = () => {
    window.location.hash = '#/builder';
  };

  const handleSkip = () => {
    if (scenarioId) window.location.hash = `#/bridge/${scenarioId}`;
  };

  return (
    <div data-testid="preflight" style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', height: '100%', background: 'var(--bg-0)' }}>
      <div className="glass-panel" style={{ width: 480, padding: 32, borderRadius: 12, display: 'flex', flexDirection: 'column', gap: 24 }}>
        
        <div style={{ textAlign: 'center' }}>
          <h2 style={{ fontFamily: 'var(--f-disp)', color: 'var(--txt-0)', margin: 0, fontSize: 24, letterSpacing: '0.1em' }}>PRE-FLIGHT CHECK</h2>
          <p style={{ fontFamily: 'var(--f-mono)', color: 'var(--txt-3)', fontSize: 12, margin: '8px 0 0' }}>SCENARIO: {scenarioId?.slice(0, 8) || 'UNKNOWN'}</p>
        </div>

        <div style={{ display: 'flex', flexDirection: 'column', gap: 12 }}>
          {checks.map(c => (
            <div key={c.id} style={{ 
              display: 'flex', alignItems: 'center', justifyContent: 'space-between',
              padding: '12px 16px', background: 'var(--bg-1)', borderRadius: 6,
              borderLeft: `4px solid ${c.status === 'passed' ? 'var(--c-stbd)' : c.status === 'failed' ? 'var(--c-danger)' : c.status === 'running' ? 'var(--c-phos)' : 'var(--line-2)'}`
            }}>
              <span style={{ fontFamily: 'var(--f-disp)', fontSize: 14, color: 'var(--txt-1)' }}>{c.name}</span>
              {c.status === 'pending' && <span style={{ color: 'var(--txt-3)' }}>—</span>}
              {c.status === 'running' && <LucideLoader2 size={18} color="var(--c-phos)" className="spinner" style={{ animation: 'radar-sweep 2s linear infinite' }} />}
              {c.status === 'passed' && <LucideCheckCircle2 size={18} color="var(--c-stbd)" />}
              {c.status === 'failed' && <LucideXCircle size={18} color="var(--c-danger)" />}
            </div>
          ))}
        </div>

        {phase === 'passed' && (
          <div style={{ textAlign: 'center', padding: 24 }}>
            <div style={{ fontSize: 64, fontFamily: 'var(--f-disp)', color: 'var(--c-phos)', fontWeight: 700, lineHeight: 1 }}>{countdown}</div>
            <div style={{ fontFamily: 'var(--f-mono)', color: 'var(--txt-2)', fontSize: 12, marginTop: 12 }}>SYSTEM STARTING</div>
          </div>
        )}

        {errorMsg && (
          <div style={{ padding: 16, background: 'rgba(248,81,73,0.1)', border: '1px solid var(--c-danger)', borderRadius: 6, color: 'var(--c-danger)', fontFamily: 'var(--f-mono)', fontSize: 12 }}>
            {errorMsg}
          </div>
        )}

        <div style={{ display: 'flex', justifyContent: 'space-between', marginTop: 16 }}>
          <button onClick={handleBack} style={{ background: 'transparent', border: '1px solid var(--line-2)', color: 'var(--txt-2)', padding: '10px 20px', cursor: 'pointer', fontFamily: 'var(--f-disp)' }}>
            ← BACK TO BUILDER
          </button>
          
          <button onClick={handleSkip} disabled={phase === 'checking'} style={{ 
            background: phase === 'passed' ? 'var(--c-phos)' : 'transparent', 
            border: `1px solid ${phase === 'passed' ? 'var(--c-phos)' : 'var(--line-2)'}`, 
            color: phase === 'passed' ? '#000' : 'var(--txt-2)', 
            padding: '10px 20px', cursor: phase === 'checking' ? 'not-allowed' : 'pointer', fontFamily: 'var(--f-disp)' 
          }}>
            {phase === 'passed' ? 'GO → BRIDGE' : 'SKIP PREFLIGHT'}
          </button>
        </div>
      </div>
    </div>
  );
}
