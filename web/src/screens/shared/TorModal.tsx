import React, { useEffect, useState } from 'react';
import { createPortal } from 'react-dom';
import { useFsmStore, useTelemetryStore } from '../../store';

export const TorModal: React.FC = () => {
  const currentState = useFsmStore((s) => s.currentState);
  const torRequest = useFsmStore((s) => s.torRequest);
  const setState = useFsmStore((s) => s.setState);
  const setTorRequest = useFsmStore((s) => s.setTorRequest);
  const simTime = useTelemetryStore((s) => s.lifecycleStatus?.sim_time ?? 0);

  const [autoTransitioned, setAutoTransitioned] = useState(false);

  if (!torRequest || currentState !== 'TOR') return null;

  const sat1Elapsed = Math.max(0, simTime - torRequest.sat1LockUntilSimTime + 5);
  const sat1Held = Math.min(5, sat1Elapsed);
  const canAccept = sat1Held >= 5;

  const deadline = Math.max(0, torRequest.tmrDeadlineSimTime - simTime);
  const deadlineColor = deadline < 10 ? 'var(--c-danger)' : deadline < 30 ? 'var(--c-warn)' : 'var(--c-phos)';

  // Auto-MRC on timeout
  useEffect(() => {
    if (simTime >= torRequest.tmrDeadlineSimTime && !autoTransitioned) {
      setAutoTransitioned(true);
      setState('MRC', 'TMR_TIMEOUT', simTime);
      // Clear torRequest after 5s
      const id = setTimeout(() => {
        setTorRequest(null);
        setAutoTransitioned(false);
      }, 5000);
      return () => clearTimeout(id);
    }
  }, [simTime, torRequest.tmrDeadlineSimTime, autoTransitioned, setState, setTorRequest]);

  const handleAccept = () => {
    if (!canAccept) return;
    setState('OVERRIDE', 'CAPTAIN_TAKE_CONTROL', simTime);
    setTorRequest(null);
  };

  const handleDecline = () => {
    setState('MRC', 'CAPTAIN_DECLINE', simTime);
    setTorRequest(null);
  };

  return createPortal(
    <div data-testid="tor-modal" style={{
      position: 'fixed', inset: 0,
      background: 'rgba(7,12,19,0.78)', backdropFilter: 'blur(6px)',
      display: 'flex', alignItems: 'center', justifyContent: 'center',
      zIndex: 100,
    }}>
      <div style={{
        width: 560, border: '2px solid var(--c-warn)', background: 'var(--bg-1)',
        position: 'relative',
      }}>
        {/* Scan line */}
        <div style={{ height: 4, background: 'var(--bg-0)', position: 'relative', overflow: 'hidden' }}>
          <div style={{
            position: 'absolute', inset: 0, width: '30%',
            background: 'linear-gradient(90deg, transparent, var(--c-warn), transparent)',
            animation: 'scan-line 1.6s linear infinite',
          }} />
        </div>

        <div style={{ padding: 20 }}>
          {/* Header */}
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'baseline' }}>
            <span style={{
              fontFamily: 'var(--f-disp)', fontSize: 11, color: 'var(--c-warn)',
              fontWeight: 700, letterSpacing: '0.20em', textTransform: 'uppercase',
            }}>TRANSFER OF RESPONSIBILITY</span>
            <span style={{ fontFamily: 'var(--f-mono)', fontSize: 10, color: 'var(--txt-3)' }}>D3 → D2</span>
          </div>

          {/* Reason */}
          <div style={{
            fontFamily: 'var(--f-disp)', fontSize: 16, color: 'var(--txt-0)',
            fontWeight: 500, lineHeight: 1.4, marginTop: 10,
          }}>
            {torRequest.reason}
          </div>

          {/* Situation */}
          <div style={{ marginTop: 14, display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 8 }}>
            <div style={{
              padding: 8, background: 'var(--bg-0)', borderLeft: '2px solid var(--c-danger)',
            }}>
              <div style={{
                fontFamily: 'var(--f-disp)', fontSize: 8, color: 'var(--c-danger)',
                letterSpacing: '0.18em', textTransform: 'uppercase',
              }}>CURRENT SITUATION</div>
              <div style={{
                fontFamily: 'var(--f-mono)', fontSize: 11, color: 'var(--txt-1)', marginTop: 4,
              }}>
                {torRequest.currentSituation}
              </div>
            </div>
            <div style={{
              padding: 8, background: 'var(--bg-0)', borderLeft: '2px solid var(--c-warn)',
            }}>
              <div style={{
                fontFamily: 'var(--f-disp)', fontSize: 8, color: 'var(--c-warn)',
                letterSpacing: '0.18em', textTransform: 'uppercase',
              }}>PROPOSED ACTION</div>
              <div style={{
                fontFamily: 'var(--f-mono)', fontSize: 11, color: 'var(--txt-1)', marginTop: 4,
              }}>
                {torRequest.proposedAction}
              </div>
            </div>
          </div>

          {/* Countdown + SAT-1 */}
          <div style={{ marginTop: 14, display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 14 }}>
            <div data-testid="tor-countdown">
              <div style={{
                fontFamily: 'var(--f-disp)', fontSize: 8, color: 'var(--txt-3)',
                letterSpacing: '0.18em', textTransform: 'uppercase',
              }}>DEADLINE</div>
              <span style={{
                fontFamily: 'var(--f-mono)', fontSize: 36, color: deadlineColor, fontWeight: 700,
              }}>
                {String(Math.ceil(deadline)).padStart(2, '0')}
                <span style={{ fontSize: 14, color: 'var(--txt-3)' }}> s</span>
              </span>
              <div style={{ height: 4, background: 'var(--bg-0)', marginTop: 4 }}>
                <div style={{
                  width: `${(deadline / 60) * 100}%`, height: '100%',
                  background: deadlineColor, transition: 'width 0.4s',
                }} />
              </div>
            </div>

            <div data-testid="tor-sat1-lock">
              <div style={{
                fontFamily: 'var(--f-disp)', fontSize: 8, color: 'var(--txt-3)',
                letterSpacing: '0.18em', textTransform: 'uppercase',
              }}>SAT-1 SETTLEMENT</div>
              <span style={{
                fontFamily: 'var(--f-mono)', fontSize: 36,
                color: canAccept ? 'var(--c-stbd)' : 'var(--txt-2)', fontWeight: 700,
              }}>
                {sat1Held.toFixed(1)}
                <span style={{ fontSize: 14, color: 'var(--txt-3)' }}> / 5.0 s</span>
              </span>
              <div style={{ height: 4, background: 'var(--bg-0)', marginTop: 4 }}>
                <div style={{
                  width: `${(sat1Held / 5) * 100}%`, height: '100%',
                  background: canAccept ? 'var(--c-stbd)' : 'var(--txt-3)',
                  transition: 'width 0.2s',
                }} />
              </div>
            </div>
          </div>

          {/* Action buttons */}
          <div style={{ marginTop: 16, display: 'flex', gap: 10 }}>
            <button onClick={handleDecline} style={{
              flex: 1, background: 'transparent', border: '1px solid var(--line-3)',
              color: 'var(--txt-1)', padding: '12px 0',
              fontFamily: 'var(--f-disp)', fontSize: 10.5, letterSpacing: '0.18em',
              fontWeight: 700, cursor: 'pointer',
            }}>DECLINE · MRC</button>
            <button
              data-testid="tor-take-control"
              disabled={!canAccept}
              onClick={handleAccept}
              style={{
                flex: 2, background: canAccept ? 'var(--c-warn)' : 'var(--bg-0)',
                border: `1px solid ${canAccept ? 'var(--c-warn)' : 'var(--line-2)'}`,
                color: canAccept ? 'var(--bg-0)' : 'var(--txt-3)',
                padding: '12px 0', fontFamily: 'var(--f-disp)', fontSize: 11,
                letterSpacing: '0.18em', fontWeight: 700,
                cursor: canAccept ? 'pointer' : 'not-allowed',
              }}>
              {canAccept ? 'ACCEPT · TAKE CONTROL (D2)' : `CONFIRM SITUATION (${(5 - sat1Held).toFixed(1)}s)`}
            </button>
          </div>

          <div style={{
            fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)',
            marginTop: 8,
          }}>
            ASDR: sat1_display_duration_s · threats_visible · odd_zone · operator_id · SHA-256
          </div>
        </div>
      </div>
    </div>,
    document.body
  );
};
