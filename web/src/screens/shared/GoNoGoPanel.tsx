import { useEffect, useState } from 'react';
import { LucideCheckCircle2, LucideXCircle } from 'lucide-react';

interface GoNoGoPanelProps {
  goNoGo: 'GO' | 'NO-GO';
  onProceed: () => void;
  onAbort: () => void;
  onDeactivateReconfigure: () => void;
  failedGates: number[];
  countdownSeconds?: number;
}

export function GoNoGoPanel({
  goNoGo,
  onProceed,
  onAbort,
  onDeactivateReconfigure,
  failedGates,
  countdownSeconds = 3,
}: GoNoGoPanelProps) {
  const [countdown, setCountdown] = useState(countdownSeconds);
  const isGo = goNoGo === 'GO';

  useEffect(() => {
    if (!isGo) return;
    if (countdown <= 0) {
      onProceed();
      return;
    }
    const timer = setTimeout(() => setCountdown(c => c - 1), 1000);
    return () => clearTimeout(timer);
  }, [isGo, countdown, onProceed]);

  return (
    <div style={{
      border: `2px solid ${isGo ? 'var(--c-stbd)' : 'var(--c-danger)'}`,
      background: isGo ? 'rgba(0,227,179,0.05)' : 'rgba(248,81,73,0.08)',
      borderRadius: 8,
      padding: '24px 32px',
      textAlign: 'center',
    }}>
      <div style={{ marginBottom: 16 }}>
        {isGo ? (
          <LucideCheckCircle2 size={48} color="var(--c-stbd)" style={{ margin: '0 auto' }} />
        ) : (
          <LucideXCircle size={48} color="var(--c-danger)" style={{ margin: '0 auto' }} />
        )}
      </div>

      <div style={{
        fontFamily: 'var(--f-disp)', fontSize: 28, letterSpacing: '0.15em',
        color: isGo ? 'var(--c-stbd)' : 'var(--c-danger)', marginBottom: 8,
      }}>
        {isGo ? 'GO' : 'NO-GO'}
      </div>

      {isGo ? (
        <>
          <div style={{ fontSize: 48, fontFamily: 'var(--f-disp)', color: 'var(--c-phos)', fontWeight: 700 }}>
            {countdown}
          </div>
          <div style={{ fontFamily: 'var(--f-mono)', fontSize: 11, color: 'var(--txt-2)', marginTop: 8 }}>
            AUTO-ACTIVATING IN {countdown} SECONDS…
          </div>
        </>
      ) : (
        <div style={{ fontFamily: 'var(--f-mono)', fontSize: 11, color: 'var(--txt-2)' }}>
          Gates FAILED: {failedGates.map(g => `GATE ${g}`).join(', ')}
        </div>
      )}

      <div style={{ display: 'flex', justifyContent: 'center', gap: 12, marginTop: 20 }}>
        <button onClick={onAbort} style={{
          background: 'transparent', border: '1px solid var(--line-2)',
          color: 'var(--txt-2)', padding: '10px 24px', cursor: 'pointer',
          fontFamily: 'var(--f-disp)', fontSize: 13, borderRadius: 4,
        }}>
          ABORT
        </button>
        {isGo ? (
          <button onClick={onProceed} style={{
            background: 'var(--c-phos)', border: 'none',
            color: '#000', padding: '10px 24px', cursor: 'pointer',
            fontFamily: 'var(--f-disp)', fontSize: 13, fontWeight: 600, borderRadius: 4,
          }}>
            PROCEED NOW
          </button>
        ) : (
          <button onClick={onDeactivateReconfigure} style={{
            background: 'var(--c-danger)', border: 'none',
            color: '#000', padding: '10px 24px', cursor: 'pointer',
            fontFamily: 'var(--f-disp)', fontSize: 13, fontWeight: 600, borderRadius: 4,
          }}>
            DEACTIVATE + RECONFIGURE
          </button>
        )}
      </div>
    </div>
  );
}
