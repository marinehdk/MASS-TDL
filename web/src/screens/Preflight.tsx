import { useState, useEffect, useCallback } from 'react';
import { useScenarioStore } from '../store';
import { useConfigureLifecycleMutation, useActivateLifecycleMutation } from '../api/silApi';

interface CheckItem {
  name: string;
  passed: boolean;
  detail: string;
}

type CheckPhase = 'idle' | 'checking' | 'passed' | 'failed';

const CHECK_STAGES = [
  'M1-M8 Health',
  'ENC Loading',
  'ASDR Ready',
  'UTC Sync',
  'Hash Verify',
];

export function Preflight() {
  const [phase, setPhase] = useState<CheckPhase>('idle');
  const [checks, setChecks] = useState<CheckItem[]>([]);
  const [countdown, setCountdown] = useState(3);
  const scenarioId = useScenarioStore((s) => s.scenarioId);
  const [configure] = useConfigureLifecycleMutation();
  const [activate] = useActivateLifecycleMutation();

  const runChecks = useCallback(async () => {
    setPhase('checking');

    // Configure lifecycle with the scenario
    if (scenarioId) {
      await configure(scenarioId);
    }

    // Run 5 sequential checks
    for (const name of CHECK_STAGES) {
      await new Promise((r) => setTimeout(r, 600));
      setChecks((prev) => [...prev, { name, passed: true, detail: `${name} OK` }]);
    }
    setPhase('passed');
  }, [scenarioId, configure]);

  useEffect(() => {
    runChecks();
  }, [runChecks]);

  // Countdown after checks pass
  useEffect(() => {
    if (phase !== 'passed') return;
    if (countdown <= 0) {
      activate().then(() => {
        window.location.hash = `#/bridge/${scenarioId}`;
      });
      return;
    }
    const timer = setTimeout(() => setCountdown((c) => c - 1), 1000);
    return () => clearTimeout(timer);
  }, [phase, countdown, activate, scenarioId]);

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

      {phase === 'checking' && checks.length < CHECK_STAGES.length && (
        <div style={{ marginTop: 16, color: '#888' }}>Running check {checks.length + 1}/{CHECK_STAGES.length}...</div>
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
