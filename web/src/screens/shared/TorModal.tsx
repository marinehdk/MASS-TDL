import React, { useEffect, useRef, useState, useCallback } from 'react';
import { createPortal } from 'react-dom';
import { useFsmStore, useTelemetryStore } from '../../store';

const HOLD_DURATION_MS = 2000;  // IMO MASS Code Part 2-A §6.3.2

export const TorModal: React.FC = () => {
  const currentState = useFsmStore((s) => s.currentState);
  const torRequest   = useFsmStore((s) => s.torRequest);
  const setState     = useFsmStore((s) => s.setState);
  const setTorRequest = useFsmStore((s) => s.setTorRequest);
  const simTime      = useTelemetryStore((s) => s.lifecycleStatus?.sim_time ?? 0);

  const [holdProgress, setHoldProgress] = useState(0);  // 0.0–1.0
  const [autoTransitioned, setAutoTransitioned] = useState(false);
  const holdStartRef    = useRef<number | null>(null);
  const intervalRef     = useRef<ReturnType<typeof setInterval> | null>(null);

  const clearHold = useCallback(() => {
    holdStartRef.current = null;
    if (intervalRef.current) clearInterval(intervalRef.current);
    intervalRef.current = null;
    setHoldProgress(0);
  }, []);

  const handlePointerDown = useCallback(() => {
    holdStartRef.current = Date.now();
    intervalRef.current = setInterval(() => {
      const elapsed = Date.now() - (holdStartRef.current ?? Date.now());
      const progress = Math.min(1, elapsed / HOLD_DURATION_MS);
      setHoldProgress(progress);
      if (progress >= 1) {
        // clearHold before setState — idempotent if pointer-up also fires
        clearHold();
        setState('OVERRIDE', 'CAPTAIN_TAKE_CONTROL', simTime);
        setTorRequest(null);
      }
    }, 50);
  }, [simTime, setState, setTorRequest, clearHold]);

  const handlePointerUp = useCallback(() => {
    // Capture elapsed BEFORE clearHold nulls holdStartRef
    const elapsed = holdStartRef.current ? Date.now() - holdStartRef.current : 0;
    clearHold();
    if (elapsed >= HOLD_DURATION_MS) {
      // handlePointerDown interval may also have fired — double setState is idempotent
      setState('OVERRIDE', 'CAPTAIN_TAKE_CONTROL', simTime);
      setTorRequest(null);
    }
  }, [simTime, setState, setTorRequest, clearHold]);

  // Auto-MRC on timeout
  useEffect(() => {
    if (!torRequest) return;
    if (simTime >= torRequest.tmrDeadlineSimTime && !autoTransitioned) {
      setAutoTransitioned(true);
      setState('MRC', 'TMR_TIMEOUT', simTime);
      const id = setTimeout(() => { setTorRequest(null); setAutoTransitioned(false); }, 5000);
      return () => clearTimeout(id);
    }
  }, [simTime, torRequest, autoTransitioned, setState, setTorRequest]);

  // Cleanup interval on unmount
  useEffect(() => () => { if (intervalRef.current) clearInterval(intervalRef.current); }, []);

  if (!torRequest || currentState !== 'TOR') return null;

  const deadline = Math.max(0, torRequest.tmrDeadlineSimTime - simTime);
  const deadlineColor = deadline < 10 ? 'var(--c-danger)' : deadline < 30 ? 'var(--c-warn)' : 'var(--c-phos)';

  // Phase 0/1/2 escalation styling
  const elapsed60 = 60 - deadline;
  const borderColor = elapsed60 >= 45 ? '#8b0000' : elapsed60 >= 20 ? 'var(--c-warn)' : 'var(--line-2)';

  return createPortal(
    <div data-testid="tor-modal" style={{
      position: 'fixed', inset: 0,
      background: 'rgba(7,12,19,0.78)', backdropFilter: 'blur(6px)',
      display: 'flex', alignItems: 'center', justifyContent: 'center',
      zIndex: 100, pointerEvents: 'auto',
    }}>
      <div style={{
        width: 560, border: `2px solid ${borderColor}`,
        background: 'var(--bg-1)', position: 'relative',
        boxShadow: elapsed60 >= 45 ? '0 0 32px rgba(139,0,0,0.6)' : elapsed60 >= 20 ? '0 0 24px rgba(251,191,36,0.4)' : 'none',
      }}>
        {/* Scan line */}
        <div style={{ height: 4, background: 'var(--bg-0)', position: 'relative', overflow: 'hidden' }}>
          <div style={{
            position: 'absolute', inset: 0, width: '30%',
            background: `linear-gradient(90deg, transparent, ${borderColor}, transparent)`,
            animation: 'scan-line 1.6s linear infinite',
          }} />
        </div>

        <div style={{ padding: 20 }}>
          {/* Header */}
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'baseline' }}>
            <span style={{
              fontFamily: 'var(--f-disp)', fontSize: 11, color: 'var(--c-warn)',
              fontWeight: 700, letterSpacing: '0.20em', textTransform: 'uppercase',
            }}>⚠ TRANSFER OF RESPONSIBILITY REQUEST</span>
            <span style={{ fontFamily: 'var(--f-mono)', fontSize: 10, color: 'var(--txt-3)' }}>
              {torRequest.recommendedMrm ?? 'MRM-01'}
            </span>
          </div>

          {/* Reason */}
          <div style={{
            fontFamily: 'var(--f-disp)', fontSize: 15, color: 'var(--txt-0)',
            fontWeight: 500, lineHeight: 1.4, marginTop: 10,
          }}>{torRequest.reason}</div>

          {/* Situation / Action */}
          <div style={{ marginTop: 14, display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 8 }}>
            <div style={{ padding: 8, background: 'var(--bg-0)', borderLeft: '2px solid var(--c-danger)' }}>
              <div style={{ fontFamily: 'var(--f-disp)', fontSize: 8, color: 'var(--c-danger)', letterSpacing: '0.18em', textTransform: 'uppercase' }}>CURRENT SITUATION</div>
              <div style={{ fontFamily: 'var(--f-mono)', fontSize: 11, color: 'var(--txt-1)', marginTop: 4 }}>{torRequest.currentSituation}</div>
            </div>
            <div style={{ padding: 8, background: 'var(--bg-0)', borderLeft: '2px solid var(--c-warn)' }}>
              <div style={{ fontFamily: 'var(--f-disp)', fontSize: 8, color: 'var(--c-warn)', letterSpacing: '0.18em', textTransform: 'uppercase' }}>PROPOSED ACTION</div>
              <div style={{ fontFamily: 'var(--f-mono)', fontSize: 11, color: 'var(--txt-1)', marginTop: 4 }}>{torRequest.proposedAction}</div>
            </div>
          </div>

          {/* Countdown */}
          <div style={{ marginTop: 14 }}>
            <div style={{ fontFamily: 'var(--f-disp)', fontSize: 8, color: 'var(--txt-3)', letterSpacing: '0.18em', textTransform: 'uppercase' }}>DEADLINE</div>
            <div data-testid="tor-countdown" style={{ fontFamily: 'var(--f-mono)', fontSize: 36, color: deadlineColor, fontWeight: 700 }}>
              {String(Math.ceil(deadline)).padStart(2, '0')}
              <span style={{ fontSize: 14, color: 'var(--txt-3)' }}> s</span>
            </div>
            <div style={{ height: 4, background: 'var(--bg-0)', marginTop: 4 }}>
              <div style={{ width: `${(deadline / 60) * 100}%`, height: '100%', background: deadlineColor, transition: 'width 0.4s' }} />
            </div>
          </div>

          {/* TAKE OVER button with pointer-hold mechanic */}
          <div style={{ marginTop: 16, display: 'flex', gap: 10 }}>
            <button
              data-testid="tor-decline"
              onClick={() => { setState('MRC', 'CAPTAIN_DECLINE', simTime); setTorRequest(null); }}
              style={{
                flex: 1, background: 'transparent', border: '1px solid var(--line-3)',
                color: 'var(--txt-1)', padding: '12px 0',
                fontFamily: 'var(--f-disp)', fontSize: 10.5, letterSpacing: '0.18em',
                fontWeight: 700, cursor: 'pointer',
              }}
            >DECLINE · MRC</button>
            <button
              data-testid="tor-take-control"
              onPointerDown={handlePointerDown}
              onPointerUp={handlePointerUp}
              onPointerLeave={handlePointerUp}
              style={{
                flex: 2, background: holdProgress > 0 ? `rgba(251,191,36,${0.1 + holdProgress * 0.5})` : 'var(--bg-0)',
                border: `1px solid ${holdProgress > 0 ? 'var(--c-warn)' : 'var(--line-2)'}`,
                color: holdProgress > 0 ? 'var(--c-warn)' : 'var(--txt-2)',
                padding: '12px 0', fontFamily: 'var(--f-disp)', fontSize: 11,
                letterSpacing: '0.18em', fontWeight: 700, cursor: 'pointer',
                userSelect: 'none', position: 'relative', overflow: 'hidden',
              }}
            >
              {holdProgress > 0 ? `HOLD TO CONFIRM (${((1 - holdProgress) * 2).toFixed(1)}s)` : 'HOLD ≥2s · TAKE CONTROL'}
            </button>
          </div>

          {/* Hold progress indicator */}
          {holdProgress > 0 && (
            <div data-testid="tor-hold-progress" style={{ marginTop: 4, height: 3, background: 'var(--bg-0)' }}>
              <div style={{
                width: `${holdProgress * 100}%`, height: '100%',
                background: 'var(--c-warn)', transition: 'width 0.05s linear',
              }} />
            </div>
          )}

          <div style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)', marginTop: 8 }}>
            IMO MASS Code Part 2-A §6.3.2 · ASDR: sat1_display_duration_s · operator_id
          </div>
        </div>
      </div>
    </div>,
    document.body
  );
};
